#include "cameras.h"
#include "file_reader.h"
#include <libswscale/swscale.h>

l1 *screens = NULL;
pthread_mutex_t screens_lock;

int screen_open_video_file(struct screen *screen);
void* multiple_cameras_thread(void * ptr);
static int init_single_camera_screen(struct screen *screen);
static int init_multiple_camera_screen(struct screen *screen);

int screen_init(struct screen *screen) {
  int ret;
  unsigned int i;

  if(screen->type == REAL) {
    if(screen->tmpl_size == 1)
      ret = init_single_camera_screen(screen);
    else
      ret = init_multiple_camera_screen(screen);

    if(ret < 0) {
      fprintf(stderr, "screen initialization failed\n");
      return -1;
    }

    for(i=0; i < screen->ncams; i++) {
      struct camera *cam = screen->cams[i];
      struct cam_consumer *consumer = (struct cam_consumer*)malloc(sizeof(struct cam_consumer));
      consumer->screen = screen;
      if(screen->tmpl_size > 1) {
        consumer->position = i;
        avpicture_alloc(&consumer->picture, PIX_FMT_YUV420P, SCREEN_WIDTH/screen->tmpl_size, SCREEN_HEIGHT/screen->tmpl_size);
        consumer->sws_context = sws_getContext(
          cam->codec->width, cam->codec->height, cam->codec->pix_fmt,
          SCREEN_WIDTH/screen->tmpl_size, SCREEN_HEIGHT/screen->tmpl_size, PIX_FMT_YUV420P,
          SWS_BILINEAR, NULL, NULL, NULL);
      } else {
        consumer->sws_context = NULL;
      }
      l1_insert(&cam->cam_consumers_list, &cam->consumers_lock, consumer);
    }
  } else {
    if(screen_open_video_file(screen) < 0) {
      fprintf(stderr, "open_video_file failed\n");
      return -1;
    }
  }

  l1_insert(&screens, &screens_lock, screen);

  return 0;
}

int remove_screen_counsumers(void *value, void *arg) {
  struct cam_consumer *consumer = (struct cam_consumer *)value;
  struct screen *screen = (struct screen *)arg;

  if(consumer->screen == screen) {
    if(screen->tmpl_size > 1)
      avpicture_free(&consumer->picture);
    if(consumer->sws_context != NULL)
      sws_freeContext(consumer->sws_context);
    free(consumer);
    return 0;
  } else {
    return 1;
  }
}

void screen_destroy(struct screen *screen) {
  screen->active = 0;

  if(screen->type == REAL) {
    for(int i=0; i < screen->ncams; i++) {
      l1_filter(&screen->cams[i]->cam_consumers_list, &screen->cams[i]->consumers_lock, &remove_screen_counsumers, screen);
    }
  } else {
    screen->io->screen = NULL;
    screen->io->active = 0;
  }

  if(screen->tmpl_size > 1) {
    pthread_join(screen->worker_thread, NULL);
    avpicture_free(&screen->combined_picture);
    pthread_mutex_destroy(&screen->combined_picture_lock);
  }

  free(screen->screen_id);
  free(screen->cams);
  free(screen);
}

void screen_remove(struct screen *screen) {
  l1_remove(&screens, &screens_lock, screen);
  screen_destroy(screen);
}

static char* create_sdp(struct screen *screen) {
  char buff[2048];
  if(av_sdp_create(&screen->rtp_context, 1, buff, sizeof(buff)) < 0)
    return NULL;
  char *sdp = (char*)malloc(strlen(buff)+1);
  strcpy(sdp, buff);
  return sdp;
}

static int init_rtp_stream(struct screen *screen, AVCodecContext *codec) {
  AVFormatContext *rtp_context;
  AVOutputFormat *rtp_fmt;
  AVStream *rtp_stream;
  int ret;

  rtp_context = avformat_alloc_context();
  rtp_fmt = av_guess_format("rtp", NULL, NULL);
  if(rtp_fmt == NULL) {
    av_err_msg("av_guess_format", 0);
    return -1;
  }
  rtp_context->oformat = rtp_fmt;

  strcpy(rtp_context->filename, "rtp://0.0.0.0");

  rtp_stream = avformat_new_stream(rtp_context, (AVCodec *)codec->codec);
  if(rtp_stream == NULL) {
    avformat_free_context(rtp_context);
    av_err_msg("avformat_new_stream", 0);
    return -1;
  }
  if((ret = avcodec_copy_context(rtp_stream->codec, (const AVCodecContext *)codec)) < 0) {
    avformat_free_context(rtp_context);
    av_err_msg("avcodec_copy_context", ret);
    return -1;
  }
  if((ret = avformat_write_header(rtp_context, NULL)) < 0) {
    avformat_free_context(rtp_context);
    av_err_msg("avformat_write_header", ret);
    return -1;
  }

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;
  return 0;
}

