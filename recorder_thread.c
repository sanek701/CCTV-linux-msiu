#include <highgui.h>
#include <libswscale/swscale.h>
#include "cameras.h"

extern char *store_dir;

static int interrupt_callback(void *opaque) {
  struct camera *cam = opaque;
  if(time(NULL) - cam->last_io > 5) return 1;
  return 0;
}

static int open_input(AVFormatContext *s, AVDictionary **opts) {
  int ret;
  AVCodec *dec;
  int video_stream_index;
  if((ret = avformat_open_input(&s, s->filename, NULL, opts)) < 0)
    av_crit("avformat_open_input", ret);
  if((ret = avformat_find_stream_info(s, NULL)) < 0)
    av_crit("avformat_find_stream_info", ret);
  video_stream_index = av_find_best_stream(s, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  if(video_stream_index < 0)
    av_crit("av_find_best_stream", video_stream_index);
  if((ret = avcodec_open2(s->streams[video_stream_index]->codec, dec, NULL)) < 0) {
    av_crit("avcodec_open2", ret);
  }
  return video_stream_index;
}

static void open_camera(struct camera *cam) {
  int ret = 0;
  AVDictionary *opts = NULL;
  if((ret = av_dict_set(&opts, "rtsp_transport", "tcp", 0)) < 0)
    av_crit("av_dict_set", ret);
  cam->context = avformat_alloc_context();
  snprintf(cam->context->filename, sizeof(cam->context->filename), "%s", cam->url);
  cam->context->interrupt_callback.callback = &interrupt_callback;
  cam->context->interrupt_callback.opaque = cam;
  cam->last_io = time(NULL);

  cam->video_stream_index = open_input(cam->context, &opts);
  cam->input_stream = cam->context->streams[cam->video_stream_index];
  cam->codec = cam->input_stream->codec;
  av_dict_free(&opts);
}

static void open_output(struct camera *cam) {
  int ret = 0;
  //AVDictionary *av_opts = NULL;
  AVFormatContext *s;
  AVOutputFormat *ofmt;
  AVStream *ost;

  s = avformat_alloc_context();

  if((ofmt = av_guess_format("h264", NULL, NULL)) == NULL)
    av_crit("av_guess_format", 0);
  s->oformat = ofmt;

  create_videofile(cam, s->filename);

  if((ret = avio_open2(&s->pb, s->filename, AVIO_FLAG_WRITE, NULL, NULL)) < 0)
    av_crit("avio_open2", ret);

  ost = avformat_new_stream(s, (AVCodec *)cam->codec->codec);
  if(ost == NULL)
    av_crit("avformat_new_stream", 0);
  if((ret = avcodec_copy_context(ost->codec, (const AVCodecContext *)cam->codec)) < 0)
    av_crit("avcodec_copy_context", ret);

  ost->sample_aspect_ratio = cam->codec->sample_aspect_ratio;
  ost->r_frame_rate        = cam->input_stream->r_frame_rate;
  ost->avg_frame_rate      = ost->r_frame_rate;
  ost->time_base           = cam->input_stream->time_base;
  ost->codec->time_base    = ost->time_base;
  
  if(ofmt->flags & AVFMT_GLOBALHEADER)
    ost->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

  if((ret = avformat_write_header(s, NULL)) < 0) //&av_opts
    av_crit("avformat_write_header", ret);

  cam->output_context = s;
  cam->output_stream = ost;
  
  //av_dict_free(&av_opts);
}

int video_stream_index;
AVFormatContext *rd;
AVStream *st;

static void close_output(struct camera *cam) {
  int ret;
  if((ret = av_write_trailer(cam->output_context)) < 0)
    av_crit("av_write_trailer", ret);
  if((ret = avcodec_close(cam->output_stream->codec)) < 0)
    av_crit("avcodec_close", ret);
  if((ret = avio_close(cam->output_context->pb)) < 0)
    av_crit("avio_close", ret);
  avformat_free_context(cam->output_context);
}

static int detect_motion(struct motion_detection *md, AVFrame *frame) {
  IplImage *tmp;
  AVPicture pict;

  tmp = md->cur;
  md->cur = md->prev;
  md->prev = tmp;

  avpicture_fill(&pict, md->buffer, PIX_FMT_GRAY8, md->cam->codec->width, md->cam->codec->height);
  sws_scale(md->img_convert_ctx, (const uint8_t* const*)frame->data, frame->linesize, 0, md->cam->codec->height, (uint8_t* const*)pict.data, pict.linesize);
  memcpy(md->cur->imageData, pict.data[0], md->cur->imageSize);
  md->cur->widthStep = pict.linesize[0];

  cvAbsDiff(md->cur, md->prev, md->silh);
  cvThreshold(md->silh, md->silh, md->cam->threshold, 250, CV_THRESH_BINARY);

  int density = 0;
  for(int i=0; i < md->silh->height; i++) {
    uint8_t* ptr = md->silh->imageData + i * md->silh->widthStep;
    for(int j=0; j < md->silh->width; j++)
      if(*(ptr+j) > 0)
        density += 1;
  }

  if((float)density / (float)(md->silh->height * md->silh->width) > 0.01) {
    return 1;
  } else {
    return 0;
  }

  //avpicture_free(&pict);
}

void *recorder_thread(void *ptr) {
  struct camera *cam = (struct camera *)ptr;
  struct motion_detection md;
  AVPacket packet;
  AVFrame *frame;
  int got_frame, ret;
  unsigned int cnt = 0;
  time_t first_activity = 0;
  time_t last_activity = 0;

  open_camera(cam);
  open_output(cam);

  av_dump_format(cam->context, 0, cam->context->filename, 0);
  av_dump_format(cam->output_context, 0, cam->output_context->filename, 1);

  md.cam = cam;

  md.prev = cvCreateImage(cvSize(cam->codec->width, cam->codec->height), IPL_DEPTH_8U, 1);
  md.cur  = cvCreateImage(cvSize(cam->codec->width, cam->codec->height), IPL_DEPTH_8U, 1);
  md.silh = cvCreateImage(cvSize(cam->codec->width, cam->codec->height), IPL_DEPTH_8U, 1);
  cvZero(md.prev);
  cvZero(md.cur);
  cvZero(md.silh);

  md.img_convert_ctx = sws_getContext(
    cam->codec->width, cam->codec->height, cam->codec->pix_fmt,
    cam->codec->width, cam->codec->height, PIX_FMT_GRAY8,
    SWS_BICUBIC, NULL, NULL, NULL);
  md.buffer = (uint8_t*)av_malloc(3 * cam->codec->width * cam->codec->height);

  int got_key_frame = 0, first_detection = 1;
  frame = avcodec_alloc_frame();
  if(!frame)
    av_crit("avcodec_alloc_frame", 0);
  av_init_packet(&packet);
  while(1) {
    cam->last_io = time(NULL);
    ret = av_read_frame(cam->context, &packet);
    if(ret < 0) {
      if(ret == AVERROR_EOF) break;
      else av_crit("av_read_frame", ret);
    }

    if(packet.stream_index == cam->video_stream_index) {
      // start on keyframe
      if(!got_key_frame && !(packet.flags & AV_PKT_FLAG_KEY)) {
        continue;
      }
      got_key_frame = 1;

      avcodec_get_frame_defaults(frame);
      got_frame = 0;

      cnt = (cnt + 1) % cam->analize_frames;
      if(cnt == 0) {
        if((ret = avcodec_decode_video2(cam->codec, frame, &got_frame, &packet)) < 0)
          av_crit("avcodec_decode_video2", ret);

        if(got_frame) {
          if(detect_motion(&md, frame)) {
            if(first_activity == 0) first_activity = time(NULL);
            last_activity = time(NULL);
          } else {
            if(first_activity > 0 && time(NULL) - last_activity > cam->motion_delay) {
              if(!first_detection)
                create_event(cam->id, first_activity, last_activity);
              else
                first_detection = 0;
              first_activity = 0;
            }
          }
        }

        if(time(NULL) - cam->last_screenshot > 60 && (packet.flags & AV_PKT_FLAG_KEY)) {
          char fname[128];
          snprintf(fname, sizeof(fname), "%s/%s/screenshot.png", store_dir, cam->name);
          cvSaveImage(fname, md.cur, 0);
          cam->last_screenshot = time(NULL);
        }
      }

      packet.stream_index = cam->output_stream->id;
      if((ret = av_write_frame(cam->output_context, &packet)) < 0)
        av_crit("av_write_frame", ret);

      for(l1 *p = cam->cam_consumers_list; p != NULL; p = p->next) {
        struct cam_consumer *consumer = (struct cam_consumer *)p->value;
        if(consumer->screen->ncams == 1) {
          packet.stream_index = consumer->screen->rtp_stream->id;
          if((ret = av_write_frame(consumer->screen->rtp_context, &packet)) < 0)
            av_crit("av_write_frame", ret);
        } else {
          // decode frame
          // rescame image
          // copy to output image
        }
      }
    }
    av_free_packet(&packet);
    av_init_packet(&packet);
    
    if(!cam->active) {
      break;
    }

    if(time(NULL) - cam->file_started_at > 60*60) {
      update_videofile(cam);
      close_output(cam);
      open_output(cam);
      got_key_frame = 0;
    }
  }

  update_videofile(cam);
  close_output(cam);
  if((ret = avcodec_close(cam->codec)) < 0)
    av_crit("avcodec_close", ret);
  avformat_close_input(&cam->context);
  av_free(frame);

  cvReleaseImage(&md.prev);
  cvReleaseImage(&md.cur);
  cvReleaseImage(&md.silh);
  av_free(md.buffer);
  sws_freeContext(md.img_convert_ctx);

  return NULL;
}
