#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <libavformat/avformat.h>
#include <libavutil/avstring.h>
#include "camera.h"
#include "event_loop.h"
#include "file_reader.h"
#include "screen.h"
#include "string_utils.h"

#define SERVER_PORT 8554

enum RTSPStatusCode {
  RTSP_STATUS_OK              =200, /**< OK */
  RTSP_STATUS_METHOD          =405, /**< Method Not Allowed */
  RTSP_STATUS_BANDWIDTH       =453, /**< Not Enough Bandwidth */
  RTSP_STATUS_SESSION         =454, /**< Session Not Found */
  RTSP_STATUS_STATE           =455, /**< Method Not Valid in This State */
  RTSP_STATUS_AGGREGATE       =459, /**< Aggregate operation not allowed */
  RTSP_STATUS_ONLY_AGGREGATE  =460, /**< Only aggregate operation allowed */
  RTSP_STATUS_TRANSPORT       =461, /**< Unsupported transport */
  RTSP_STATUS_INTERNAL        =500, /**< Internal Server Error */
  RTSP_STATUS_SERVICE         =503, /**< Service Unavailable */
  RTSP_STATUS_VERSION         =505, /**< RTSP Version not supported */
};

enum RTSPMethod {
  DESCRIBE,
  ANNOUNCE,
  OPTIONS,
  SETUP,
  PLAY,
  PAUSE,
  TEARDOWN,
  GET_PARAMETER,
  SET_PARAMETER,
  REDIRECT,
  RECORD,
  UNKNOWN = -1,
};

struct rtsp_session {
  char *session_id;
  struct screen *screen;
};

extern int rtsp_server_fd;
extern l1 *screens;
extern pthread_mutex_t screens_lock;

static int  rtsp_session_find_func(void *value, void *arg);
static void rtsp_reply_header(struct client *client, int CSeq, enum RTSPStatusCode error_number);
static void rtsp_reply_error(struct client *client, int CSeq, enum RTSPStatusCode error_number);

static l1 *sessions = NULL;
static pthread_mutex_t sessions_lock;

void rtsp_server_start() {
  int socket_fd, flags, on = 1;
  struct sockaddr_in address;
  int address_length = sizeof(struct sockaddr_in);

  memset(&address, 0, address_length);
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(SERVER_PORT);

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

  rtsp_server_fd = socket_fd;
}

