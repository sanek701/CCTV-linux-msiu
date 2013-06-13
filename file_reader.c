#include "cameras.h"

extern int should_terminate;

void* copy_input_to_output(void *ptr) {
  int ret;
  struct in_out_cpy *io = (struct in_out_cpy *)ptr;
  AVPacket packet;

  int frame = 0;
  int64_t start = av_gettime();
  int64_t pts = 0, now;

  AVRational frame_rate = io->in_stream->r_frame_rate;

  av_init_packet(&packet);
  while(1) {
    ret = av_read_frame(io->in_ctx, &packet);
    if(ret == AVERROR_EOF) {
      printf("EOF\n");
      break;
    }
    if(ret < 0) {
      av_crit("av_read_frame", ret);
      break;
    }

    packet.stream_index = io->out_stream->id;
    packet.dts = av_rescale_q(av_gettime(), AV_TIME_BASE_Q, io->out_stream->time_base);

    if((ret = av_write_frame(io->out_ctx, &packet)) < 0) {
      av_crit("av_write_frame", ret);
      break;
    }

    av_free_packet(&packet);
    av_init_packet(&packet);

    if(!io->active)
      break;
    if(should_terminate)
      break;

    frame += 1;
    pts = frame * frame_rate.den * AV_TIME_BASE / frame_rate.num;
    now = av_gettime() - start;

    if(pts > now) {
      av_usleep(pts - now);
    }
  }

  avio_close(io->out_ctx->pb);
  avformat_close_input(&io->in_ctx);
  avformat_free_context(io->out_ctx);
  free(io);
  return NULL;
}
