void db_init_pg_conn(const char *conf_file);
void db_close_pg_conn();
struct camera* db_select_cameras(int *ncams);
void db_create_event(int cam_id, time_t raw_started_at, time_t raw_finished_at);
void db_create_videofile(struct camera *cam, char *filepath);
void db_update_videofile(struct camera *cam);
int  db_find_video_file(int cam_id, char *fname, time_t *timestamp);
void db_update_mp4_file_flag(int file_id);
int  db_unlink_oldest_file();
