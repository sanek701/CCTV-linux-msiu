#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/un.h>
#include "camera.h"
#include "event_loop.h"
#include "screen.h"
#include "json.h"

#define MAX_CLIENTS 10
#define BUF_SIZE 1024

extern struct camera *cameras;
extern int n_cameras;
extern int should_terminate;
extern int info_server_fd;
extern char *store_dir;
extern l1 *screens;
extern pthread_mutex_t screens_lock;

void info_server_start() {
  int socket_fd, flags, on = 1;
  struct sockaddr_un address;
  int address_length;

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

  info_server_fd = socket_fd;
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

void parse_info_request(struct client *client, char *buf) {
  json_settings settings;
  char error[256];
  int i, j;

  int type = 0;
  int ncams = 0;
  int *cam_ids = NULL;
  int tmpl_size = 1;
  time_t timestamp = 0;
  char *screen_id = NULL;

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
    } else if (strcmp(key, "cam_ids") == 0) {
      ncams = value->u.array.length;
      cam_ids = (int*)malloc(ncams * sizeof(int));
      for(j = 0; j < ncams; j++) {
        json_value *arr_elem = value->u.array.values[j];
        cam_ids[j] = arr_elem->u.integer;
      }
    } else if (strcmp(key, "timestamp") == 0) {
      timestamp = value->u.integer;
    } else if (strcmp(key, "screen_id") == 0) {
      screen_id = (char*)malloc(strlen(value->u.string.ptr)+1);
      strcpy(screen_id, value->u.string.ptr);
    } else if (strcmp(key, "template") == 0) {
      tmpl_size = value->u.integer;
    }
  }
  json_value_free(json);

  switch(type) {
    case ARCHIVE:
    case REAL:
      if(screen_id == NULL) {
        screen = (struct screen *)malloc(sizeof(struct screen));
        screen->type = type;
        screen->ncams = ncams;
        screen->screen_id = random_string(8);
        screen->tmpl_size = tmpl_size;
        screen->cams = get_cams_by_ids(cam_ids, ncams);
        screen->timestamp = timestamp;\
        screen->rtp_context = NULL;
        screen->active = 0;
        screen->io = NULL;

        if(screen_init(screen) < 0) {
          sprintf(buf, "{ \"error\": \"Screen initialization failed\" }\n");
        } else {
          sprintf(buf, "{ \"screen_id\": \"%s\", \"width\": %d, \"height\": %d }\n",
            screen->screen_id, screen->rtp_stream->codec->width,  screen->rtp_stream->codec->height);
        }
      } else {
        screen = (struct screen *) l1_find(&screens, &screens_lock, &screen_find_func, screen_id);
        if(screen == NULL) {
          sprintf(buf, "{ \"error\": \"Screen not found\" }\n");
        } else {
          if(type != ARCHIVE || ncams != 1 || cam_ids == NULL || cam_ids[0] != screen->cams[0]->id) {
            sprintf(buf, "{ \"error\": \"Bad request\" }\n");
          } else {
            screen->timestamp = timestamp;
            if(screen_open_video_file(screen) < 0) {
              sprintf(buf, "{ \"error\": \"Moment not found\" }\n");
            } else {
              sprintf(buf, "{ \"screen_id\": \"%s\", \"width\": %d, \"height\": %d }\n",
                screen->screen_id, screen->rtp_stream->codec->width, screen->rtp_stream->codec->height);
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
    default:
      sprintf(buf, "{ \"error\": \"Command not found\" }\n");
      break;
  }

  snd(client, buf);

  if(cam_ids != NULL)
    free(cam_ids);
  if(screen_id != NULL)
    free(screen_id);
}
