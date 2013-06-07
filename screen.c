#include "cameras.h"
#include <unistd.h>

GstRTSPServer *server;
int ports = 0;

static void open_video_file(struct screen *screen);
static void init_single_camera_screen(struct screen *screen);
static void init_multiple_camera_screen(struct screen *screen);

void init_screen(struct screen *screen) {
  char gst_pipe[1024], path[256];
  int i, port;

  for(port=1, i=1; i<=65536; port+=1, i*=2) {
    if((ports & i) == 0) {
      ports |= i;
      break;
    }
  }

  screen->rtp_port = 4000 + port;

  printf("rtp_port: %d\n", screen->rtp_port);

  sprintf(gst_pipe, "( udpsrc port=%d ! application/x-rtp,payload=96 ! rtph264depay ! rtph264pay name=pay0 pt=96 )", screen->rtp_port);
  sprintf(path, "/stream_%d", screen->session_id);

  GstRTSPMediaFactory *factory = gst_rtsp_media_factory_new();
  GstRTSPMediaMapping *mapping = gst_rtsp_server_get_media_mapping(server);
  gst_rtsp_media_factory_set_launch(factory, gst_pipe);
  gst_rtsp_media_mapping_add_factory(mapping, path, factory);
  gst_rtsp_media_factory_set_shared(factory, TRUE);
  g_object_unref(mapping);

  if(screen->type == REAL) {
    if(screen->ncams == 1)
      init_single_camera_screen(screen);
    else
      init_multiple_camera_screen(screen);

    for(i=0; i<screen->ncams; i++) {
      struct cam_consumer *consumer = (struct cam_consumer*)malloc(sizeof(struct cam_consumer));
      consumer->screen = screen;
      consumer->position = i;
      l1_insert(&screen->cams[i]->cam_consumers_list, consumer);
    }
  } else {
    open_video_file(screen);
  }
}

static void init_single_camera_screen(struct screen *screen) {
  AVFormatContext *rtp_context;
  AVOutputFormat *rtp_fmt;
  AVStream *rtp_stream;
  int ret;

  struct camera *cam = screen->cams[0];

  rtp_context = avformat_alloc_context();
  rtp_fmt = av_guess_format("rtp", NULL, NULL);
  if(rtp_fmt == NULL)
    av_crit("av_guess_format", 0);
  rtp_context->oformat = rtp_fmt;
  sprintf(rtp_context->filename, "rtp://127.0.0.1:%d", screen->rtp_port);
  if((ret = avio_open(&(rtp_context->pb), rtp_context->filename, AVIO_FLAG_WRITE)) < 0)
    av_crit("avio_open", ret);
  rtp_stream = avformat_new_stream(rtp_context, (AVCodec *)cam->codec->codec);
  if(rtp_stream == NULL)
    av_crit("avformat_new_stream", 0);
  if((ret = avcodec_copy_context(rtp_stream->codec, (const AVCodecContext *)cam->codec)) < 0)
    av_crit("avcodec_copy_context", ret);
  if((ret = avformat_write_header(rtp_context, NULL)) < 0)
    av_crit("avformat_write_header", ret);

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;
  screen->width = cam->codec->width;
  screen->height = cam->codec->height;
}

static void create_frame() {
  AVFrame *picture = avcodec_alloc_frame();
  uint8_t *picture_buf;
  int size;
  size = avpicture_get_size(PIX_FMT_YUV420P, 1024, 768);
  picture_buf = (uint8_t *)malloc(size);
  avpicture_fill((AVPicture *)picture, picture_buf, PIX_FMT_YUV420P, 1024, 768);
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
    av_crit("av_guess_format", 0);
  rtp_context->oformat = rtp_fmt;
  sprintf(rtp_context->filename, "rtp://127.0.0.1:%d", screen->rtp_port);
  if((ret = avio_open(&(rtp_context->pb), rtp_context->filename, AVIO_FLAG_WRITE)) < 0)
    av_crit("avio_open", ret);
  rtp_stream = avformat_new_stream(rtp_context, codec);
  if(rtp_stream == NULL)
    av_crit("avformat_new_stream", 0);

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
    av_crit("avformat_write_header", ret);

  create_frame();

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;
}

