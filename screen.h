int   screen_init(struct screen *screen);
int   screen_find_func(void *value, void *arg);
void  screen_destroy(struct screen *screen);
void  screen_remove(struct screen *screen);
int   screen_open_video_file(struct screen *screen);
char* screen_create_sdp(struct screen *screen);
