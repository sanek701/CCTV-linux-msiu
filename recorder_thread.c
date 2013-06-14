#include <highgui.h>
#include <libswscale/swscale.h>
#include "cameras.h"
#include "file_reader.h"

extern char *store_dir;
extern l1 *h264_to_mp4_tasks;
extern pthread_mutex_t tasks_lock;

static int interrupt_callback(void *opaque) {
  struct camera *cam = opaque;
  if(time(NULL) - cam->last_io > 5) return 1;
  return 0;
}

static int open_camera(struct camera *cam) {
  int ret = 0, v_st_i;
  AVDictionary *opts = NULL;

  if((ret = av_dict_set(&opts, "rtsp_transport", "tcp", 0)) < 0) {
    av_err_msg ("av_dict_set", ret);
    return -1;
  }

  cam->context = avformat_alloc_context();
  snprintf(cam->context->filename, sizeof(cam->context->filename), "%s", cam->url);
  cam->context->interrupt_callback.callback = &interrupt_callback;
  cam->context->interrupt_callback.opaque = cam;
  cam->last_io = time(NULL);

  if((v_st_i = open_input(cam->context, &opts)) < 0)
    return -1;

  cam->video_stream_index = v_st_i;
  cam->input_stream = cam->context->streams[cam->video_stream_index];
  cam->codec = cam->input_stream->codec;
  av_dict_free(&opts);

  return 0;
}

static AVStream* copy_ctx_from_input(AVFormatContext *s, struct camera *cam) {
  int ret;
  AVStream* ost = avformat_new_stream(s, (AVCodec *)cam->codec->codec);
  if(ost == NULL) {
    av_err_msg("avformat_new_stream", 0);
    return NULL;
  }
  if((ret = avcodec_copy_context(ost->codec, (const AVCodecContext *)cam->codec)) < 0) {
    av_err_msg("avcodec_copy_context", ret);
    return NULL;
  }

  ost->sample_aspect_ratio = cam->codec->sample_aspect_ratio;
  ost->r_frame_rate        = cam->input_stream->r_frame_rate;
  ost->avg_frame_rate      = ost->r_frame_rate;
  ost->time_base           = cam->input_stream->time_base;
  ost->codec->time_base    = ost->time_base;
  
  if(s->oformat->flags & AVFMT_GLOBALHEADER)
    ost->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;

  return ost;
}

static int open_output(struct camera *cam) {
  AVFormatContext *s;
  AVStream *ost;
  int ret;

  s = avformat_alloc_context();

  if((s->oformat = av_guess_format("h264", NULL, NULL)) == NULL) {
    av_err_msg("av_guess_format", 0);
    avformat_free_context(s);
    return -1;
  }

  db_create_videofile(cam, s->filename);

  if((ret = avio_open2(&s->pb, s->filename, AVIO_FLAG_WRITE, NULL, NULL)) < 0) {
    av_err_msg("avio_open2", ret);
    avformat_free_context(s);
    return -1;
  }
  if((ost = copy_ctx_from_input(s, cam)) == NULL) {
    fprintf(stderr, "copy_ctx_from_input failed\n");
    avformat_free_context(s);
    return -1;
  }
  if((ret = avformat_write_header(s, NULL)) < 0) {
    av_err_msg("avformat_write_header", ret);
    avformat_free_context(s);
    return -1;
  }

  cam->output_context = s;
  cam->output_stream = ost;
  return 0;
}

static AVStream* init_mp4_output(AVFormatContext *s, struct camera *cam) {
  int ret;
  AVStream *ost;
  strcpy(s->filename + strlen(s->filename) - 4, "mp4");
  if((s->oformat = av_guess_format("mp4", NULL, NULL)) == NULL) {
    av_err_msg("av_guess_format", 0);
    return NULL;
  }
  if((ret = avio_open2(&s->pb, s->filename, AVIO_FLAG_WRITE, NULL, NULL)) < 0) {
    av_err_msg("avio_open2", ret);
    return NULL;
  }
  if((ost = copy_ctx_from_input(s, cam)) == NULL) {
    fprintf(stderr, "copy_ctx_from_input failed\n");
    return NULL;
  }
  if((ret = avformat_write_header(s, NULL)) < 0) {
    av_err_msg("avformat_write_header", ret);
    avformat_free_context(s);
    return NULL;
  }
  return ost;
}

