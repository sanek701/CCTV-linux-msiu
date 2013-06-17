#include "cameras.h"
#include "file_reader.h"

#define PORT_RANGE_START 4000

GstRTSPServer *server;
unsigned int ports = 0;

l1 *screens = NULL;
pthread_mutex_t screens_lock;

int screen_open_video_file(struct screen *screen);
static void init_single_camera_screen(struct screen *screen);
static void init_multiple_camera_screen(struct screen *screen);

int screen_init(struct screen *screen) {
  char gst_pipe[128], path[128];
  int port;
  unsigned int i;

  for(port=1, i=1; port <= 32; port+=1, i*=2) {
    if((ports & i) == 0) {
      ports |= i;
      break;
    }
  }

  screen->rtp_port = PORT_RANGE_START + port;

  printf("rtp_port: %d\n", screen->rtp_port);

  if(screen->type == REAL) {
    if(screen->ncams == 1)
      init_single_camera_screen(screen);
    else
      init_multiple_camera_screen(screen);

    for(i=0; i<screen->ncams; i++) {
      struct cam_consumer *consumer = (struct cam_consumer*)malloc(sizeof(struct cam_consumer));
      consumer->screen = screen;
      consumer->position = i;
      l1_insert(&screen->cams[i]->cam_consumers_list, &screen->cams[i]->consumers_lock, consumer);
    }
  } else {
    if(screen_open_video_file(screen) < 0) {
      fprintf(stderr, "open_video_file failed\n");
      ports -= 1 << port;
      return -1;
    }
  }

  sprintf(gst_pipe, "( filesrc location=/tmp/stream_%d.sdp ! sdpdemux name=dynpay0 )", screen->rtp_port);
  sprintf(path, "/stream_%d", screen->session_id);

  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  GstRTSPMediaMapping *mapping = gst_rtsp_server_get_media_mapping(server);
  gst_rtsp_media_factory_set_launch(factory, gst_pipe);
  gst_rtsp_media_mapping_add_factory(mapping, path, factory);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_object_unref(mapping);

  l1_insert(&screens, &screens_lock, screen);

  return 0;
}

int remove_screen_counsumers(void *value, void *arg) {
  struct cam_consumer *consumer = (struct cam_consumer *)value;
  struct screen *screen = (struct screen *)arg;

  if(consumer->screen == screen) {
    free(consumer);
    return 0;
  } else {
    return 1;
  }
}

void screen_destroy(struct screen *screen) {
  char path[128];
  if(screen->type == REAL) {
    for(int i=0; i < screen->ncams; i++) {
      l1_filter(&screen->cams[i]->cam_consumers_list, &screen->cams[i]->consumers_lock, &remove_screen_counsumers, screen);
    }
  } else {
    screen->io->active = 0;
  }

  printf("consumers clear\n");

  int port = screen->rtp_port - PORT_RANGE_START;
  ports -= 1 << port;

  sprintf(path, "/stream_%d", screen->session_id);
  GstRTSPMediaMapping *mapping = gst_rtsp_server_get_media_mapping(server);
  gst_rtsp_media_mapping_remove_factory(mapping, path);
  g_object_unref(mapping);

  free(screen->cams);
  free(screen);
}

static void create_frame() {
  AVFrame *picture = avcodec_alloc_frame();
  uint8_t *picture_buf;
  int size;
  size = avpicture_get_size(PIX_FMT_YUV420P, 1024, 768);
  picture_buf = (uint8_t *)malloc(size);
  avpicture_fill((AVPicture *)picture, picture_buf, PIX_FMT_YUV420P, 1024, 768);
}

static void create_sdp(struct screen *screen) {
  char buff[2048], fname[128];
  av_sdp_create(&screen->rtp_context, 1, buff, sizeof(buff));
  sprintf(fname, "/tmp/stream_%d.sdp", screen->rtp_port);
  FILE *f = fopen(fname, "w+");
  fprintf(f, "%s", buff);
  fflush(f);
  fclose(f);
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
  sprintf(rtp_context->filename, "rtp://127.0.0.1:%d", screen->rtp_port);

  if((ret = avio_open(&(rtp_context->pb), rtp_context->filename, AVIO_FLAG_WRITE)) < 0) {
    av_err_msg("avio_open", ret);
    return -1;
  }
  rtp_stream = avformat_new_stream(rtp_context, (AVCodec *)codec->codec);
  if(rtp_stream == NULL) {
    av_err_msg("avformat_new_stream", 0);
    return -1;
  }
  if((ret = avcodec_copy_context(rtp_stream->codec, (const AVCodecContext *)codec)) < 0) {
    av_err_msg("avcodec_copy_context", ret);
    return -1;
  }
  if((ret = avformat_write_header(rtp_context, NULL)) < 0) {
    av_err_msg("avformat_write_header", ret);
    return -1;
  }

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;
  return 0;
}