void parse_rtsp_request(struct client* client) {
  const char *p = client->buffer, *p1, *p2;
  char cmd[32];
  char url[256];
  char path1[128];
  char protocol[32];
  char screen_id[16];
  char session_id[64];
  char transport_protocol[16];
  char lower_transport[16] = "UDP";
  char profile[16];
  char parameter[16];
  char line[1024];
  const char *l;
  char *path = path1;

  int client_port_min=0, client_port_max=0;
  int method, CSeq = 0;
  int ret;
  int len;
  int rcvd = strlen(client->buffer);

  char out[128];
  char *sdp = NULL;
  struct rtsp_session *session = NULL;
  struct screen *screen = NULL;

  //printf("------->\n");
  //printf("%s", client->buffer);
  //printf("<-------\n");

  get_word(cmd, sizeof(cmd), &p);
  get_word(url, sizeof(url), &p);
  get_word(protocol, sizeof(protocol), &p);

  if (strcmp(protocol, "RTSP/1.0") != 0)
    rtsp_reply_error(client, CSeq, RTSP_STATUS_VERSION);

  session_id[0] = '\0';
  transport_protocol[0] = '\0';
  profile[0] = '\0';

  /* parse each header line */
  /* skip to next line */
  while (*p != '\n' && *p != '\0') p++;
  if (*p == '\n') p++;

  while (*p != '\0') {
    p1 = memchr(p, '\n', rcvd - (p - client->buffer));
    if (!p1)
      break;

    p2 = p1;
    if (p2 > p && p2[-1] == '\r')
      p2--;

    /* skip empty line */
    if (p2 == p) {
      p = p1 + 1;
      continue;
    }

    len = p2 - p;
    if (len > sizeof(line) - 1)
      len = sizeof(line) - 1;
    memcpy(line, p, len);
    line[len] = '\0';
    l = (const char *)line;

    //printf("Parse header line: <%s>\n", line);

    if(av_stristart(l, "Session:", &l)) {
      get_word_sep(session_id, sizeof(session_id), ";", &l);
    } else if (av_stristart(l, "Transport:", &l)) {
      // parse first transport
      while(av_isspace(*l) && *l != '\0') l++;

      if(*l != '\0') {
        get_word_sep(transport_protocol, sizeof(transport_protocol), "/", &l);
        get_word_sep(profile, sizeof(profile), "/;,", &l);
        if(*l == '/')
          get_word_sep(lower_transport, sizeof(lower_transport), ";,", &l);
        if(*l == ';')
          l++;
        while(*l != '\0' && *l != ',') {
          get_word_sep(parameter, sizeof(parameter), "=;,", &l);
          if(!strcmp(parameter, "client_port")) {
            if(*l == '=') {
              l++;
              parse_range(&client_port_min, &client_port_max, &l);
            }
          }
          while(*l != ';' && *l != '\0' && *l != ',') l++;
          if(*l == ';') l++;
        }
      }
    } else if (av_stristart(l, "CSeq:", &l)) {
      CSeq = strtol(l, NULL, 10);
    }

    p = p1 + 1;
  }

  screen_id[0] = '\0';
  av_url_split(NULL, 0, NULL, 0, NULL, 0, NULL, path, sizeof(path1), url);
  if(*path == '/') {
    path++;
    get_word_sep(screen_id, sizeof(screen_id), "/?", (const char **)&path);
  }

  printf("url: <%s>, path: <%s>, screen_id: <%s>\n", url, path1, screen_id);
  printf("transport_protocol: <%s>, profile: <%s>, lower_transport: <%s> client_port: %d-%d\n",
    transport_protocol, profile, lower_transport, client_port_min, client_port_max);

  if(screen_id[0] != '\0') {
    screen = (struct screen *) l1_find(&screens, &screens_lock, &screen_find_func, screen_id);
    if(screen == NULL) {
      printf("Screen not found\n");
      rtsp_reply_error(client, CSeq, RTSP_STATUS_SERVICE);
      return;
    }
  }

  if(session_id[0] != '\0') {
    session = (struct rtsp_session *) l1_find(&sessions, &sessions_lock, &rtsp_session_find_func, session_id);
    if(session == NULL)
      printf("Session <%s> not found\n", session_id);
    else
      printf("Session <%s> found. Session screen: <%s>\n", session->session_id, session->screen->screen_id);
  }

  if (!strcmp(cmd, "DESCRIBE"))
    method = DESCRIBE;
  else if (!strcmp(cmd, "OPTIONS"))
    method = OPTIONS;
  else if (!strcmp(cmd, "SETUP"))
    method = SETUP;
  else if (!strcmp(cmd, "PLAY"))
    method = PLAY;
  else if (!strcmp(cmd, "PAUSE"))
    method = PAUSE;
  else if (!strcmp(cmd, "TEARDOWN"))
    method = TEARDOWN;
  else {
    rtsp_reply_error(client, CSeq, RTSP_STATUS_METHOD);
    return;
  }

  switch(method) {
    case OPTIONS:
      rtsp_reply_header(client, CSeq, RTSP_STATUS_OK);
      snd(client, "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\r\n");
      snd(client, "\r\n");
      break;

    case DESCRIBE:
      rtsp_reply_header(client, CSeq, RTSP_STATUS_OK);
      sprintf(out, "Content-Base: %s\r\n", url);
      snd(client, out);
      snd(client, "Content-Type: application/sdp\r\n");

      sdp = screen_create_sdp(screen);

      sprintf(out, "Content-Length: %d\r\n", strlen(sdp));
      snd(client, out);
      snd(client, "\r\n");
      snd(client, sdp);
      free(sdp);
      break;

    case SETUP:
      /*
      if(av_strcasecmp(transport_protocol, "RTP") != 0 ||
        av_strcasecmp(lower_transport, "UDP") != 0 ) {
        rtsp_reply_error(client, CSeq, RTSP_STATUS_TRANSPORT);
        return;
      }
      */

      sprintf(screen->rtp_context->filename, "rtp://%s:%d", inet_ntoa(client->from_addr.sin_addr), client_port_min);
      printf("Streaming to <%s>\n", screen->rtp_context->filename);

      if((ret = avio_open(&screen->rtp_context->pb, screen->rtp_context->filename, AVIO_FLAG_WRITE)) < 0) {
        avformat_free_context(screen->rtp_context);
        av_err_msg("avio_open", ret);
        rtsp_reply_error(client, CSeq, RTSP_STATUS_SESSION);
        return;
      }

      session = (struct rtsp_session *)malloc(sizeof(struct rtsp_session));
      session->session_id = random_string(8);
      session->screen = screen;

      l1_insert(&sessions, &sessions_lock, session);

      rtsp_reply_header(client, CSeq, RTSP_STATUS_OK);
      sprintf(out, "Session: %s\r\n", session->session_id);
      snd(client, out);
      sprintf(out, "Transport: RTP/AVP/UDP;unicast;client_port=%d-%d\r\n", client_port_min, client_port_max);
      snd(client, out);
      snd(client, "\r\n");
      break;

    case PLAY:
      if(session == NULL || session->screen != screen) {
        rtsp_reply_error(client, CSeq, RTSP_STATUS_SESSION);
        return;
      }

      if((ret = avformat_write_header(screen->rtp_context, NULL)) < 0) {
        avformat_free_context(screen->rtp_context);
        av_err_msg("avformat_write_header", ret);
        return;
      }

      screen->active = 1;

      if(screen->type == ARCHIVE) {
        if(pthread_create(&screen->worker_thread, NULL, copy_input_to_output, screen->io) < 0)
          error("pthread_create");
      } else if(screen->tmpl_size > 1) {
        if(pthread_create(&screen->worker_thread, NULL, multiple_cameras_thread, screen) < 0)
          error("pthread_create");
      }

      rtsp_reply_header(client, CSeq, RTSP_STATUS_OK);
      snd(client, "\r\n");
      break;

    case TEARDOWN:
      rtsp_reply_header(client, CSeq, RTSP_STATUS_OK);
      snd(client, "\r\n");
      break;
  }
}

