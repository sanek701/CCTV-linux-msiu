#include "cameras.h"
#include <signal.h>
#include <strings.h>
#include <sys/statvfs.h>
#include <sys/time.h>

extern PGconn *conn;
extern char *store_dir;
struct camera *cameras;
int n_cameras;
int should_terminate = 0;

void *recorder_thread(void *ptr);
void init_pg_conn(const char *conf_file);
void close_pg_conn();
void *start_rtsp_server(void* ptr);
void initialize_control_socket();
void control_socket_loop();
void close_control_socket();

void print_camera(struct camera *cam) {
  fprintf(stderr, "Camera %d <url: %s, analize_frames: %d, threshold: %d, motion_delay: %d>\n",
    cam->id, cam->url, cam->analize_frames, cam->threshold, cam->motion_delay);
}

void terminate(int signal) {
  fprintf(stderr, "Terminating...\n");
  for(int i=0; i < n_cameras; i++) {
    cameras[i].active = 0;
  }
  should_terminate = 1;
}

void error(const char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

void check_disk_space() {
  struct statvfs statvfs_buf;
  if(statvfs(store_dir, &statvfs_buf) < 0) {
    perror("statvfs");
  }
  printf("Free space on device: %llu\n", (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_bavail);
}

int main(int argc, char** argv) {
  if(argc < 2) {
    fprintf(stderr, "specify config file\n");
    exit(EXIT_FAILURE);
  }

  gst_init(&argc, &argv);

  initialize_control_socket();
  fprintf(stderr, "Control socket initialized\n");

  signal(SIGINT, terminate);

  //  av_log_set_level(AV_LOG_DEBUG);
  av_register_all();
  avcodec_register_all();
  avformat_network_init();
  // avdevice_register_all(); //Register all devices

  fprintf(stderr, "FFmpeg initialized\n");

  init_pg_conn(argv[1]);

  PGresult *result = PQexec(conn, "SELECT id, url, code, analize_frames, threshold, motion_delay FROM cameras ORDER BY id;");
  if(PQresultStatus(result) != PGRES_TUPLES_OK) {
    fprintf(stderr, "No cameras found: %s\n", PQresultErrorMessage(result));
    PQclear(result);
  }

  n_cameras = 1; //PQntuples(result);
  cameras = (struct camera*)malloc(n_cameras * sizeof(struct camera));
  memset(cameras, 0, n_cameras * sizeof(struct camera));

  for(int i=0; i < n_cameras; i++) {
    char *id = PQgetvalue(result, i, 0);
    char *url = PQgetvalue(result, i, 1);
    char *name = PQgetvalue(result, i, 2);
    char *analize_frames = PQgetvalue(result, i, 3);
    char *threshold = PQgetvalue(result, i, 4);
    char *motion_delay = PQgetvalue(result, i, 5);

    cameras[i].url = (char*)malloc(strlen(url)+1);
    cameras[i].name = (char*)malloc(strlen(name)+1);
    strcpy(cameras[i].url, url);
    strcpy(cameras[i].name, name);
    sscanf(id, "%d", &cameras[i].id);
    sscanf(analize_frames, "%d", &cameras[i].analize_frames);
    sscanf(threshold, "%d", &cameras[i].threshold);
    sscanf(motion_delay, "%d", &cameras[i].motion_delay);

    cameras[i].last_screenshot = 0;
    cameras[i].active = 1;
    cameras[i].cam_consumers_list = NULL;

    print_camera(&cameras[i]);
  }
  PQclear(result);

  pthread_t *threads = (pthread_t *)malloc(n_cameras * sizeof(pthread_t));

  for(int i=0; i < n_cameras; i++) {
    if(pthread_create(&threads[i], NULL, recorder_thread, (void *)(&cameras[i])) < 0)
      error("pthread_create");
  }

  fprintf(stderr, "All cameras started\n");

  pthread_t rtsp_server_thread;
  if(pthread_create(&rtsp_server_thread, NULL, start_rtsp_server, NULL) < 0)
    error("pthread_create");

  control_socket_loop();

  for(int i=0; i < n_cameras; i++) {
    pthread_join(threads[i], NULL);
  }

  for(int i=0; i < n_cameras; i++) {
    free(cameras[i].url);
    free(cameras[i].name);
  }
  free(cameras);
  free(threads);
  avformat_network_deinit();
  close_pg_conn();
  close_control_socket();

  fprintf(stderr, "Terminated.\n");
  return (EXIT_SUCCESS);
}
