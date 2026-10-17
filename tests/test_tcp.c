/**
 * @file test_tcp.c
 * @brief Tests for the TCP helpers, on the loopback interface.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "common/errors.h"
#include "net/tcp.h"

/** @brief Number of failed checks; nonzero fails the test. */
static int failures = 0;

/** @brief Record a failed check and keep going, so one run shows them all. */
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/** @brief Listen, connect, accept and move bytes both ways. */
static void test_roundtrip(void) {
  int lfd = -1, cfd = -1, sfd = -1;
  uint16_t port = 0;
  CHECK(tcp_listen("127.0.0.1", 0, &lfd) == PQC_OK);
  CHECK(lfd >= 0);
  CHECK(tcp_local_port(lfd, &port) == PQC_OK);
  CHECK(port != 0);
  CHECK(tcp_connect("127.0.0.1", port, 1000, &cfd) == PQC_OK);
  CHECK(tcp_accept(lfd, &sfd) == PQC_OK);

  CHECK(write(cfd, "ping", 4) == 4);
  char buf[8] = {0};
  CHECK(read(sfd, buf, sizeof(buf)) == 4);
  CHECK(memcmp(buf, "ping", 4) == 0);
  CHECK(write(sfd, "pong", 4) == 4);
  CHECK(read(cfd, buf, sizeof(buf)) == 4);
  CHECK(memcmp(buf, "pong", 4) == 0);

  /* A receive timeout turns "nothing to read" into an error, not a hang. */
  CHECK(tcp_set_timeout_ms(cfd, 100) == PQC_OK);
  CHECK(read(cfd, buf, sizeof(buf)) < 0);

  close(cfd);
  close(sfd);
  close(lfd);
}

/** @brief Bad arguments and unreachable servers are rejected cleanly. */
static void test_bad_input(void) {
  int fd = 7;
  uint16_t port = 0;
  CHECK(tcp_listen(NULL, 0, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(fd == -1);
  CHECK(tcp_listen("127.0.0.1", 0, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_listen("not-an-address", 0, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_listen("localhost", 0, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_local_port(-1, &port) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_local_port(0, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_accept(-1, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_accept(0, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_connect(NULL, 80, 100, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_connect("127.0.0.1", 0, 100, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_connect("300.1.1.1", 80, 100, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_connect("127.0.0.1", 80, 100, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(tcp_set_timeout_ms(-1, 100) == PQC_ERR_INVALID_ARG);

  /* Find a port that is free, release it, then connect to it: refused. */
  int lfd = -1;
  CHECK(tcp_listen("127.0.0.1", 0, &lfd) == PQC_OK);
  CHECK(tcp_local_port(lfd, &port) == PQC_OK);
  close(lfd);
  CHECK(tcp_connect("127.0.0.1", port, 500, &fd) == PQC_ERR_IO);
  CHECK(fd == -1);

  /* A port that is already bound cannot be bound again. */
  int a = -1, b = -1;
  CHECK(tcp_listen("127.0.0.1", 0, &a) == PQC_OK);
  CHECK(tcp_local_port(a, &port) == PQC_OK);
  CHECK(tcp_listen("127.0.0.1", port, &b) == PQC_ERR_IO);
  close(a);
}

/** @brief Run every test and report. */
int main(void) {
  test_roundtrip();
  test_bad_input();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
