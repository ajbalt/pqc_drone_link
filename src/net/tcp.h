#ifndef PQC_TCP_H
#define PQC_TCP_H

/**
 * @file tcp.h
 * @brief Minimal blocking IPv4 TCP helpers for the TLS client and server.
 *
 * Addresses are IPv4 literals such as "127.0.0.1": the certificate hostname is
 * checked by TLS, not by DNS, so no name lookup is needed. Sockets are
 * blocking, with send and receive timeouts, and have TCP_NODELAY set so a
 * 36-byte message is not held back waiting for more data.
 */

#include <stdint.h>

/**
 * @brief Open a listening socket.
 * @param[in]  addr  IPv4 literal to bind, e.g. "127.0.0.1" or "0.0.0.0".
 * @param[in]  port  Port to bind; 0 lets the OS pick one (see
 * tcp_local_port()).
 * @param[out] fd    Listening socket on success; -1 on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG (NULL or not an IPv4 literal),
 *         PQC_ERR_IO (socket, bind or listen failed).
 */
int tcp_listen(const char *addr, uint16_t port, int *fd);

/**
 * @brief Find the port a socket is bound to.
 * @param[in]  fd    A bound socket.
 * @param[out] port  Port in host byte order.
 * @return PQC_OK, PQC_ERR_INVALID_ARG, PQC_ERR_IO.
 */
int tcp_local_port(int fd, uint16_t *port);

/**
 * @brief Wait for and accept one connection.
 * @param[in]  listen_fd  Socket from tcp_listen().
 * @param[out] fd         Accepted socket on success; -1 on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG, PQC_ERR_IO.
 */
int tcp_accept(int listen_fd, int *fd);

/**
 * @brief Connect to a server.
 * @param[in]  addr        IPv4 literal of the server.
 * @param[in]  port        Server port, 1-65535.
 * @param[in]  timeout_ms  Connect, send and receive timeout; 0 means none.
 * @param[out] fd          Connected socket on success; -1 on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG, PQC_ERR_TIMEOUT (connect timed out),
 *         PQC_ERR_IO (refused, unreachable, ...).
 */
int tcp_connect(const char *addr, uint16_t port, unsigned timeout_ms, int *fd);

/**
 * @brief Set the send and receive timeout of a socket.
 * @param[in] fd          Open socket.
 * @param[in] timeout_ms  Timeout in milliseconds; 0 removes the timeout.
 * @return PQC_OK, PQC_ERR_INVALID_ARG, PQC_ERR_IO.
 * @note A TLS read that hits this timeout reports PQC_ERR_TIMEOUT.
 */
int tcp_set_timeout_ms(int fd, unsigned timeout_ms);

#endif /* PQC_TCP_H */