static int init_single_camera_screen(struct screen *screen) {
  struct camera *cam = screen->cams[0];

  if(init_rtp_stream(screen, cam->codec) < 0)
    return -1;

  return 0;
}

static int init_multiple_camera_screen(struct screen *screen) {
  AVFormatContext *rtp_context;
  AVOutputFormat *rtp_fmt;
  AVStream *rtp_stream;
  AVCodecContext *c;
  AVCodec *codec;
  int ret;

  rtp_context = avformat_alloc_context();
  rtp_fmt = av_guess_format("rtp", NULL, NULL);

  codec = avcodec_find_encoder(CODEC_ID_H264);

  if(rtp_fmt == NULL) {
    avformat_free_context(rtp_context);
    av_err_msg("av_guess_format", 0);
    return -1;
  }
  rtp_context->oformat = rtp_fmt;

  strcpy(rtp_context->filename, "rtp://0.0.0.0");

  rtp_stream = avformat_new_stream(rtp_context, codec);
  if(rtp_stream == NULL) {
    avformat_free_context(rtp_context);
    av_err_msg("avformat_new_stream", 0);
    return -1;
  }

  c = rtp_stream->codec;
  avcodec_get_context_defaults3(c, codec);

  c->codec_id = CODEC_ID_H264;
  c->width = SCREEN_WIDTH;
  c->height = SCREEN_HEIGHT;
  c->pix_fmt = PIX_FMT_YUV420P;
  c->time_base.den = 3;
  c->time_base.num = 1;
  c->gop_size = 12;

  if((ret = avcodec_open2(c, codec, NULL)) < 0) {
    avformat_free_context(rtp_context);
    av_err_msg("avcodec_open2", 0);
    return -1;
  }

  if((ret = avpicture_alloc(&screen->combined_picture, c->pix_fmt, c->width, c->height)) < 0) {
    avformat_free_context(rtp_context);
    av_err_msg("avpicture_alloc", ret);
    return -1;
  }

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;

  if(create_sdp(screen) < 0)
    return -1;

  if(pthread_mutex_init(&screen->combined_picture_lock, NULL) < 0) {
    fprintf(stderr, "pthread_mutex_init failed\n");
    exit(EXIT_FAILURE);
  }

  return 0;
}

int screen_open_video_file(struct screen *screen) {
  int ext, ret;
  int video_stream_index;
  struct camera *cam = screen->cams[0];
  AVFormatContext *s = avformat_alloc_context();
  AVStream *input_stream;

  if((ext = db_find_video_file(cam->id, s->filename, &screen->timestamp)) < 0) {
    fprintf(stderr, "db_find_video_file: File not found\n");
    avformat_free_context(s);
    return -1;
  }

  if(ext == H264_FILE) {
    input_stream = init_h264_read_ctx(s, cam);
    if(input_stream == NULL) {
      avformat_free_context(s);
      return -1;
    }
    if(h264_seek_file(s, input_stream, screen->timestamp) < 0) {
      avformat_free_context(s);
      return -1;
    }
  } else {
    if((video_stream_index = open_input(s, NULL)) < 0) {
      fprintf(stderr, "open_input failed");
      avformat_close_input(&s);
      return -1;
    }
    input_stream = s->streams[video_stream_index];
    int64_t seek_target = av_rescale_q((int64_t)screen->timestamp * AV_TIME_BASE, AV_TIME_BASE_Q, input_stream->time_base);

    if((ret = av_seek_frame(s, video_stream_index, seek_target, AVSEEK_FLAG_ANY)) < 0) {
      av_err_msg("av_seek_frame", ret);
      avformat_close_input(&s);
      return -1;
    }
  }

  if(screen->rtp_context == NULL)
    init_rtp_stream(screen, input_stream->codec);

  struct in_out_cpy *io = (struct in_out_cpy*)malloc(sizeof(struct in_out_cpy));
  io->in_ctx = s;
  io->out_ctx = screen->rtp_context;
  io->in_stream = input_stream;
  io->out_stream = screen->rtp_stream;
  io->prev_io = screen->io;
  io->close_output = 1;
  io->rate_emu = 1;
  io->active = 1;
  io->screen = screen;

  screen->io = io;

  return 0;
}
