/**
 * @file tcp.c
 * @brief Blocking IPv4 TCP helpers.
 */

#include "net/tcp.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "common/errors.h"

/** @brief Fill @p sa from an IPv4 literal and a port; 0 on success. */
static int make_addr(const char *addr, uint16_t port, struct sockaddr_in *sa) {
  memset(sa, 0, sizeof(*sa));
  sa->sin_family = AF_INET;
  sa->sin_port = htons(port);
  return inet_pton(AF_INET, addr, &sa->sin_addr) == 1 ? 0 : -1;
}

/** @brief Turn off Nagle's algorithm so small messages go out at once. */
static int set_nodelay(int fd) {
  int one = 1;
  return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
}

int tcp_set_timeout_ms(int fd, unsigned timeout_ms) {
  if (fd < 0) {
    return PQC_ERR_INVALID_ARG;
  }
  struct timeval tv = {.tv_sec = (time_t)(timeout_ms / 1000),
                       .tv_usec = (suseconds_t)(timeout_ms % 1000) * 1000};
  if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0 ||
      setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) != 0) {
    return PQC_ERR_IO;
  }
  return PQC_OK;
}

int tcp_listen(const char *addr, uint16_t port, int *fd) {
  if (fd == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *fd = -1;
  struct sockaddr_in sa;
  if (addr == NULL || make_addr(addr, port, &sa) != 0) {
    return PQC_ERR_INVALID_ARG;
  }
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return PQC_ERR_IO;
  }
  int one = 1;
  /* Without SO_REUSEADDR, restarting the server fails while old connections
   * sit in TIME_WAIT. */
  if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) != 0 ||
      bind(s, (struct sockaddr *)&sa, sizeof(sa)) != 0 || listen(s, 8) != 0) {
    close(s);
    return PQC_ERR_IO;
  }
  *fd = s;
  return PQC_OK;
}

int tcp_local_port(int fd, uint16_t *port) {
  if (fd < 0 || port == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  struct sockaddr_in sa;
  socklen_t len = sizeof(sa);
  if (getsockname(fd, (struct sockaddr *)&sa, &len) != 0 ||
      sa.sin_family != AF_INET) {
    return PQC_ERR_IO;
  }
  *port = ntohs(sa.sin_port);
  return PQC_OK;
}

int tcp_accept(int listen_fd, int *fd) {
  if (fd == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *fd = -1;
  if (listen_fd < 0) {
    return PQC_ERR_INVALID_ARG;
  }
  int s;
  do {
    s = accept(listen_fd, NULL, NULL);
  } while (s < 0 && errno == EINTR);
  if (s < 0) {
    return PQC_ERR_IO;
  }
  if (set_nodelay(s) != 0) {
    close(s);
    return PQC_ERR_IO;
  }
  *fd = s;
  return PQC_OK;
}

int tcp_connect(const char *addr, uint16_t port, unsigned timeout_ms, int *fd) {
  if (fd == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *fd = -1;
  struct sockaddr_in sa;
  if (addr == NULL || port == 0 || make_addr(addr, port, &sa) != 0) {
    return PQC_ERR_INVALID_ARG;
  }
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    return PQC_ERR_IO;
  }
  /* A send timeout also bounds connect() on Linux. */
  if (tcp_set_timeout_ms(s, timeout_ms) != PQC_OK || set_nodelay(s) != 0) {
    close(s);
    return PQC_ERR_IO;
  }
  if (connect(s, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
    int timed_out = (errno == EINPROGRESS || errno == EAGAIN ||
                     errno == EWOULDBLOCK || errno == ETIMEDOUT);
    close(s);
    return timed_out ? PQC_ERR_TIMEOUT : PQC_ERR_IO;
  }
  *fd = s;
  return PQC_OK;
}