static int rtsp_session_find_func(void *value, void *arg) {
  struct rtsp_session *session = (struct rtsp_session *)value;
  char* session_id = (char *)arg;
  if(strcmp(session->session_id, session_id) == 0)
    return 1;
  else
    return 0;
}

static void rtsp_reply_header(struct client *client, int CSeq, enum RTSPStatusCode error_number) {
  const char *str;
  time_t ti;
  struct tm *tm;
  char out[128];
  char buf2[32];

  switch(error_number) {
  case RTSP_STATUS_OK:
    str = "OK";
    break;
  case RTSP_STATUS_METHOD:
    str = "Method Not Allowed";
    break;
  case RTSP_STATUS_SESSION:
    str = "Session Not Found";
    break;
  case RTSP_STATUS_STATE:
    str = "Method Not Valid in This State";
    break;
  case RTSP_STATUS_TRANSPORT:
    str = "Unsupported transport";
    break;
  case RTSP_STATUS_INTERNAL:
    str = "Internal Server Error";
    break;
  case RTSP_STATUS_VERSION:
    str = "RTSP Version not supported";
    break;
  default:
    str = "Unknown Error";
    break;
  }

  sprintf(out, "RTSP/1.0 %d %s\r\n", error_number, str);
  snd(client, out);
  sprintf(out, "CSeq: %d\r\n", CSeq);
  snd(client, out);

  /* output GMT time */
  ti = time(NULL);
  tm = gmtime(&ti);
  strftime(buf2, sizeof(buf2), "%a, %d %b %Y %H:%M:%S", tm);
  sprintf(out, "Date: %s GMT\r\n", buf2);
  snd(client, out);
}

static void rtsp_reply_error(struct client *client, int CSeq, enum RTSPStatusCode error_number) {
  rtsp_reply_header(client, CSeq, error_number);
  snd(client, "\r\n");
}
