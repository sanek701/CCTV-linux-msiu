#include "cameras.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

extern struct camera *cameras;
extern int n_cameras;
extern int should_terminate;
void error(const char *msg);
void init_screen(struct screen *screen);

struct sockaddr_un address, client;
int socket_fd, connection_fd;
socklen_t address_length;
struct pollfd fds[10];
int nfds = 1, on = 1, rc;
int timeout = 5000;
char buffer[2048];

struct camera** get_cams_by_ids(int *cam_ids, int ncams) {
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

void parse_command() {
  json_settings settings;
  char error[256];
  int i;

  int type = 0;
  int ncams = 0;
  int *cam_ids = NULL;
  int file_id = 0;
  int64_t position = 0;
  char *session_id;

  struct screen *screen = NULL;

  memset(&settings, 0, sizeof (json_settings));
  json_value *json = json_parse(buffer, strlen(buffer));

  if(json == NULL) {
    fprintf(stderr, "Json error: %s\n", error);
  }

  for(i=0; i < json->u.object.length; i++) {
    char *key = json->u.object.values[i].name;
    json_value *value = json->u.object.values[i].value;

    if(strcmp(key, "type") == 0) {
      char *string_value = value->u.string.ptr;
      if(strcmp(string_value, "archive")==0)
        type = ARCHIVE;
      else type = REAL;
    } else if (strcmp(key, "cam_ids") == 0) {
      ncams = value->u.array.length;
      cam_ids = (int*)malloc(ncams * sizeof(int));
      for(i = 0; i < ncams; i++) {
        json_value *arr_elem = value->u.array.values[i];
        cam_ids[i] = arr_elem->u.integer;
      }
    } else if (strcmp(key, "file_id") == 0) {
      file_id = value->u.integer;
    } else if (strcmp(key, "position") == 0) {
      position = value->u.integer;
    } else if (strcmp(key, "session_id") == 0) {
      session_id = (char*)malloc(value->u.string.length);
      strcpy(session_id, value->u.string.ptr);
    }
  }
  json_value_free(json);

  printf("session_id: %s, type: %d, ncams: %d, file_id: %d, position: %lld\n", session_id, type, ncams, file_id, position);

  screen = (struct screen *)malloc(sizeof(struct screen));
  screen->ncams = ncams;
  screen->cams = get_cams_by_ids(cam_ids, ncams);

  init_screen(screen);
}

void initialize_control_socket() {
  memset(&address, 0, address_length);
  memset(&fds, 0 , sizeof(fds));
  address.sun_family = AF_UNIX;
  strcpy(address.sun_path, "/tmp/videoserver");
  unlink(address.sun_path);
  address_length = strlen(address.sun_path) + sizeof(address.sun_family);

  if((socket_fd = socket(PF_UNIX, SOCK_STREAM, 0)) < 0)
    error("socket");
  if(setsockopt(socket_fd, SOL_SOCKET,  SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    error("setsockopt");
  if(ioctl(socket_fd, FIONBIO, (char *)&on) < 0)
    error("ioctl");
  if(bind(socket_fd, (struct sockaddr *)&address, address_length) < 0)
    error("bind");
  if(listen(socket_fd, 5) < 0)
    error("listen"); 
}

void control_socket_loop() {
  int current_size, close_conn, compress_array = 0;
  int i,j;

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
            if(errno != EWOULDBLOCK)
              perror("accept");
            break;
          }
          printf("New incoming connection - %d\n", connection_fd);
          fds[nfds].fd = connection_fd;
          fds[nfds].events = POLLIN;
          nfds++;
        } while(connection_fd >= 0);

      } else {

        close_conn = 0;
        do {
          rc = recv(fds[i].fd, buffer, sizeof(buffer), 0);
          if(rc < 0) {
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

          parse_command();

          rc = send(fds[i].fd, "ok\n", 4, 0);
          if(rc < 0) {
            perror("send");
            close_conn = 1;
            break;
          }
        } while(TRUE);

        if(close_conn) {
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
