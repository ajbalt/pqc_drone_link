/**
 * @file test_uart_port.c
 * @brief Tests for uart_port.c using a pseudo-terminal as the other end.
 *
 * The test plays the Pico: it writes to the pty master, and the port opens the
 * slave exactly as it would open /dev/serial0. The raw-mode test sends all 256
 * byte values, which a terminal in its default mode would change or swallow.
 */

#define _XOPEN_SOURCE 600 /* posix_openpt() and friends */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/errors.h"
#include "uart/uart_port.h"

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

/** @brief Create a pseudo-terminal. Returns the master; the slave's path is
 *         copied to @p slave. Returns -1 on failure. */
static int open_pty(char *slave, size_t cap) {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  if (master < 0 || grantpt(master) != 0 || unlockpt(master) != 0) {
    return -1;
  }
  const char *name = ptsname(master);
  if (name == NULL || strlen(name) >= cap) {
    return -1;
  }
  strcpy(slave, name);
  return master;
}

/** @brief Write all of @p buf to @p fd. */
static void send_all(int fd, const uint8_t *buf, size_t n) {
  size_t done = 0;
  while (done < n) {
    ssize_t w = write(fd, buf + done, n - done);
    CHECK(w > 0);
    if (w <= 0) {
      return;
    }
    done += (size_t)w;
  }
}

