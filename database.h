void init_pg_conn(const char *conf_file);
void close_pg_conn();
struct camera* select_cameras(int *ncams);
void create_event(int cam_id, time_t raw_started_at, time_t raw_finished_at);
void create_videofile(struct camera *cam, char *filepath);
void update_videofile(struct camera *cam);
int find_video_file(int cam_id, char *fname, time_t *timestamp);
