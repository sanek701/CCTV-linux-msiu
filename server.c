#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <signal.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include "cameras.h"
#include "control_socket.h"

struct camera *cameras = NULL;
int n_cameras = 0;
int should_terminate = 0;
char *store_dir;

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

void av_crit(char *msg, int errnum) {
  char errbuf[2048];
  fprintf(stderr, "%s\n", msg);
  if(errnum != 0 && av_strerror(errnum, errbuf, sizeof(errbuf)) == 0) {
    fprintf(stderr, "%s\n", errbuf);
  }
  exit(EXIT_FAILURE);
}

static void check_disk_space() {
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

  //av_log_set_level(AV_LOG_DEBUG);
  av_register_all();
  avcodec_register_all();
  avformat_network_init();
  // avdevice_register_all(); //Register all devices

  fprintf(stderr, "FFmpeg initialized\n");

  init_pg_conn(argv[1]);

  cameras = select_cameras(&n_cameras);

  pthread_t *threads = (pthread_t *)malloc(n_cameras * sizeof(pthread_t));

  for(int i=0; i < n_cameras; i++) {
    if(pthread_create(&threads[i], NULL, recorder_thread, (void *)(&cameras[i])) < 0)
      error("pthread_create");
  }

  fprintf(stderr, "All cameras started\n");

  pthread_t rtsp_server_thread;
  if(pthread_create(&rtsp_server_thread, NULL, start_rtsp_server, NULL) < 0)
    error("pthread_create");
  if(pthread_detach(rtsp_server_thread) < 0)
    error("pthread_detach");

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
