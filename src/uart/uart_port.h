#ifndef PQC_UART_PORT_H
#define PQC_UART_PORT_H

/**
 * @file uart_port.h
 * @brief A serial device opened raw (8N1, no flow control) for reading bytes
 *        from the Pico.
 *
 * Pi-side only: it uses POSIX termios and poll(). The bytes it returns go to
 * the frame parser in proto/uart_frame.h. A pseudo-terminal opens the same way
 * as a real serial port, which is how the tests stand in for the Pico.
 */

#include <stddef.h>
#include <stdint.h>

/** @brief An open serial port (opaque). */
struct uart_port;

/**
 * @brief Open and configure a serial device.
 *
 * The device is put in raw mode: no echo, no line editing, no newline or
 * parity processing, no software or hardware flow control, 8 data bits, no
 * parity, one stop bit. Bytes already waiting in the driver are discarded, so
 * the first byte read belongs to the stream from now on.
 *
 * @param[in]  path  Device path, such as /dev/serial0 or /dev/pts/3.
 * @param[in]  baud  9600, 19200, 38400, 57600, 115200 or 230400.
 * @param[out] out   The open port; NULL on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_BAD_CONFIG for a baud rate not in the list,
 *         PQC_ERR_IO if the device cannot be opened, is not a terminal, or
 *         cannot be configured.
 */
int uart_port_open(const char *path, unsigned baud, struct uart_port **out);

/**
 * @brief Read whatever bytes arrive, waiting up to a timeout for the first.
 *
 * @param[in]  port        Open port.
 * @param[out] buf         Destination.
 * @param[in]  cap         Size of @p buf in bytes; must not be 0.
 * @param[in]  timeout_ms  Longest wait for data, in milliseconds; 0 = do not
 * wait.
 * @param[out] n           Bytes read; 0 if the timeout passed with no data.
 * @return PQC_OK (also for a timeout, with *n == 0), PQC_ERR_INVALID_ARG for a
 *         NULL pointer or a zero @p cap, PQC_ERR_IO if the device failed or
 *         the other end went away.
 * @note A signal that interrupts the wait also returns PQC_OK with *n == 0, so
 *       a caller must treat 0 bytes as "nothing yet", not as the end.
 */
int uart_port_read(struct uart_port *port, uint8_t *buf, size_t cap,
                   unsigned timeout_ms, size_t *n);

/**
 * @brief The file descriptor, for a caller that waits on it with poll().
 * @param[in]  port  Open port.
 * @param[out] fd    The descriptor. It stays owned by the port: do not close
 * it.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int uart_port_fd(const struct uart_port *port, int *fd);

/**
 * @brief Close the port and free it. Safe to call with NULL.
 * @param[in] port  Port to close.
 */
void uart_port_close(struct uart_port *port);

#endif /* PQC_UART_PORT_H */
