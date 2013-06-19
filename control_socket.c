#include <fcntl.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/un.h>
#include "cameras.h"
#include "screen.h"
#include "json.h"

#define MAX_CLIENTS 10
#define BUF_SIZE 1024

extern struct camera *cameras;
extern int n_cameras;
extern int should_terminate;
extern char *store_dir;
extern l1 *screens;
extern pthread_mutex_t screens_lock;

static struct sockaddr_un address, client;
static int socket_fd, connection_fd;
static socklen_t address_length;
static struct pollfd fds[MAX_CLIENTS + 1];
static int nfds = 1, rc;
static int timeout = 5000;
static int reads[MAX_CLIENTS];
static char buffer[BUF_SIZE * MAX_CLIENTS];
static unsigned int session_id = 0;

static time_t last_disk_space_check = 0;

static void check_disk_space() {
  struct statvfs statvfs_buf;
  unsigned long long avail;
  if(statvfs(store_dir, &statvfs_buf) < 0) {
    perror("statvfs");
    return;
  }
  
  while((avail = (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_bavail) < (unsigned long long)1024 * 1024 * 1024) {
    if(!db_unlink_oldest_file()) {
      fprintf(stderr, "Few disk space left, but nothing to delete.\n");
      break;
    }
  }
}

static struct camera** get_cams_by_ids(int *cam_ids, int ncams) {
  int i, j;
  struct camera **cams = (struct camera **)malloc(ncams * sizeof(struct camera *));

  for(i=0; i<ncams; i++) {
    cams[i] = NULL;
    for(j=0; j<n_cameras; j++) {
      if(cameras[j].id == cam_ids[i]) {
        cams[i] = &cameras[j];
        break;
      }
    }
  }
  return cams;
}

int find_screen_func(void *value, void *arg) {
  struct screen *screen = value;
  int session_id = *((int *)arg);
  if(screen->session_id == session_id)
    return 1;
  else
    return 0;
}

static void snd(int fd, char *buffer, int *close_conn) {
  int rc = send(fd, buffer, strlen(buffer), 0);
  if(rc < 0) {
    perror("send");
    *close_conn = 1;
  }
}

static void parse_command(char *buf, int client_fd, int *close_conn) {
  json_settings settings;
  char error[256];
  int i, j;

  int type = 0;
  int ncams = 0;
  int *cam_ids = NULL;
  int ssid = 0;
  int tmpl_size = 1;
  time_t timestamp = 0;

  struct screen *screen = NULL;
  struct camera *cam;
  struct statvfs statvfs_buf;

  memset(&settings, 0, sizeof (json_settings));
  json_value *json = json_parse_ex(&settings, buf, strlen(buf), error);

  if(json == NULL) {
    fprintf(stderr, "Json error: %s\n", error);
    fprintf(stderr, "buffer: %s\n", buf);
  }

  for(i=0; i < json->u.object.length; i++) {
    char *key = json->u.object.values[i].name;
    json_value *value = json->u.object.values[i].value;

    if(strcmp(key, "type") == 0) {
      char *string_value = value->u.string.ptr;
      if(strcmp(string_value, "archive") == 0)
        type = ARCHIVE;
      else if(strcmp(string_value, "real") == 0)
        type = REAL;
      else if(strcmp(string_value, "stats") == 0)
        type = STATS;
      else if(strcmp(string_value, "cam_info") == 0)
        type = CAM_INFO;
      else if(strcmp(string_value, "screen_ping") == 0)
        type = SCREEN_PING;
    } else if (strcmp(key, "cam_ids") == 0) {
      ncams = value->u.array.length;
      cam_ids = (int*)malloc(ncams * sizeof(int));
      for(j = 0; j < ncams; j++) {
        json_value *arr_elem = value->u.array.values[j];
        cam_ids[j] = arr_elem->u.integer;
      }
    } else if (strcmp(key, "timestamp") == 0) {
      timestamp = value->u.integer;
    } else if (strcmp(key, "session_id") == 0) {
      ssid = value->u.integer;
    } else if (strcmp(key, "template") == 0) {
      tmpl_size = value->u.integer;
    }
  }
  json_value_free(json);

  switch(type) {
    case ARCHIVE:
    case REAL:
      if(ssid == 0) {
        session_id += 1;
        ssid = session_id;

        screen = (struct screen *)malloc(sizeof(struct screen));
        screen->type = type;
        screen->ncams = ncams;
        screen->session_id = ssid;
        screen->tmpl_size = tmpl_size;
        screen->cams = get_cams_by_ids(cam_ids, ncams);
        screen->timestamp = timestamp;\
        screen->rtp_context = NULL;
        screen->last_activity = time(NULL);
        screen->active = 1;
        screen->io = NULL;

        if(screen_init(screen) < 0) {
          sprintf(buf, "{ \"error\": \"Screen initialization failed\" }\n");
        } else {
          sprintf(buf, "{ \"session_id\": %d, \"width\": %d, \"height\": %d }\n",
            screen->session_id, screen->rtp_stream->codec->width,  screen->rtp_stream->codec->height);
        }
      } else {
        screen = (struct screen *) l1_find(&screens, &screens_lock, &find_screen_func, &ssid);
        if(screen == NULL) {
          sprintf(buf, "{ \"error\": \"Screen not found\" }\n");
        } else {
          if(type != ARCHIVE || ncams != 1 || cam_ids == NULL || cam_ids[0] != screen->cams[0]->id) {
            sprintf(buf, "{ \"error\": \"Bad request\" }\n");
          } else {
            screen->timestamp = timestamp;
            if(screen_open_video_file(screen) < 0) {
              sprintf(buf, "{ \"error\": \"Moment not found\"}\n");
            } else {
              sprintf(buf, "{ \"session_id\": %d, \"width\": %d, \"height\": %d }\n",
                screen->session_id, screen->rtp_stream->codec->width,  screen->rtp_stream->codec->height);
            }
          }
        }
      }
      break;
    case STATS:
      if(statvfs(store_dir, &statvfs_buf) < 0) {
        perror("statvfs");
        sprintf(buf, "{ \"error\": \"statvfs failed\" }\n");
      } else {
        sprintf(buf, "{ \"free_space\": %llu, \"tolal_space\": %llu, \"ncams\": %d }\n",
          (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_bavail,
          (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_blocks, n_cameras);
      }
      break;
    case CAM_INFO:
      cam = get_cams_by_ids(cam_ids, ncams)[0];
      if(cam == NULL) {
        sprintf(buf, "{ \"error\": \"Camera not found\" }\n");
      } else {
        sprintf(buf, "{ \"codec\": \"%s\", \"width\": %d, \"height\": %d, \"fps_den\": %d, \"fps_num\": %d }\n",
          cam->codec->codec->long_name, cam->codec->width, cam->codec->height,
          cam->input_stream->r_frame_rate.den, cam->input_stream->r_frame_rate.num);
      }
      break;
    case SCREEN_PING:
      screen = (struct screen *) l1_find(&screens, &screens_lock, &find_screen_func, &ssid);
      if(screen == NULL) {
        sprintf(buf, "{ \"error\": \"Screen not found\" }\n");
      } else {
        screen->last_activity = time(NULL);
        sprintf(buf, "{ \"ok\": \"ok\" }\n");
      }
      break;
    default:
      sprintf(buf, "{ \"error\": \"Command not found\" }\n");
      break;
  }

  snd(client_fd, buf, close_conn);

  if(cam_ids != NULL)
    free(cam_ids);
}

void control_socket_init() {
  int flags, on = 1;
  memset(&address, 0, address_length);
  memset(&fds, 0 , sizeof(fds));
  memset(&reads, 0 , sizeof(int) * MAX_CLIENTS);
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
  int i,j,k,ns,rd,flags;
  char *buf;

  address_length = sizeof(struct sockaddr_un);
  fds[0].fd = socket_fd;
  fds[0].events = POLLIN;

  while(!should_terminate) {
    if(time(NULL) - last_disk_space_check > 60) {
      check_disk_space();
      last_disk_space_check = time(NULL);
    }

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
          rd = reads[i-1];
          buf = buffer + (i-1) * BUF_SIZE;
          rc = recv(fds[i].fd, buf + rd, BUF_SIZE - rd, 0);
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

          ns = 0;

          if(rd > 0 && buf[rd-1] == '\n')
            ns += 1;

          reads[i-1] += rc;

          for(j = rd, k = rc; k>0; k--, j++) {
            if(buf[j] == '\n')
              ns += 1;
            else
              ns = 0;
            if(ns == 2) {
              buf[rd + rc] = '\0';
              reads[i-1] = 0;
              parse_command(buf, fds[i].fd, &close_conn);
              break;
            }
          }
          
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

void control_socket_close() {
  int i;
  for (i = 0; i < nfds; i++) {
    if(fds[i].fd >= 0)
      close(fds[i].fd);
  }
}
