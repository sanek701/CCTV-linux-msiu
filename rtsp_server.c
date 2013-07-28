#include "cameras.h"
#include "screen.h"
#include "file_reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>

#define MAX_CLIENTS 10
#define BUF_SIZE 1024
#define SERVER_PORT 8554

#define OPTIONS 1
#define DESCRIBE 2
#define SETUP 3
#define TEARDOWN 4
#define PLAY 5
#define PAUSE 6

extern int should_terminate;
extern l1 *screens;
extern pthread_mutex_t screens_lock;

static struct sockaddr_in address, client;
static int socket_fd, connection_fd;
static socklen_t address_length;
static struct pollfd fds[MAX_CLIENTS + 1];
static int nfds = 1, rc;
static int reads[MAX_CLIENTS];
static char buffer[BUF_SIZE * MAX_CLIENTS];
static int timeout = 5000;

static int rtsp_session_find_func(void *value, void *arg);
static void parse_command(char *buf, int len, int client_fd, int *close_conn);
static void snd(int fd, char *buffer, int *close_conn);
static void rtsp_server_loop();

static l1 *sessions = NULL;
static pthread_mutex_t sessions_lock;

void* rtsp_server_start(void *ptr) {
  int flags, on = 1;
  memset(&address, 0, address_length);
  memset(&fds, 0 , sizeof(fds));
  memset(&reads, 0 , sizeof(int) * MAX_CLIENTS);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(SERVER_PORT);
  address_length = sizeof(struct sockaddr_in);

  if((socket_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    error("socket");
  if(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(on)) < 0)
    error("setsockopt");
  if((flags = fcntl(socket_fd, F_GETFL, 0)) < 0)
    error("fcntl");
  if(fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0)
    error("fcntl");
  if(bind(socket_fd, (struct sockaddr *)&address, address_length) < 0)
    error("bind");
  if(listen(socket_fd, 5) < 0)
    error("listen");

  rtsp_server_loop();
  return NULL;
}

void rtsp_server_loop() {
  int current_size, close_conn, compress_array = 0;
  int i,j,k,ns,rd,flags;
  char *buf;

  address_length = sizeof(struct sockaddr_in);
  fds[0].fd = socket_fd;
  fds[0].events = POLLIN;

  while(!should_terminate) {
    if((rc = poll(fds, nfds, timeout)) < 0) {
      if(errno == EINTR) continue;
      else error("poll");
    }
    current_size = nfds;
    for (i = 0; i < current_size; i++) {
      if(fds[i].revents == 0)
        continue;
      if(fds[i].revents != POLLIN) {
        fprintf(stderr, "Error! revents = %d\n", fds[i].revents);
      }

      if(fds[i].fd == socket_fd) {

        do {
          if((connection_fd = accept(socket_fd, (struct sockaddr *)&client, &address_length)) < 0) {
            if(errno == EINTR) continue;
            if(errno != EWOULDBLOCK)
              perror("accept");
            break;
          }
          fprintf(stderr, "New incoming RTSP connection - %d\n", connection_fd);
          if((flags = fcntl(connection_fd, F_GETFL, 0)) < 0)
            error("fcntl");
          if(fcntl(connection_fd, F_SETFL, flags | O_NONBLOCK) < 0)
            error("fcntl");
          fds[nfds].fd = connection_fd;
          fds[nfds].events = POLLIN;
          nfds++;
        } while(connection_fd >= 0);

      } else {

        close_conn = 0;
        do {
          rd = reads[i-1];
          buf = buffer + (i-1) * BUF_SIZE;
          rc = recv(fds[i].fd, buf + rd, BUF_SIZE - rd, 0);
          if(rc < 0) {
            if(errno == EINTR) continue;
            if(errno != EWOULDBLOCK) {
              perror("recv");
              close_conn = 1;
            }
            break;
          }

          if(rc == 0) {
            close_conn = 1;
            break;
          }

          ns = 0;

          if(rd > 0 && buf[rd-1] == '\n')
            ns += 1;

          reads[i-1] += rc;

          for(j = rd, k = rc; k>0; k--, j++) {
            if(buf[j] == '\n')
              ns += 1;
            else if(buf[j] != '\r')
              ns = 0;
            if(ns == 2) {
              rd += rc;
              buf[rd] = '\0';

              reads[i-1] = 0;
              parse_command(buf, rd, fds[i].fd, &close_conn);
              break;
            }
          }
          
          if(close_conn) break;
        } while(rc > 0);

        if(close_conn) {
          printf("Closing connection - %d\n", fds[i].fd);
          close(fds[i].fd);
          fds[i].fd = -1;
          compress_array = 1;
        }
      }
    }

    if(compress_array) {
      for (i = 0; i < nfds; i++) {
        if (fds[i].fd == -1) {
          for(j = i; j < nfds; j++) {
            fds[j].fd = fds[j+1].fd;
          }
          nfds--;
        }
      }
      compress_array = 0;
    }
  }
}

static void parse_command(char *buf, int len, int client_fd, int *close_conn) {
  char out[128];
  char *req = buf;
  char *url, *sdp = NULL, *header;
  char *transport = NULL, *session_id = NULL, *screen_id = NULL;
  struct rtsp_session *session = NULL;
  struct screen *screen = NULL;
  int method, CSeq = 0;
  int c, h = 0;

  printf("------->\n");
  printf("%s", buf);
  printf("<-------\n");

  if(strncasecmp(req, "OPTIONS", 7) == 0) {
    method = OPTIONS; req += 7;
  } else if(strncasecmp(req, "DESCRIBE", 8) == 0) {
    method = DESCRIBE; req += 8;
  } else if(strncasecmp(req, "SETUP", 5) == 0) {
    method = SETUP; req += 5;
  } else if(strncasecmp(req, "TEARDOWN", 8) == 0) {
    method = TEARDOWN; req += 8;
  } else if(strncasecmp(req, "PLAY", 4) == 0) {
    method = PLAY; req += 4;
  } else if(strncasecmp(req, "PAUSE", 5) == 0) {
    method = PAUSE; req += 5;
  } else {
    printf("BAD request 511\n");
  }

  printf("method: %d\n", method);

  while(*req == ' ') req += 1;
  url = req;

  while(*req != ' ') req += 1;
  *req = '\0';

  if(strncasecmp(url, "rtsp://", 7) != 0) {
    printf("bad url\n");
  }

  screen_id = url + 7;
  while(*screen_id != '/') screen_id += 1;
  screen_id += 1;

  printf("url: <%s>, screen_id: %s\n", url, screen_id);

  screen = (struct screen *) l1_find(&screens, &screens_lock, &screen_find_func, screen_id);

  if(screen == NULL)
    printf("Screen not found\n");

  while(*req != '\n') req += 1;
  req += 1;

  if(strncasecmp(req, "CSeq: ", 6) == 0) {
    req += 6;
    while(*req >= '0' && *req <= '9') {
      CSeq *= 10;
      CSeq += (int)(*req) - (int)'0';
      req += 1;
    }
  } else {
    printf("NO CSeq!\n");
  }

  printf("CSeq: %d\n", CSeq);

  while(*req != '\n') req += 1;
  req += 1;

  for(c = 0; c < len; c++) {
    if(buf[c] == '\n') {
      buf[c] = '\0';

      if(h != 0) {
        header = buf + h;
        if(strncmp(header, "Transport:", 10) == 0) transport = header + 11;
        if(strncmp(header, "Session:", 8) == 0) session_id =  header + 9;
      }

      h = c + 1;
    }
  }

  if(session_id != NULL) {
    session = (struct rtsp_session *) l1_find(&sessions, &sessions_lock, &rtsp_session_find_func, session_id);
    if(session == NULL)
      printf("Session %s not found\n", session_id);
    else
      printf("Session %s found. Session screen: %s\n", session->session_id, session->screen->screen_id);
  }

  sprintf(out, "CSeq: %d\n", CSeq);

  switch(method) {
    case OPTIONS:
      snd(client_fd, "RTSP/1.0 200 OK\n", close_conn);
      snd(client_fd, out, close_conn);
      snd(client_fd, "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\n\n", close_conn);
      break;
    case DESCRIBE:
      snd(client_fd, "RTSP/1.0 200 OK\n", close_conn);
      snd(client_fd, out, close_conn);
      snd(client_fd, "Content-Type: application/sdp\n", close_conn);

      sdp = screen_create_sdp(screen);
      sprintf(out, "Content-Length: %d\n\n", strlen(sdp));
      snd(client_fd, out, close_conn);
      snd(client_fd, sdp, close_conn);
      free(sdp);
      break;
    case SETUP:
      if(strcmp(transport, "RTP/AVP;unicast;") != 0) {
        printf("Transport not supported\n");
      }

      snd(client_fd, "RTSP/1.0 200 OK\n", close_conn);
      snd(client_fd, out, close_conn);

      session = (struct rtsp_session *)malloc(sizeof(struct rtsp_session));
      session->session_id = random_string(8);
      session->screen = screen;

      l1_insert(&sessions, &sessions_lock, session);

      sprintf(out, "Session: %s\n", session->session_id);
      snd(client_fd, out, close_conn);
      snd(client_fd, "Transport: RTP/AVP;unicast;\n", close_conn);

      // select ports
      /* multiple clients - ???
        rtp_context = screen->rtp_context;
        if((ret = avio_open(&(rtp_context->pb), rtp_context->filename, AVIO_FLAG_WRITE)) < 0) {
          avformat_free_context(rtp_context);
          av_err_msg("avio_open", ret);
          return -1;
        }
        if((ret = avformat_write_header(rtp_context, NULL)) < 0) {
          avformat_free_context(rtp_context);
          av_err_msg("avformat_write_header", ret);
          return -1;
        }
      */

      break;
    case PLAY:
      screen->active = 1;

      if(screen->type == ARCHIVE) {
        if(pthread_create(&screen->worker_thread, NULL, copy_input_to_output, screen->io) < 0)
          error("pthread_create");
      } else if(screen->tmpl_size > 1) {
        if(pthread_create(&screen->worker_thread, NULL, multiple_cameras_thread, screen) < 0)
          error("pthread_create");
      }
      break;
  }
}

static void snd(int fd, char *buffer, int *close_conn) {
  int rc = send(fd, buffer, strlen(buffer), 0);
  if(rc < 0) {
    perror("send");
    *close_conn = 1;
  }
}

static int rtsp_session_find_func(void *value, void *arg) {
  struct rtsp_session *session = value;
  char* session_id = (char *)arg;
  if(strcpy(session->session_id, session_id) == 0)
    return 1;
  else
    return 0;
}
