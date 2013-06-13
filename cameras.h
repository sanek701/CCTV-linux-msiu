#include <pthread.h>
#include <libavformat/avformat.h>
#include <cv.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include "l1-list.h"
#include "database.h"

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
  int active;
};

struct screen {
    int type;
    int ncams;
    struct camera **cams;
    unsigned int session_id;
    time_t timestamp;
    uint8_t **frames;
    int left_frames;
    pthread_mutex_t counter_lock;
    GstRTSPMediaFactory *factory;
    AVFormatContext *rtp_context;
    AVStream *rtp_stream;
    int rtp_port;
    int width;
    int height;
    struct in_out_cpy *io;
};

struct cam_consumer {
    struct screen *screen;
    int position;
};

void error(const char *msg);
void av_crit(char *msg, int errnum);
void *recorder_thread(void *ptr);
int init_screen(struct screen *screen);
void *start_rtsp_server(void* ptr);
