/**
 * @file uart_port.c
 * @brief Raw serial device access with termios and poll().
 */

#include "uart/uart_port.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

#include "common/errors.h"

/** @brief An open port: just the descriptor. */
struct uart_port {
  int fd; /**< the serial device, open read-write and non-blocking */
};

/** @brief Map a baud rate to its termios constant; 0 if it is not supported. */
static speed_t speed_for(unsigned baud) {
  switch (baud) {
    case 9600:
      return B9600;
    case 19200:
      return B19200;
    case 38400:
      return B38400;
    case 57600:
      return B57600;
    case 115200:
      return B115200;
    case 230400:
      return B230400;
    default:
      return 0;
  }
}

/** @brief Put an open descriptor in raw 8N1 mode at @p speed.
 *  @return 0 on success, -1 on failure. */
static int configure_raw(int fd, speed_t speed) {
  struct termios t;
  /* tcgetattr() fails with ENOTTY on a regular file or a pipe, so a path that
   * is not a terminal is refused here. */
  if (tcgetattr(fd, &t) != 0) {
    return -1;
  }
  /* cfmakeraw() is not in strict C17 + POSIX, so the flags are cleared by
   * hand. Raw means the bytes the Pico sent are exactly the bytes we read. */
  t.c_iflag &= ~(tcflag_t)(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                           ICRNL | IXON | IXOFF);
  t.c_oflag &= ~(tcflag_t)OPOST;
  t.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
  t.c_cflag &= ~(tcflag_t)(CSIZE | PARENB | CSTOPB);
  t.c_cflag |= (tcflag_t)(CS8 | CLOCAL | CREAD);
  t.c_cc[VMIN] = 0; /* read() returns what is there; poll() does the waiting */
  t.c_cc[VTIME] = 0;
  if (cfsetispeed(&t, speed) != 0 || cfsetospeed(&t, speed) != 0) {
    return -1;
  }
  if (tcsetattr(fd, TCSANOW, &t) != 0) {
    return -1;
  }
  /* Anything received before now may be half a frame. */
  return tcflush(fd, TCIOFLUSH);
}

int uart_port_open(const char *path, unsigned baud, struct uart_port **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  if (path == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  speed_t speed = speed_for(baud);
  if (speed == 0) {
    return PQC_ERR_BAD_CONFIG;
  }
  /* O_NOCTTY: opening a serial port must not make it our controlling terminal.
   */
  int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) {
    return PQC_ERR_IO;
  }
  if (configure_raw(fd, speed) != 0) {
    close(fd);
    return PQC_ERR_IO;
  }
  struct uart_port *port = malloc(sizeof(*port));
  if (port == NULL) {
    close(fd);
    return PQC_ERR_IO;
  }
  port->fd = fd;
  *out = port;
  return PQC_OK;
}

int uart_port_read(struct uart_port *port, uint8_t *buf, size_t cap,
                   unsigned timeout_ms, size_t *n) {
  if (n == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *n = 0;
  if (port == NULL || buf == NULL || cap == 0) {
    return PQC_ERR_INVALID_ARG;
  }
  struct pollfd pfd = {.fd = port->fd, .events = POLLIN, .revents = 0};
  int ready =
      poll(&pfd, 1, (int)(timeout_ms > 2000000000u ? 2000000000u : timeout_ms));
  if (ready < 0) {
    return (errno == EINTR) ? PQC_OK : PQC_ERR_IO;
  }
  if (ready == 0) {
    return PQC_OK; /* timeout: no data yet */
  }
  /* Read before judging the hangup: if data and a hangup arrive together, the
   * data is still returned. (Linux may drop bytes that were queued when a
   * pseudo-terminal's master closed, so the last few bytes before a hangup can
   * be lost; the stream resynchronises on the next frame.) */
  ssize_t got = read(port->fd, buf, cap);
  if (got > 0) {
    *n = (size_t)got;
    return PQC_OK;
  }
  if (got < 0 && (errno == EAGAIN || errno == EINTR)) {
    return PQC_OK;
  }
  /* A read of 0 bytes or EIO after the poll means the device is gone: for a
   * pseudo-terminal the master was closed, for a USB serial adapter it was
   * unplugged. */
  return PQC_ERR_IO;
}

int uart_port_fd(const struct uart_port *port, int *fd) {
  if (port == NULL || fd == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *fd = port->fd;
  return PQC_OK;
}

void uart_port_close(struct uart_port *port) {
  if (port == NULL) {
    return;
  }
  close(port->fd);
  free(port);
}
