#include <netinet/in.h>

#define BUF_SIZE 1024

struct client {
  int fd;
  int rtsp;
  int alive;
  struct pollfd *poll_entry;
  char buffer[BUF_SIZE];
  char *buf_ptr;
  char *buf_end;
  struct sockaddr_in from_addr;
};

void event_loop();
void event_loop_stop();
void snd(struct client *client, char *buffer);
