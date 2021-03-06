#include <pthread.h>
#include <libavformat/avformat.h>
#include <cv.h>
#include "l1-list.h"
#include "database.h"

#define PORT_RANGE_START 4000
#define SCREEN_WIDTH 1080
#define SCREEN_HEIGHT 810

#define ARCHIVE 1
#define REAL 2
#define STATS 3
#define CAM_INFO 4

#define MP4_FILE 100
#define H264_FILE 101

struct camera {
  int id;
  char *url;
  char *name;
  int analize_frames;
  int threshold;
  int motion_delay;
  int active;
  
  AVFormatContext *context;
  AVCodecContext  *codec;
  AVStream        *input_stream;
  AVFormatContext *output_context;
  AVStream        *output_stream;
  AVIOInterruptCB int_cb;
  int video_stream_index;

  int file_id;
  time_t file_started_at;
  time_t last_io;
  time_t last_screenshot;

  l1* cam_consumers_list;
  pthread_mutex_t consumers_lock;
};

struct motion_detection {
  struct camera *cam;
  struct SwsContext *img_convert_ctx;
  IplImage *prev;
  IplImage *cur;
  IplImage *silh;
  uint8_t* buffer;
};

struct in_out_cpy {
  AVFormatContext *in_ctx;
  AVFormatContext *out_ctx;
  AVStream *in_stream;
  AVStream *out_stream;
  int close_output;
  int active;
  int rate_emu;
  int file_id;
  int frame;
  int64_t start_time;
  struct in_out_cpy *prev_io;
  pthread_mutex_t io_lock;
  struct screen *screen;
};

void  error(const char *msg);
void  av_err_msg(char *msg, int errnum);
char* random_string(int len);
void* recorder_thread(void *ptr);
void* start_h264_to_mp4_service(void *ptr);
AVStream* init_h264_read_ctx(AVFormatContext *s, struct camera *cam);
void rtsp_server_start();
void info_server_start();