static void init_single_camera_screen(struct screen *screen) {
  struct camera *cam = screen->cams[0];

  init_rtp_stream(screen, cam->codec);
  screen->width = cam->codec->width;
  screen->height = cam->codec->height;

  create_sdp(screen);
}

static void init_multiple_camera_screen(struct screen *screen) {
  AVFormatContext *rtp_context;
  AVOutputFormat *rtp_fmt;
  AVStream *rtp_stream;
  AVCodecContext *c;
  AVCodec *codec;
  int ret;

  rtp_context = avformat_alloc_context();
  rtp_fmt = av_guess_format("rtp", NULL, NULL);

  codec = avcodec_find_encoder(CODEC_ID_H264);

  if(rtp_fmt == NULL)
    av_err_msg("av_guess_format", 0);
  rtp_context->oformat = rtp_fmt;
  sprintf(rtp_context->filename, "rtp://127.0.0.1:%d", screen->rtp_port);
  if((ret = avio_open(&(rtp_context->pb), rtp_context->filename, AVIO_FLAG_WRITE)) < 0)
    av_err_msg("avio_open", ret);
  rtp_stream = avformat_new_stream(rtp_context, codec);
  if(rtp_stream == NULL)
    av_err_msg("avformat_new_stream", 0);

  c = rtp_stream->codec;
  avcodec_get_context_defaults3(c, codec);

  c->codec_id = CODEC_ID_H264;
  c->bit_rate = 400000;
  c->width = 1024;
  c->height = 768;
  c->time_base.den = 25;
  c->time_base.num = 1;
  c->gop_size = 12;
  c->pix_fmt = PIX_FMT_YUV420P;

  avcodec_open2(c, codec, NULL);

  if((ret = avformat_write_header(rtp_context, NULL)) < 0)
    av_err_msg("avformat_write_header", ret);

  create_frame();

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;

  create_sdp(screen);
}

int screen_open_video_file(struct screen *screen) {
  int ext;
  struct camera *cam = screen->cams[0];
  AVFormatContext *s = avformat_alloc_context();
  AVStream *input_stream;

  if((ext = db_find_video_file(cam->id, s->filename, &screen->timestamp)) < 0) {
    fprintf(stderr, "db_find_video_file File not found\n");
    avformat_free_context(s);
    return -1;
  }
  
  if(ext == H264_FILE) {
    input_stream = init_h264_read_ctx(s, cam);
    h264_seek_file(s, input_stream, screen->timestamp);
  } else {
    int video_stream_index = open_input(s, NULL);
    input_stream = s->streams[video_stream_index];
    av_seek_frame(s, -1, screen->timestamp, AVSEEK_FLAG_ANY);
  }

  if(screen->rtp_context == NULL)
    init_rtp_stream(screen, input_stream->codec);

  struct in_out_cpy *io = (struct in_out_cpy*)malloc(sizeof(struct in_out_cpy));
  io->in_ctx = s;
  io->out_ctx = screen->rtp_context;
  io->in_stream = input_stream;
  io->out_stream = screen->rtp_stream;
  io->rate_emu = 1;
  io->active = 1;

  pthread_t copy_input_to_output_thread;
  if(pthread_create(&copy_input_to_output_thread, NULL, copy_input_to_output, io) < 0)
    error("pthread_create");
  if(pthread_detach(copy_input_to_output_thread) < 0)
    error("pthread_detach");

  screen->width = cam->codec->width;
  screen->height = cam->codec->height;
  screen->io = io;

  create_sdp(screen);
  return 0;
}

int filter_timeout_screen(void *value, void *arg) {
  struct screen *screen = (struct screen *)value;
  if(time(NULL) - screen->last_activity > 10) {
    printf("Killing session_id: %d\n", screen->session_id);
    screen_destroy(screen);
    return 0;
  } else {
    return 1;
  }
}

static gboolean timeout(GstRTSPServer *server, gboolean ignored) {
  GstRTSPSessionPool *pool;
  pool = gst_rtsp_server_get_session_pool(server);
  gst_rtsp_session_pool_cleanup(pool);
  g_object_unref(pool);

  //l1_filter(&screens, &screens_lock, &filter_timeout_screen, NULL);
  return TRUE;
}

void *start_rtsp_server(void* ptr) {
  GMainLoop *loop;

  loop = g_main_loop_new(NULL, FALSE);
  server = gst_rtsp_server_new();

  gst_rtsp_server_attach(server, NULL);
  g_timeout_add_seconds(3, (GSourceFunc)timeout, server);
  fprintf(stderr, "Starting gst-rtsp-server\n");
  g_main_loop_run(loop);
  return NULL;
}