static void* copy_input_to_output(void *ptr) {
  struct in_out_cpy *io = (struct in_out_cpy *)ptr;
  AVPacket packet;

  av_init_packet(&packet);
  while(av_read_frame(io->in_ctx, &packet) >= 0) {
    packet.stream_index = io->out_stream->id;
    av_write_frame(io->out_ctx, &packet);

    av_free_packet(&packet);
    av_init_packet(&packet);

    if(!io->active)
      break;
  }

  //avcodec_close(io->out_stream->codec);
  //avcodec_close(io->in_ctx->streams[io->in_stream_index]->codec);
  avio_close(io->out_ctx->pb);
  avformat_close_input(&io->in_ctx);
  avformat_free_context(io->out_ctx);
  free(io);
  return NULL;
}

static void open_video_file(struct screen *screen) {
  int ret;
  struct camera *cam = screen->cams[0];
  AVFormatContext *s = avformat_alloc_context();
  find_video_file(cam->id, s->filename, &screen->timestamp);

  if((ret = avformat_open_input(&s, s->filename, NULL, NULL)) < 0)
    av_crit("avformat_open_input", ret);
  if((ret = avformat_find_stream_info(s, NULL)) < 0)
    av_crit("avformat_find_stream_info", ret);
  int video_stream_index = av_find_best_stream(s, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if(video_stream_index < 0)
    av_crit("av_find_best_stream", video_stream_index);

  AVStream *input_stream = s->streams[video_stream_index];
  AVCodecContext *codec = input_stream->codec;

  input_stream->time_base.den = codec->time_base.den = cam->input_stream->codec->time_base.den;
  input_stream->time_base.num = codec->time_base.num = cam->input_stream->codec->time_base.num;

  if((ret = av_seek_frame(s, video_stream_index, av_rescale_q((int64_t)screen->timestamp, AV_TIME_BASE_Q, input_stream->time_base), 0)) < 0)
    av_crit("av_seek_frame", ret);


  AVFormatContext *rtp_context;
  AVOutputFormat *rtp_fmt;
  AVStream *rtp_stream;

  rtp_context = avformat_alloc_context();
  rtp_fmt = av_guess_format("rtp", NULL, NULL);
  if(rtp_fmt == NULL)
    av_crit("av_guess_format", 0);
  rtp_context->oformat = rtp_fmt;
  sprintf(rtp_context->filename, "rtp://127.0.0.1:%d", screen->rtp_port);
  if((ret = avio_open(&(rtp_context->pb), rtp_context->filename, AVIO_FLAG_WRITE)) < 0)
    av_crit("avio_open", ret);
  rtp_stream = avformat_new_stream(rtp_context, (AVCodec *)codec->codec);
  if(rtp_stream == NULL)
    av_crit("avformat_new_stream", 0);
  if((ret = avcodec_copy_context(rtp_stream->codec, (const AVCodecContext *)codec)) < 0)
    av_crit("avcodec_copy_context", ret);
  if((ret = avformat_write_header(rtp_context, NULL)) < 0)
    av_crit("avformat_write_header", ret);

  struct in_out_cpy *io = (struct in_out_cpy*)malloc(sizeof(struct in_out_cpy));
  io->in_ctx = s;
  io->out_ctx = rtp_context;
  io->in_stream_index = video_stream_index;
  io->out_stream = rtp_stream;
  io->active = 1;

  pthread_t copy_input_to_output_thread;
  if(pthread_create(&copy_input_to_output_thread, NULL, copy_input_to_output, io) < 0)
    error("pthread_create");
  if(pthread_detach(copy_input_to_output_thread) < 0)
    error("pthread_detach");

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;
  screen->width = cam->codec->width;
  screen->height = cam->codec->height;
  screen->io = io;
}

static gboolean timeout(GstRTSPServer * server, gboolean ignored) {
  GstRTSPSessionPool *pool;
 
  pool = gst_rtsp_server_get_session_pool(server);
  gst_rtsp_session_pool_cleanup(pool);
  g_object_unref(pool);
 
  return TRUE;
}

void *start_rtsp_server(void* ptr) {
  GMainLoop *loop;

  loop = g_main_loop_new(NULL, FALSE);
  server = gst_rtsp_server_new();

  gst_rtsp_server_attach(server, NULL);
  g_timeout_add_seconds(2, (GSourceFunc)timeout, server);
  printf("RSTP server starting\n");
  g_main_loop_run(loop);
  printf("RSTP server stopped\n");
  return NULL;
}
