#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/un.h>
#include "cameras.h"
#include "json.h"

extern struct camera *cameras;
extern int n_cameras;
extern int should_terminate;
extern char *store_dir;

static struct sockaddr_un address, client;
static int socket_fd, connection_fd;
static socklen_t address_length;
static struct pollfd fds[10];
static int nfds = 1, rc;
static int timeout = 5000;
static char buffer[2048];
static unsigned int session_id = 0;

static struct camera** get_cams_by_ids(int *cam_ids, int ncams) {
  int i, j;
  struct camera **cams = (struct camera **)malloc(ncams * sizeof(struct camera *));

  for(i=0; i<ncams; i++) {
    for(j=0; j<n_cameras; j++) {
      if(cameras[j].id == cam_ids[i]) {
        cams[i] = &cameras[j];
        break;
      }
    }
  }
  return cams;
}

static void snd(int fd, char *buffer, int *close_conn) {
  int rc = send(fd, buffer, strlen(buffer), 0);
  if(rc < 0) {
    perror("send");
    *close_conn = 1;
  }
}

static void parse_command(int client_fd, int *close_conn) {
  json_settings settings;
  char error[256];
  int i;

  int type = 0;
  int ncams = 0;
  int *cam_ids = NULL;
  int ssid = 0;
  time_t timestamp = 0;

  struct screen *screen = NULL;

  memset(&settings, 0, sizeof (json_settings));
  json_value *json = json_parse_ex(&settings, buffer, strlen(buffer), error);

  if(json == NULL) {
    fprintf(stderr, "Json error: %s\n", error);
    fprintf(stderr, "buffer: %s\n", buffer);
  }

  for(i=0; i < json->u.object.length; i++) {
    char *key = json->u.object.values[i].name;
    json_value *value = json->u.object.values[i].value;

    if(strcmp(key, "type") == 0) {
      char *string_value = value->u.string.ptr;
      if(strcmp(string_value, "archive")==0)
        type = ARCHIVE;
      else if(strcmp(string_value, "real")==0)
        type = REAL;
      else if(strcmp(string_value, "stats")==0)
        type = STATS;
    } else if (strcmp(key, "cam_ids") == 0) {
      ncams = value->u.array.length;
      cam_ids = (int*)malloc(ncams * sizeof(int));
      for(i = 0; i < ncams; i++) {
        json_value *arr_elem = value->u.array.values[i];
        cam_ids[i] = arr_elem->u.integer;
      }
    } else if (strcmp(key, "timestamp") == 0) {
      timestamp = value->u.integer;
    } else if (strcmp(key, "session_id") == 0) {
      ssid = value->u.integer;
    }
  }
  json_value_free(json);

  if(type == ARCHIVE || type == REAL) {
    if(ssid == 0) {
      session_id += 1;
      ssid = session_id;
    }

    screen = (struct screen *)malloc(sizeof(struct screen));
    screen->type = type;
    screen->ncams = ncams;
    screen->session_id = ssid;
    screen->cams = get_cams_by_ids(cam_ids, ncams);
    screen->timestamp = timestamp;

    printf("session_id: %d, type: %d, ncams: %d, timestamp: %ld\n", session_id, type, ncams, timestamp);

    init_screen(screen);

    sprintf(buffer, "{ \"session_id\": %d, \"width\": %d, \"height\": %d }\n", screen->session_id, screen->width, screen->height);
    snd(client_fd, buffer, close_conn);
  } else if(type == STATS) {
    struct statvfs statvfs_buf;
    if(statvfs(store_dir, &statvfs_buf) < 0) {
      perror("statvfs");
      return;
    }
    sprintf(buffer, "{ \"free_space\": %llu, \"tolal_space\": %llu, \"ncams\": %d }\n",
      (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_bavail,
      (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_blocks, n_cameras);
    snd(client_fd, buffer, close_conn);
  }
}

void initialize_control_socket() {
  int flags, on = 1;
  memset(&address, 0, address_length);
  memset(&fds, 0 , sizeof(fds));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, "/tmp/videoserver");
  unlink(address.sun_path);
  address_length = strlen(address.sun_path) + sizeof(address.sun_family);

  if((socket_fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
    error("socket");
  if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    error("setsockopt");
  if((flags = fcntl(socket_fd, F_GETFL, 0)) < 0)
    error("fcntl");
  if(fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    error("fcntl");
  if(bind(socket_fd, (struct sockaddr *)&address, address_length) < 0)
    error("bind");
  if(listen(socket_fd, 5) < 0)
    error("listen"); 
}

void control_socket_loop() {
  int current_size, close_conn, compress_array = 0;
  int i,j,flags;

  address_length = sizeof(struct sockaddr_un);
  fds[0].fd = socket_fd;
  fds[0].events = POLLIN;

  while(!should_terminate) {
    if((rc = poll(fds, nfds, timeout)) < 0) {
      if(errno == EINTR) continue;
      else error("poll");
    }
    current_size = nfds;
    for (i = 0; i < current_size; i++) {
      if(fds[i].revents == 0)
        continue;
      if(fds[i].revents != POLLIN) {
        printf("Error! revents = %d\n", fds[i].revents);
      }

      if(fds[i].fd == socket_fd) {

        do {
          if((connection_fd = accept(socket_fd, (struct sockaddr *)&client, &address_length)) < 0) {
            if(errno == EINTR) continue;
            if(errno != EWOULDBLOCK)
              perror("accept");
            break;
          }
          printf("New incoming connection - %d\n", connection_fd);
          if((flags = fcntl(connection_fd, F_GETFL, 0)) < 0)
            error("fcntl");
          if(fcntl(connection_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            error("fcntl");
          fds[nfds].fd = connection_fd;
          fds[nfds].events = POLLIN;
          nfds++;
        } while(connection_fd >= 0);

      } else {

        close_conn = 0;
        do {
          rc = recv(fds[i].fd, buffer, sizeof(buffer), 0);
          if(rc < 0) {
            if(errno == EINTR) continue;
            if(errno != EWOULDBLOCK) {
              perror("recv");
              close_conn = 1;
            }
            break;
          }

          if(rc == 0) {
            close_conn = 1;
            break;
          }

          buffer[rc] = '\0';

          parse_command(fds[i].fd, &close_conn);
          if(close_conn) break;
        } while(rc > 0);

        if(close_conn) {
          printf("Closing connection - %d\n", fds[i].fd);
          close(fds[i].fd);
          fds[i].fd = -1;
          compress_array = 1;
        }
      }
    }

    if(compress_array) {
      for (i = 0; i < nfds; i++) {
        if (fds[i].fd == -1) {
          for(j = i; j < nfds; j++) {
            fds[j].fd = fds[j+1].fd;
          }
          nfds--;
        }
      }
      compress_array = 0;
    }
  }
}

void close_control_socket() {
  int i;
  for (i = 0; i < nfds; i++) {
    if(fds[i].fd >= 0)
      close(fds[i].fd);
  }
}
