#include "cameras.h"

GMainLoop *loop;
GstRTSPServer *server;
GstRTSPMediaMapping *mapping;
GstRTSPMediaFactory *factory;

static void open_video_file(struct screen *screen);

void init_screen(struct screen *screen) {
  char gst_pipe[1024], pipe[256];
  AVFormatContext *rtp_context;
  AVOutputFormat *rtp_fmt;
  AVStream *rtp_stream;
  AVCodecContext *c;
  AVCodec *codec;
  int ret;

  sprintf(pipe, "/tmp/rtsp_pipe_%d", screen->session_id);
  if(mkfifo(pipe, 0666) < 0)
    error("mkfifo");

  rtp_context = avformat_alloc_context();
  rtp_fmt = av_guess_format("rtp", NULL, NULL);

  codec = avcodec_find_encoder(CODEC_ID_H264);

  if(rtp_fmt == NULL)
    av_crit("av_guess_format", 0);
  rtp_context->oformat = rtp_fmt;
  if((ret = avio_open(&(rtp_context->pb), pipe, AVIO_FLAG_WRITE)) < 0)
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

  sprintf(gst_pipe, "( filesrc location=%s ! application/x-rtp,payload=96 ! rtph264depay ! rtph264pay name=pay0 pt=96 )", pipe);

  factory = gst_rtsp_media_factory_new();
  gst_rtsp_media_factory_set_launch(factory, gst_pipe);
  gst_rtsp_media_mapping_add_factory(mapping, "/test", factory);
  gst_rtsp_media_factory_set_shared(factory, TRUE);

  screen->rtp_context = rtp_context;
  screen->rtp_stream = rtp_stream;

  if(screen->type == REAL) {
    // create consumer
    // create thead that joins images
  } else {
    open_video_file(screen);
  }
}

/*
void init_single_camera_screen(struct camera *cam) {
    AVFormatContext *rtp_context;
    AVOutputFormat *rtp_fmt;
    AVStream *rtp_stream;
    int ret;

    rtp_context = avformat_alloc_context();
    rtp_fmt = av_guess_format("rtp", NULL, NULL);
    if(rtp_fmt == NULL)
      av_crit("av_guess_format", 0);
    rtp_context->oformat = rtp_fmt;
    if((ret = avio_open(&(rtp_context->pb), "rtp://localhost:4444", AVIO_FLAG_WRITE)) < 0)
      av_crit("avio_open", ret);
    rtp_stream = avformat_new_stream(rtp_context, (AVCodec *)cam->codec->codec);
    if(rtp_stream == NULL)
      av_crit("avformat_new_stream", 0);
    if((ret = avcodec_copy_context(rtp_stream->codec, (const AVCodecContext *)cam->codec)) < 0)
      av_crit("avcodec_copy_context", ret);
    if((ret = avformat_write_header(rtp_context, NULL)) < 0)
      printf("avformat_write_header", ret);

}
*/

static void open_video_file(struct screen *screen) {
  AVFormatContext *s;
  int stream_index;
  int64_t timestamp;
  av_seek_frame(s, stream_index, timestamp, 0);
}

static void create_frame() {
  AVFrame *picture = avcodec_alloc_frame();
  uint8_t *picture_buf;
  int size;
  size = avpicture_get_size(PIX_FMT_YUV420P, 1024, 768);
  picture_buf = malloc(size);
  avpicture_fill((AVPicture *)picture, picture_buf, PIX_FMT_YUV420P, 1024, 768);
}

void *start_rtsp_server(void* ptr) {
  loop = g_main_loop_new(NULL, FALSE);
  server = gst_rtsp_server_new();
  mapping = gst_rtsp_server_get_media_mapping(server);
  gst_rtsp_server_attach(server, NULL);
  g_main_loop_run(loop);
  return NULL;
}
