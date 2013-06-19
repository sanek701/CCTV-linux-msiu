#include <unistd.h>
#include "cameras.h"

int terminate_h264_to_mp4_service = 0;
l1 *h264_to_mp4_tasks = NULL;
pthread_mutex_t tasks_lock;

int open_input(AVFormatContext *s, AVDictionary **opts) {
  int ret;
  AVCodec *dec;
  int video_stream_index;

  if((ret = avformat_open_input(&s, s->filename, NULL, opts)) < 0) {
    av_err_msg("avformat_open_input", ret);
    return -1;
  }
  if((ret = avformat_find_stream_info(s, NULL)) < 0) {
    av_err_msg("avformat_find_stream_info", ret);
    return -1;
  }
  video_stream_index = av_find_best_stream(s, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
  if(video_stream_index < 0) {
    av_err_msg("av_find_best_stream", video_stream_index);
    return -1;
  }
  if((ret = avcodec_open2(s->streams[video_stream_index]->codec, dec, NULL)) < 0) {
    av_err_msg("avcodec_open2", ret);
    return -1;
  }
  return video_stream_index;
}

AVStream* init_h264_read_ctx(AVFormatContext *s, struct camera *cam) {
  int ret;
  if((ret = avformat_open_input(&s, s->filename, NULL, NULL)) < 0) {
    av_err_msg("avformat_open_input", ret);
    return NULL;
  }
  if((ret = avformat_find_stream_info(s, NULL)) < 0) {
    av_err_msg("avformat_find_stream_info", ret);
    return NULL;
  }
  int video_stream_index = av_find_best_stream(s, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
  if(video_stream_index < 0) {
    av_err_msg("av_find_best_stream", video_stream_index);
    return NULL;
  }

  AVStream *input_stream = s->streams[video_stream_index];
  input_stream->time_base = input_stream->codec->time_base = AV_TIME_BASE_Q;
  input_stream->r_frame_rate = cam->input_stream->r_frame_rate;
  return input_stream;
}

int h264_seek_file(AVFormatContext *s, AVStream *ist, int timestampt) {
  int ret;
  AVPacket packet;
  int frame = 0;

  AVRational frame_rate = ist->r_frame_rate;

  av_init_packet(&packet);
  while(1) {
    if((ret = av_read_frame(s, &packet)) < 0) {
      av_err_msg("av_read_frame", ret);
      return -1;
    }

    frame += 1;

    if(frame * frame_rate.den / frame_rate.num >= timestampt)
      break;
  }
  return 0;
}

void* copy_input_to_output(void *ptr) {
  int ret, got_key_frame = 0;
  struct in_out_cpy *io = (struct in_out_cpy *)ptr;
  AVPacket packet;

  io->frame = 0;
  io->start_time = av_gettime();
  int64_t pts, now;

  AVRational frame_rate = io->in_stream->r_frame_rate;
  AVRational time_base = av_inv_q(frame_rate);

  if(!io->rate_emu)
    got_key_frame = 1;

  av_init_packet(&packet);
  while(1) {
    ret = av_read_frame(io->in_ctx, &packet);
    if(ret == AVERROR_EOF) {
      break;
    }
    if(ret < 0) {
      av_err_msg("av_read_frame", ret);
      break;
    }

    if(!got_key_frame && !(packet.flags & AV_PKT_FLAG_KEY)) {
      continue;
    }
    got_key_frame = 1;

    if(io->prev_io != NULL) {
      pthread_mutex_init(&io->prev_io->io_lock, NULL);
      pthread_mutex_lock(&io->prev_io->io_lock);
      io->start_time = io->prev_io->start_time;
      io->frame = io->prev_io->frame + 1;
      io->prev_io->close_output = 0;
      io->prev_io->active = 0;
      pthread_mutex_lock(&io->prev_io->io_lock); // wait until prev io finishes
      io->prev_io = NULL;
    }

    if(io->rate_emu)
      packet.dts = av_rescale_q(av_gettime(), AV_TIME_BASE_Q, io->out_stream->time_base);
    else
      packet.dts = av_rescale_q(io->frame, time_base, io->out_stream->time_base);

    packet.pts = AV_NOPTS_VALUE;
    packet.stream_index = io->out_stream->id;

    if((ret = av_write_frame(io->out_ctx, &packet)) < 0) {
      av_err_msg("av_write_frame", ret);
      break;
    }

    // TODO: '[rtp @ 0x979dd90] pts (450000) < dts (123436231555728) in stream 0' when playing mp4

    av_free_packet(&packet);
    av_init_packet(&packet);

    if(!io->active)
      break;

    io->frame += 1;
    pts = io->frame * frame_rate.den * AV_TIME_BASE / frame_rate.num;
    now = av_gettime() - io->start_time;

    if(io->rate_emu && pts > now) {
      av_usleep(pts - now);
    }
  }

  avformat_close_input(&io->in_ctx);

  if(io->close_output) {
    if((ret = av_write_trailer(io->out_ctx)) < 0)
      av_err_msg("av_write_trailer", ret);
    avio_close(io->out_ctx->pb);
    avformat_free_context(io->out_ctx);
  } else {
    pthread_mutex_unlock(&io->io_lock); // release next io
    pthread_mutex_destroy(&io->io_lock);
  }

  free(io);

  return NULL;
}

void* multiple_cameras_thread(void * ptr) {
  struct screen *screen = (struct screen *)ptr;
  AVCodecContext *c = screen->rtp_stream->codec;
  AVFrame *frame;
  AVPicture picture;
  AVPacket pkt;
  int ret, got_output;

  frame = avcodec_alloc_frame();

  if((ret = avpicture_alloc(&picture, c->pix_fmt, c->width, c->height)) < 0) {
    avformat_free_context(screen->rtp_context);
    av_err_msg("avpicture_alloc", ret);
    return NULL;
  }

  /* copy data and linesize picture pointers to frame */
  *((AVPicture *)frame) = picture;

  if((ret = avformat_write_header(screen->rtp_context, NULL)) < 0) {
    avformat_free_context(screen->rtp_context);
    av_err_msg("avformat_write_header", ret);
    return NULL;
  }

  while(1) {
    av_init_packet(&pkt);
    pkt.data = NULL; // packet data will be allocated by the encoder
    pkt.size = 0;

    if((ret = avcodec_encode_video2(c, &pkt, frame, &got_output)) < 0) {
      avformat_free_context(screen->rtp_context);
      av_err_msg("avcodec_encode_video2", ret);
      return NULL;
    }

    /* If size is zero, it means the image was buffered. */
    if (got_output) {
      if (c->coded_frame->pts != AV_NOPTS_VALUE)
        pkt.pts = av_rescale_q(c->coded_frame->pts, c->time_base, screen->rtp_stream->time_base);
      if (c->coded_frame->key_frame)
        pkt.flags |= AV_PKT_FLAG_KEY;

      pkt.stream_index = screen->rtp_stream->index;

      /* Write the compressed frame to the media file. */
      if((ret = av_interleaved_write_frame(screen->rtp_context, &pkt)) < 0) {
        avformat_free_context(screen->rtp_context);
        av_err_msg("av_interleaved_write_frame", ret);
        return NULL;
      }
    }
  }

  return NULL;
}

void* start_h264_to_mp4_service(void *ptr) {
  char fname[1024];
  int file_id;
  struct in_out_cpy *io;

  if(pthread_mutex_init(&tasks_lock, NULL) < 0) {
    fprintf(stderr, "pthread_mutex_init failed\n");
    exit(EXIT_FAILURE);
  }

  while(1) {
    while(1) {
      io = (struct in_out_cpy *)l1_shift(&h264_to_mp4_tasks, &tasks_lock);
      if(io == NULL) break;

      strcpy(fname, io->in_ctx->filename);
      file_id = io->file_id;

      av_dump_format(io->out_ctx, 0, io->out_ctx->filename, 1);

      copy_input_to_output(io);
      db_update_mp4_file_flag(file_id);
      unlink(fname);
    }
    if(terminate_h264_to_mp4_service) break;
    sleep(3);
  }

  pthread_mutex_destroy(&tasks_lock);
  return NULL;
}
