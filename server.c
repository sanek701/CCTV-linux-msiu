#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <signal.h>
#include "cameras.h"
#include "screen.h"
#include "control_socket.h"

struct camera *cameras = NULL;
int n_cameras = 0;
int should_terminate = 0;
char *store_dir;

extern int terminate_h264_to_mp4_service;
extern pthread_mutex_t screens_lock;
extern l1 *screens;

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

void av_err_msg(char *msg, int errnum) {
  char errbuf[2048];
  fprintf(stderr, "%s errnum: %d\n", msg, errnum);
  if(errnum != 0 && av_strerror(errnum, errbuf, sizeof(errbuf)) == 0) {
    fprintf(stderr, "%s\n", errbuf);
  }
}

char* random_string(int len) {
  char *str = (char*)malloc(len+1);
  for(int i=0; i<len; i++) {
    if(rand()%36 > 25)
      str[i] = (char)((int)'0' + rand()%10);
    else
      str[i] = (char)((int)'a' + rand()%26);
  }
  str[len] = '\0';
  return str;
}

int main(int argc, char** argv) {
  if(argc < 2) {
    fprintf(stderr, "specify config file\n");
    exit(EXIT_FAILURE);
  }

  control_socket_init();
  fprintf(stderr, "Control socket initialized\n");

  signal(SIGINT, terminate);

  //av_log_set_level(AV_LOG_DEBUG);
  av_register_all();
  avcodec_register_all();
  avformat_network_init();

  fprintf(stderr, "FFmpeg initialized\n");

  db_init_pg_conn(argv[1]);

  cameras = db_select_cameras(&n_cameras);

  pthread_t *threads = (pthread_t *)malloc(n_cameras * sizeof(pthread_t));

  for(int i=0; i < n_cameras; i++) {
    if(pthread_create(&threads[i], NULL, recorder_thread, (void *)(&cameras[i])) < 0)
      error("pthread_create");
  }

  fprintf(stderr, "All cameras started\n");

  if(pthread_mutex_init(&screens_lock, NULL) < 0) {
    fprintf(stderr, "pthread_mutex_init failed\n");
    exit(EXIT_FAILURE);
  }

  
  pthread_t rtsp_server_thread;
  if(pthread_create(&rtsp_server_thread, NULL, rtsp_server_start, NULL) < 0)
    error("pthread_create");

  pthread_t h264_to_mp4_thread;
  if(pthread_create(&h264_to_mp4_thread, NULL, start_h264_to_mp4_service, NULL) < 0)
    error("pthread_create");

  control_socket_loop();

  struct screen *screen;
  while((screen = (struct screen *)l1_shift(&screens, &screens_lock)) != NULL) {
    screen_destroy(screen);
  }
  pthread_mutex_destroy(&screens_lock);

  for(int i=0; i < n_cameras; i++) {
    pthread_join(threads[i], NULL);
    free(cameras[i].url);
    free(cameras[i].name);
    pthread_mutex_destroy(&cameras[i].consumers_lock);
  }

  terminate_h264_to_mp4_service = 1;
  pthread_join(h264_to_mp4_thread, NULL);
  fprintf(stderr, "h264_to_mp4_thread shutted down\n");

  pthread_join(rtsp_server_thread, NULL);
  fprintf(stderr, "rtsp_server shutted down\n");

  free(cameras);
  free(threads);

  avformat_network_deinit();
  db_close_pg_conn();
  control_socket_close();

  fprintf(stderr, "Terminated.\n");
  return (EXIT_SUCCESS);
}