/** @brief Milliseconds on the monotonic clock. */
static long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/** @brief Every supported baud rate opens; others and bad paths are refused. */
static void test_open(void) {
  char slave[128];
  int master = open_pty(slave, sizeof(slave));
  CHECK(master >= 0);
  if (master < 0) {
    return;
  }
  static const unsigned bauds[] = {9600, 19200, 38400, 57600, 115200, 230400};
  for (size_t i = 0; i < sizeof(bauds) / sizeof(bauds[0]); i++) {
    struct uart_port *p = (struct uart_port *)1;
    CHECK(uart_port_open(slave, bauds[i], &p) == PQC_OK && p != NULL);
    uart_port_close(p);
  }

  struct uart_port *p = (struct uart_port *)1;
  CHECK(uart_port_open(slave, 12345, &p) == PQC_ERR_BAD_CONFIG && p == NULL);
  CHECK(uart_port_open(slave, 0, &p) == PQC_ERR_BAD_CONFIG);
  CHECK(uart_port_open(NULL, 115200, &p) == PQC_ERR_INVALID_ARG && p == NULL);
  CHECK(uart_port_open(slave, 115200, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(uart_port_open("/no/such/device", 115200, &p) == PQC_ERR_IO &&
        p == NULL);
  CHECK(uart_port_open("/tmp", 115200, &p) == PQC_ERR_IO); /* a directory */
  CHECK(uart_port_open("/dev/null", 115200, &p) == PQC_ERR_IO); /* not a tty */

  char path[] = "/tmp/test_uart_port_XXXXXX";
  int fd = mkstemp(path);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
    CHECK(uart_port_open(path, 115200, &p) == PQC_ERR_IO); /* a regular file */
    unlink(path);
  }
  uart_port_close(NULL); /* must be safe */
  close(master);
}

/** @brief A read with no data waits about the timeout and reports 0 bytes. */
static void test_timeout(void) {
  char slave[128];
  int master = open_pty(slave, sizeof(slave));
  CHECK(master >= 0);
  struct uart_port *p = NULL;
  CHECK(uart_port_open(slave, 115200, &p) == PQC_OK);
  if (p == NULL) {
    return;
  }
  uint8_t buf[16];
  size_t n = 99;
  long t0 = now_ms();
  CHECK(uart_port_read(p, buf, sizeof(buf), 80, &n) == PQC_OK && n == 0);
  long waited = now_ms() - t0;
  CHECK(waited >= 70 && waited < 2000);
  t0 = now_ms();
  CHECK(uart_port_read(p, buf, sizeof(buf), 0, &n) == PQC_OK && n == 0);
  CHECK(now_ms() - t0 < 50); /* a zero timeout does not wait */
  uart_port_close(p);
  close(master);
}

/** @brief Raw mode: all 256 byte values arrive unchanged. */
static void test_raw(void) {
  char slave[128];
  int master = open_pty(slave, sizeof(slave));
  struct uart_port *p = NULL;
  CHECK(uart_port_open(slave, 115200, &p) == PQC_OK);
  if (p == NULL || master < 0) {
    return;
  }
  uint8_t sent[256];
  for (int i = 0; i < 256; i++) {
    sent[i] =
        (uint8_t)i; /* includes 0x0D, 0x0A, 0x03, 0x11, 0x00, 0x7F, 0xFF */
  }
  send_all(master, sent, sizeof(sent));

  uint8_t got[300];
  size_t total = 0;
  for (int tries = 0; tries < 50 && total < sizeof(sent); tries++) {
    size_t n = 0;
    CHECK(uart_port_read(p, got + total, sizeof(got) - total, 100, &n) ==
          PQC_OK);
    total += n;
  }
  CHECK(total == sizeof(sent));
  CHECK(memcmp(got, sent, sizeof(sent)) == 0);
  uart_port_close(p);
  close(master);
}

/** @brief Bytes that arrived before the port was opened are discarded. */
static void test_stale_bytes(void) {
  char slave[128];
  int master = open_pty(slave, sizeof(slave));
  CHECK(master >= 0);
  if (master < 0) {
    return;
  }
  const uint8_t stale[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  send_all(master, stale, sizeof(stale));
  struct uart_port *p = NULL;
  CHECK(uart_port_open(slave, 115200, &p) == PQC_OK);
  if (p == NULL) {
    return;
  }
  uint8_t buf[16];
  size_t n = 99;
  CHECK(uart_port_read(p, buf, sizeof(buf), 100, &n) == PQC_OK && n == 0);
  uart_port_close(p);
  close(master);
}

/** @brief When the other end closes, the read ends in an error instead of
 *         waiting for ever. Linux may drop bytes still queued when the master
 *         closes, so only a correct prefix of them is required. */
static void test_peer_gone(void) {
  char slave[128];
  int master = open_pty(slave, sizeof(slave));
  struct uart_port *p = NULL;
  CHECK(uart_port_open(slave, 115200, &p) == PQC_OK);
  if (p == NULL || master < 0) {
    return;
  }
  const uint8_t last[2] = {'a', 'b'};
  send_all(master, last, sizeof(last));
  close(master);

  uint8_t buf[16];
  size_t n = 0;
  size_t total = 0;
  int rc = PQC_OK;
  for (int tries = 0; tries < 20 && rc == PQC_OK; tries++) {
    rc = uart_port_read(p, buf + total, sizeof(buf) - total, 100, &n);
    total += n;
  }
  CHECK(total <= 2 && memcmp(buf, last, total) == 0);
  CHECK(rc == PQC_ERR_IO);
  uart_port_close(p);
}

/** @brief NULL pointers and a zero capacity are refused; the descriptor is
 *         reachable. */
static void test_bad_args(void) {
  char slave[128];
  int master = open_pty(slave, sizeof(slave));
  struct uart_port *p = NULL;
  CHECK(uart_port_open(slave, 115200, &p) == PQC_OK);
  if (p == NULL || master < 0) {
    return;
  }
  uint8_t buf[4];
  size_t n = 7;
  CHECK(uart_port_read(NULL, buf, sizeof(buf), 0, &n) == PQC_ERR_INVALID_ARG);
  CHECK(n == 0);
  CHECK(uart_port_read(p, NULL, sizeof(buf), 0, &n) == PQC_ERR_INVALID_ARG);
  CHECK(uart_port_read(p, buf, 0, 0, &n) == PQC_ERR_INVALID_ARG);
  CHECK(uart_port_read(p, buf, sizeof(buf), 0, NULL) == PQC_ERR_INVALID_ARG);
  int fd = -1;
  CHECK(uart_port_fd(p, &fd) == PQC_OK && fd >= 0);
  CHECK(uart_port_fd(NULL, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(uart_port_fd(p, NULL) == PQC_ERR_INVALID_ARG);
  uart_port_close(p);
  close(master);
}

/** @brief Run every test; exit status is nonzero if any check failed. */
int main(void) {
  test_open();
  test_timeout();
  test_raw();
  test_stale_bytes();
  test_peer_gone();
  test_bad_args();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
