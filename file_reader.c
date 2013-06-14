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
  int ret;
  struct in_out_cpy *io = (struct in_out_cpy *)ptr;
  AVPacket packet;

  int frame = 0;
  int64_t start = av_gettime();
  int64_t pts = 0, now;

  AVRational frame_rate = io->in_stream->r_frame_rate;
  AVRational time_base = av_inv_q(frame_rate);

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

    if(io->rate_emu)
      packet.dts = av_rescale_q(av_gettime(), AV_TIME_BASE_Q, io->out_stream->time_base);
    else
      packet.dts = av_rescale_q(frame, time_base, io->out_stream->time_base);

    packet.stream_index = io->out_stream->id;

    if((ret = av_write_frame(io->out_ctx, &packet)) < 0) {
      av_err_msg("av_write_frame", ret);
      break;
    }

    av_free_packet(&packet);
    av_init_packet(&packet);

    if(!io->active)
      break;

    frame += 1;
    pts = frame * frame_rate.den * AV_TIME_BASE / frame_rate.num;
    now = av_gettime() - start;

    if(io->rate_emu && pts > now) {
      av_usleep(pts - now);
    }
  }

  if((ret = av_write_trailer(io->out_ctx)) < 0)
    av_err_msg("av_write_trailer", ret);

  avio_close(io->out_ctx->pb);
  avformat_close_input(&io->in_ctx);
  avformat_free_context(io->out_ctx);
  free(io);

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