#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

#include <libavdevice/avdevice.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/timestamp.h>
#include <libswscale/swscale.h>

#include <cv.h>
#include <highgui.h>

#include <libpq-fe.h>
#include <libconfig.h>

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "l1-list.h"
#include "json.h"

#define ARCHIVE 1
#define REAL 2

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

struct screen {
    int ncams;
    struct camera **cams;
    uint8_t **frames;
    int left_frames;
    pthread_mutex_t counter_lock;
    GstRTSPMediaFactory *factory;
    AVFormatContext *rtp_context;
    AVStream *rtp_stream;
};

struct cam_consumer {
    struct screen *screen;
    int position;
};

void crit(char *msg, int errnum);
