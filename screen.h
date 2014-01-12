struct screen {
  int type;
  int ncams;
  int tmpl_size;
  int active;
  char *screen_id;
  struct camera **cams;
  time_t timestamp;
  AVFormatContext *rtp_context;
  AVStream *rtp_stream;
  AVPicture combined_picture;
  pthread_t worker_thread;
  pthread_mutex_t combined_picture_lock;
  struct in_out_cpy *io;
};

struct cam_consumer {
  struct screen *screen;
  int position;
  AVPicture picture;
  struct SwsContext *sws_context;
};

int   screen_init(struct screen *screen);
int   screen_find_func(void *value, void *arg);
void  screen_destroy(struct screen *screen);
void  screen_remove(struct screen *screen);
int   screen_open_video_file(struct screen *screen);
char* screen_create_sdp(struct screen *screen);
