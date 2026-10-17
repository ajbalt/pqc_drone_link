#ifndef PQC_TLS_LINK_H
#define PQC_TLS_LINK_H

/**
 * @file tls_link.h
 * @brief TLS 1.3 client and server over an already-connected socket.
 *
 * A thin wrapper around OpenSSL that fixes everything the experiment must keep
 * constant (CONTRIBUTING.md crypto rules 10-14):
 *  - TLS 1.3 only (minimum = maximum).
 *  - Exactly one key-exchange group: X25519 (configuration A) or
 *    X25519MLKEM768 (configuration B), set on both client and server.
 *  - Exactly one cipher suite for both configurations: TLS_AES_128_GCM_SHA256.
 *  - No session tickets and no resumption, so every handshake is a full one.
 *  - The client verifies the server certificate against one pinned CA file and
 *    checks the hostname. Failure is fatal. There is no way to turn this off.
 *
 * After every handshake the negotiated version, group and cipher suite are
 * checked again against the pinned values, so a configuration mistake cannot
 * pass silently (rule 12).
 *
 * The functions take a connected socket and never open or close it. The caller
 * owns the socket, the timeouts (SO_RCVTIMEO / SO_SNDTIMEO) and SIGPIPE.
 *
 * @note Not thread-safe: use one connection per thread, or lock around it.
 */

#include <stddef.h>
#include <stdint.h>

/** @brief The cipher suite used by both configurations (rule 14). */
#define TLS_CIPHER_SUITE "TLS_AES_128_GCM_SHA256"

/** @brief Which key-exchange group a context is pinned to. */
enum tls_group {
  TLS_GROUP_X25519 = 1, /**< configuration A: classical X25519 only */
  TLS_GROUP_HYBRID = 2, /**< configuration B: X25519MLKEM768 only */
};

/** @brief Settings for a client or server context. */
struct tls_config {
  enum tls_group group;  /**< pinned group; set on both sides */
  const char *ca_file;   /**< client: PEM file of the one trusted CA */
  const char *hostname;  /**< client: name the certificate must match */
  const char *cert_file; /**< server: PEM certificate chain */
  const char *key_file;  /**< server: PEM private key. Never commit it */
};

struct tls_ctx;  /**< Opaque: settings shared by many connections. */
struct tls_conn; /**< Opaque: one TLS connection. */

/**
 * @brief Name OpenSSL uses for a group.
 * @param[in] group  Group to look up.
 * @return "X25519" or "X25519MLKEM768", or NULL for an unknown value.
 */
const char *tls_group_name(enum tls_group group);

/**
 * @brief Create a client context.
 * @param[in]  cfg  Uses group, ca_file and hostname.
 * @param[out] out  New context; NULL on failure. Free with tls_ctx_free().
 * @return PQC_OK, PQC_ERR_INVALID_ARG (NULL, unknown group, empty or over-long
 *         hostname), PQC_ERR_BAD_CONFIG (CA file cannot be loaded),
 *         PQC_ERR_TLS_INIT (OpenSSL refused a setting).
 * @note Only the CA file is trusted. The system trust store is never used.
 */
int tls_ctx_new_client(const struct tls_config *cfg, struct tls_ctx **out);

/**
 * @brief Create a server context.
 * @param[in]  cfg  Uses group, cert_file and key_file.
 * @param[out] out  New context; NULL on failure. Free with tls_ctx_free().
 * @return PQC_OK, PQC_ERR_INVALID_ARG (NULL or unknown group),
 *         PQC_ERR_BAD_CONFIG (certificate or key cannot be loaded, or they do
 *         not match), PQC_ERR_TLS_INIT (OpenSSL refused a setting).
 * @note The server does not ask the client for a certificate (one-way
 *       authentication, docs/telemetry_format.md section 8).
 */
int tls_ctx_new_server(const struct tls_config *cfg, struct tls_ctx **out);

/**
 * @brief Free a context. Safe to call with NULL.
 * @param[in] ctx  Context from tls_ctx_new_client() or tls_ctx_new_server().
 * @note Connections made from it must be freed first.
 */
void tls_ctx_free(struct tls_ctx *ctx);

