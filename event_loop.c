#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/poll.h>
#include <sys/statvfs.h>
#include "camera.h"
#include "event_loop.h"

#define MAX_RTSP_CLIENTS 10
#define MAX_INFO_CLIENTS 4

extern int should_terminate;
extern char *store_dir;

int rtsp_server_fd = -1;
int info_server_fd = -1;

static l1 *clients = NULL;
static struct pollfd poll_table[2 + MAX_RTSP_CLIENTS + MAX_INFO_CLIENTS];
static int nfds = 0;

static time_t last_disk_space_check = 0;
static void check_disk_space();

void parse_rtsp_request(struct client* client);
void parse_info_request(struct client* client);

static void new_connection(int server_fd, int rtsp);

void event_loop() {
  struct pollfd *poll_entry;
  int timeout = 1000;
  int rc;
  char *ptr;
  l1 *c;

  struct client *client = NULL;

  while(!should_terminate) {
    if(time(NULL) - last_disk_space_check > 60) {
      check_disk_space();
      last_disk_space_check = time(NULL);
    }

    // fill in poll_table
    nfds = 0;
    if(rtsp_server_fd > 0) {
      poll_table[nfds].fd = rtsp_server_fd;
      poll_table[nfds].events = POLLIN;
      nfds++;
    }
    if(info_server_fd > 0) {
      poll_table[nfds].fd = info_server_fd;
      poll_table[nfds].events = POLLIN;
      nfds++;
    }
    for(c = clients; c != NULL; c = c->next) {
      client = (struct client *)c->value;
      client->poll_entry = &poll_table[nfds];
      poll_table[nfds].fd = client->fd;
      poll_table[nfds].events = POLLIN;
      nfds++;
    }

    // poll for events
    do {
      if((rc = poll(poll_table, nfds, timeout)) < 0) {
        if(errno == EINTR) continue;
        else error("poll");
      }
    } while(rc < 0);

    // accept clients
    poll_entry = poll_table;
    if(rtsp_server_fd > 0) {
      if(poll_entry->revents & POLLIN)
        new_connection(rtsp_server_fd, 1);
      poll_entry++;
    }
    if(info_server_fd > 0) {
      if(poll_table[0].revents & POLLIN)
        new_connection(info_server_fd, 0);
      poll_entry++;
    }

    // handle events
    for(c = clients; c != NULL; c = c->next) {
      client = (struct client *)c->value;

      if(client->poll_entry->revents == 0)
        continue;
      if(client->poll_entry->revents != POLLIN) {
        fprintf(stderr, "Error! revents != POLLIN\n");
        continue;
      }

      // read loop
      do {
        rc = recv(client->fd, client->buf_ptr, client->buf_end - client->buf_ptr, 0);
        if(rc < 0) {
          if(errno == EINTR) continue;
          if(errno != EWOULDBLOCK) {
            perror("recv");
            client->alive = 0;
          }
          break;
        }

        if(rc == 0) {
          client->alive = 0;
          break;
        }

        client->buf_ptr += rc;
        ptr = client->buf_ptr;

        if(ptr >= client->buf_end) {
          printf("Request too long\n");
          client->alive = 0;
          break;
        }

        // proceed request
        if((ptr >= client->buffer + 2 && !strncmp(ptr - 2, "\n\n", 2)) ||
           (ptr >= client->buffer + 4 && !strncmp(ptr - 4, "\r\n\r\n", 4))) {

          if(client->rtsp)
            parse_rtsp_request(client);
          else
            parse_info_request(client);
          break;
        }
        
        if(!client->alive)
          break;
      } while(rc > 0);

      if(!client->alive) {
        printf("Closing connection - %d\n", client->fd);
        close(client->fd);
        l1_remove(&clients, NULL, client);
      }
    }
  }
}

static void new_connection(int server_fd, int rtsp) {
  struct sockaddr_in client_addr;
  struct client *client;
  unsigned address_length = sizeof(struct sockaddr_in);
  int client_fd, flags;
  char types[2][4] = {"INFO", "RTSP"};

  do {
    if((client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &address_length)) < 0) {
      if(errno == EINTR) continue;
      if(errno != EWOULDBLOCK)
        perror("accept");
      break;
    }
    fprintf(stderr, "New incoming %s connection - %d\n", types[rtsp], client_fd);
    if((flags = fcntl(client_fd, F_GETFL, 0)) < 0)
      error("fcntl");
    if(fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0)
      error("fcntl");

    client = (struct client *)malloc(sizeof(struct client));
    client->fd = client_fd;
    client->buf_ptr = client->buffer;
    client->buf_end = client->buf_ptr + BUF_SIZE - 1;
    client->rtsp = rtsp;
    client->alive = 1;
    memcpy(&client->from_addr, &client_addr, address_length);
    l1_insert(&clients, NULL, client);
  } while(client_fd >= 0);
}

void snd(struct client *client, char *buffer) {
  int rc;
  if(client->rtsp)
    printf("RTSP OUT: %s", buffer);
  rc = send(client->fd, buffer, strlen(buffer), 0);
  if(rc < 0) {
    perror("send");
    client->alive = 0;
  }
}

void event_loop_stop() {

}

static void check_disk_space() {
  struct statvfs statvfs_buf;
  unsigned long long avail;
  if(statvfs(store_dir, &statvfs_buf) < 0) {
    perror("statvfs");
    return;
  }
  
  while((avail = (unsigned long long)statvfs_buf.f_bsize * statvfs_buf.f_bavail) < (unsigned long long)1024 * 1024 * 1024) {
    if(!db_unlink_oldest_file()) {
      fprintf(stderr, "Few disk space left, but nothing to delete.\n");
      break;
    }
  }
}
