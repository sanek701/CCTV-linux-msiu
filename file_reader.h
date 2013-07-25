int open_input(AVFormatContext *s, AVDictionary **opts);
AVStream* init_h264_read_ctx(AVFormatContext *s, struct camera *cam);
int h264_seek_file(AVFormatContext *s, AVStream *ist, int timestampt);
void* copy_input_to_output(void *ptr);
void* multiple_cameras_thread(void *ptr);
void* start_h264_to_mp4_service(void *ptr);