/**
 * @brief Run the client handshake on a connected socket.
 * @param[in]  ctx  A client context.
 * @param[in]  fd   Connected, blocking socket.
 * @param[out] out  New connection; NULL on failure. Free with tls_conn_free().
 * @return PQC_OK, PQC_ERR_INVALID_ARG (NULL, bad fd, or a server context),
 *         PQC_ERR_HS_AUTH (certificate chain or hostname rejected),
 *         PQC_ERR_TLS_HANDSHAKE (any other handshake failure, such as no
 *         shared group), PQC_ERR_TLS_POLICY (negotiated values differ from
 *         the pinned ones), PQC_ERR_TLS_INIT.
 * @warning Any failure is final: the caller must not retry with a weaker group
 *          or without verification (rule 12).
 */
int tls_connect(struct tls_ctx *ctx, int fd, struct tls_conn **out);

/**
 * @brief Run the server handshake on an accepted socket.
 * @param[in]  ctx  A server context.
 * @param[in]  fd   Accepted, blocking socket.
 * @param[out] out  New connection; NULL on failure. Free with tls_conn_free().
 * @return PQC_OK, PQC_ERR_INVALID_ARG (NULL, bad fd, or a client context),
 *         PQC_ERR_TLS_HANDSHAKE, PQC_ERR_TLS_POLICY, PQC_ERR_TLS_INIT.
 */
int tls_accept(struct tls_ctx *ctx, int fd, struct tls_conn **out);

/**
 * @brief Send exactly @p len bytes.
 * @param[in] conn  Open connection.
 * @param[in] buf   Bytes to send.
 * @param[in] len   Number of bytes. Zero succeeds without sending.
 * @return PQC_OK, PQC_ERR_INVALID_ARG, PQC_ERR_TIMEOUT (send timeout),
 *         PQC_ERR_TLS_CLOSED, PQC_ERR_IO.
 * @warning The process must ignore SIGPIPE (signal(SIGPIPE, SIG_IGN)), or a
 *          write to a closed peer kills it.
 */
int tls_write_all(struct tls_conn *conn, const uint8_t *buf, size_t len);

/**
 * @brief Receive exactly @p len bytes.
 * @param[in]  conn  Open connection.
 * @param[out] buf   Destination, at least @p len bytes.
 * @param[in]  len   Number of bytes to read. Zero succeeds without reading.
 * @return PQC_OK, PQC_ERR_INVALID_ARG, PQC_ERR_TIMEOUT (receive timeout),
 *         PQC_ERR_TLS_CLOSED (peer sent close_notify), PQC_ERR_IO (socket
 *         error, or TLS rejected a record: tampering looks like this, test
 * T09).
 * @warning After PQC_ERR_TIMEOUT part of a message may already have been
 *          consumed, so close the connection instead of reading again.
 */
int tls_read_exact(struct tls_conn *conn, uint8_t *buf, size_t len);

/**
 * @brief Negotiated key-exchange group, for logs (test T02).
 * @param[in] conn  Open connection.
 * @return Group name such as "X25519MLKEM768", valid until tls_conn_free().
 *         NULL if @p conn is NULL.
 */
const char *tls_conn_group(const struct tls_conn *conn);

/**
 * @brief Negotiated cipher suite, for logs.
 * @param[in] conn  Open connection.
 * @return Suite name, valid until tls_conn_free(). NULL if @p conn is NULL.
 */
const char *tls_conn_cipher(const struct tls_conn *conn);

/**
 * @brief Send close_notify and free the connection. Safe with NULL.
 * @param[in] conn  Connection to close.
 * @note Does not close the socket.
 */
void tls_conn_free(struct tls_conn *conn);

/**
 * @brief Copy the most recent OpenSSL error text, for diagnostics.
 * @param[out] buf  Destination.
 * @param[in]  len  Size of @p buf. Always terminated if len > 0.
 * @note Every tls_* call clears the error queue when it starts, so call this
 *       straight after a failure. Prints "no error" if the queue is empty.
 */
void tls_last_error(char *buf, size_t len);

#endif /* PQC_TLS_LINK_H */