static void close_output(struct camera *cam) {
  int ret;

  AVFormatContext *rd = avformat_alloc_context();
  AVFormatContext *wr = avformat_alloc_context();
  strcpy(rd->filename, cam->output_context->filename);
  strcpy(wr->filename, cam->output_context->filename);

  AVStream *ist = init_h264_read_ctx(rd, cam);
  AVStream *ost = init_mp4_output(wr, cam);

  if(ist != NULL && ost != NULL) {
    struct in_out_cpy* io = (struct in_out_cpy*)malloc(sizeof(struct in_out_cpy));
    io->in_ctx = rd;
    io->out_ctx = wr;
    io->in_stream = ist;
    io->out_stream = ost;
    io->rate_emu = 0;
    io->active = 1;
    l1_insert(&h264_to_mp4_tasks, &tasks_lock, io);
  } else {
    fprintf(stderr, "init_h264_read_ctx or init_mp4_output failed\n");
  }

  if((ret = av_write_trailer(cam->output_context)) < 0)
    av_err_msg("av_write_trailer", ret);
  if((ret = avcodec_close(cam->output_stream->codec)) < 0)
    av_err_msg("avcodec_close", ret);
  if((ret = avio_close(cam->output_context->pb)) < 0)
    av_err_msg("avio_close", ret);
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
    uint8_t* ptr = (uint8_t*)md->silh->imageData + i * md->silh->widthStep;
    for(int j=0; j < md->silh->width; j++)
      if(*(ptr+j) > 0)
        density += 1;
  }

  if((float)density / (float)(md->silh->height * md->silh->width) > 0.01) {
    return 1;
  } else {
    return 0;
  }
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

  if(open_camera(cam) < 0)
    return NULL;
  if(open_output(cam) < 0)
    return NULL;

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
  if(!frame) {
    av_err_msg("avcodec_alloc_frame", 0);
    return NULL;
  }
  av_init_packet(&packet);

  while(1) {
    cam->last_io = time(NULL);
    ret = av_read_frame(cam->context, &packet);
    if(ret < 0) {
      if(ret == AVERROR_EOF) break;
      else av_err_msg("av_read_frame", ret);
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
          av_err_msg("avcodec_decode_video2", ret);

        if(got_frame) {
          if(detect_motion(&md, frame)) {
            if(first_activity == 0) first_activity = time(NULL);
            last_activity = time(NULL);
          } else {
            if(first_activity > 0 && time(NULL) - last_activity > cam->motion_delay) {
              if(!first_detection)
                db_create_event(cam->id, first_activity, last_activity);
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
        av_err_msg("av_write_frame", ret);

      for(l1 *p = cam->cam_consumers_list; p != NULL; p = p->next) {
        struct cam_consumer *consumer = (struct cam_consumer *)p->value;
        if(consumer->screen->ncams == 1) {
          packet.stream_index = consumer->screen->rtp_stream->id;
          if((ret = av_write_frame(consumer->screen->rtp_context, &packet)) < 0)
            av_err_msg("av_write_frame", ret);
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

    if(time(NULL) - cam->file_started_at > 60 * 60) {
      db_update_videofile(cam);
      close_output(cam);
      open_output(cam);
      got_key_frame = 0;
    }
  }

  db_update_videofile(cam);
  close_output(cam);

  if((ret = avcodec_close(cam->codec)) < 0)
    av_err_msg("avcodec_close", ret);
  avformat_close_input(&cam->context);
  av_free(frame);

  cvReleaseImage(&md.prev);
  cvReleaseImage(&md.cur);
  cvReleaseImage(&md.silh);
  av_free(md.buffer);
  sws_freeContext(md.img_convert_ctx);

  return NULL;
}
