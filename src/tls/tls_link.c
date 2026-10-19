/**
 * @file tls_link.c
 * @brief OpenSSL wrapper that pins TLS 1.3, one group and one cipher suite.
 *
 * We hold no secret material of our own here: keys and session secrets live
 * inside OpenSSL, which wipes them (crypto rule 5 applies to our buffers, and
 * we have none). Nothing in this file logs keys or secrets (rule 17).
 */

#include "tls/tls_link.h"

#include <errno.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "common/errors.h"

/** @brief Longest hostname we accept, including the terminator. */
#define TLS_HOSTNAME_MAX 256

/** @brief Settings shared by many connections: OpenSSL context and pinned
 * group. */
struct tls_ctx {
  SSL_CTX *ssl_ctx;                /**< OpenSSL settings */
  enum tls_group group;            /**< the one pinned group */
  int is_client;                   /**< 1 = client context, 0 = server */
  char hostname[TLS_HOSTNAME_MAX]; /**< client: name the certificate must match
                                    */
};

/** @brief One TLS connection: OpenSSL state and the negotiated names. */
struct tls_conn {
  SSL *ssl;        /**< OpenSSL connection state */
  char group[32];  /**< negotiated group name, copied at handshake time */
  char cipher[48]; /**< negotiated cipher suite name */
};

const char *tls_group_name(enum tls_group group) {
  switch (group) {
    case TLS_GROUP_X25519:
      return "X25519";
    case TLS_GROUP_HYBRID:
      return "X25519MLKEM768";
  }
  return NULL;
}

/**
 * @brief Apply the settings both sides share.
 * @return PQC_OK or PQC_ERR_TLS_INIT.
 */
static int configure_common(SSL_CTX *ctx, enum tls_group group) {
  /* Group names are written without '?' and never as DEFAULT, so a build that
   * lacks ML-KEM fails here instead of quietly offering something else. */
  if (SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION) != 1 ||
      SSL_CTX_set_ciphersuites(ctx, TLS_CIPHER_SUITE) != 1 ||
      SSL_CTX_set1_groups_list(ctx, tls_group_name(group)) != 1) {
    return PQC_ERR_TLS_INIT;
  }
  /* Every handshake must be a full one: no tickets, no session cache. */
  SSL_CTX_set_num_tickets(ctx, 0);
  SSL_CTX_set_options(ctx, SSL_OP_NO_TICKET);
  SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_OFF);
  return PQC_OK;
}

/** @brief Allocate a context shell with the common settings applied. */
static int ctx_new(const struct tls_config *cfg, int is_client,
                   struct tls_ctx **out) {
  *out = NULL;
  ERR_clear_error();
  struct tls_ctx *c = calloc(1, sizeof(*c));
  if (c == NULL) {
    return PQC_ERR_TLS_INIT;
  }
  c->ssl_ctx = SSL_CTX_new(TLS_method());
  if (c->ssl_ctx == NULL) {
    free(c);
    return PQC_ERR_TLS_INIT;
  }
  c->group = cfg->group;
  c->is_client = is_client;
  int rc = configure_common(c->ssl_ctx, cfg->group);
  if (rc != PQC_OK) {
    tls_ctx_free(c);
    return rc;
  }
  *out = c;
  return PQC_OK;
}

int tls_ctx_new_client(const struct tls_config *cfg, struct tls_ctx **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  if (cfg == NULL || tls_group_name(cfg->group) == NULL ||
      cfg->ca_file == NULL || cfg->hostname == NULL ||
      cfg->hostname[0] == '\0' || strlen(cfg->hostname) >= TLS_HOSTNAME_MAX) {
    return PQC_ERR_INVALID_ARG;
  }
  struct tls_ctx *c = NULL;
  int rc = ctx_new(cfg, 1, &c);
  if (rc != PQC_OK) {
    return rc;
  }
  /* Trust only the pinned CA. We never call SSL_CTX_set_default_verify_paths,
   * so the system store cannot vouch for the server (rule 13). */
  if (SSL_CTX_load_verify_file(c->ssl_ctx, cfg->ca_file) != 1) {
    tls_ctx_free(c);
    return PQC_ERR_BAD_CONFIG;
  }
  SSL_CTX_set_verify(c->ssl_ctx, SSL_VERIFY_PEER, NULL);
  memcpy(c->hostname, cfg->hostname, strlen(cfg->hostname) + 1);
  *out = c;
  return PQC_OK;
}

int tls_ctx_new_server(const struct tls_config *cfg, struct tls_ctx **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  if (cfg == NULL || tls_group_name(cfg->group) == NULL ||
      cfg->cert_file == NULL || cfg->key_file == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  struct tls_ctx *c = NULL;
  int rc = ctx_new(cfg, 0, &c);
  if (rc != PQC_OK) {
    return rc;
  }
  if (SSL_CTX_use_certificate_chain_file(c->ssl_ctx, cfg->cert_file) != 1 ||
      SSL_CTX_use_PrivateKey_file(c->ssl_ctx, cfg->key_file,
                                  SSL_FILETYPE_PEM) != 1 ||
      SSL_CTX_check_private_key(c->ssl_ctx) != 1) {
    tls_ctx_free(c);
    return PQC_ERR_BAD_CONFIG;
  }
  *out = c;
  return PQC_OK;
}

void tls_ctx_free(struct tls_ctx *ctx) {
  if (ctx == NULL) {
    return;
  }
  SSL_CTX_free(ctx->ssl_ctx);
  free(ctx);
}

/**
 * @brief Re-check what was negotiated against what we pinned.
 * @return PQC_OK, PQC_ERR_TLS_POLICY, or PQC_ERR_HS_AUTH on a client with no
 *         verified peer certificate.
 */
static int check_negotiated(const struct tls_ctx *ctx, SSL *ssl,
                            struct tls_conn *conn) {
  const char *group = SSL_get0_group_name(ssl);
  const SSL_CIPHER *cipher = SSL_get_current_cipher(ssl);
  if (group == NULL || cipher == NULL) {
    return PQC_ERR_TLS_POLICY;
  }
  snprintf(conn->group, sizeof(conn->group), "%s", group);
  snprintf(conn->cipher, sizeof(conn->cipher), "%s",
           SSL_CIPHER_get_name(cipher));

  /* Compared with the full pinned strings: a truncated copy cannot match.
   * OpenSSL reports the classical group as lowercase "x25519" but the hybrid as
   * "X25519MLKEM768", and group names are case-insensitive, so ignore case. */
  if (SSL_version(ssl) != TLS1_3_VERSION ||
      strcasecmp(group, tls_group_name(ctx->group)) != 0 ||
      strcmp(conn->cipher, TLS_CIPHER_SUITE) != 0) {
    return PQC_ERR_TLS_POLICY;
  }
  if (ctx->is_client) {
    /* SSL_VERIFY_PEER already aborts a bad handshake; this is the second,
     * independent check that a verified certificate really exists. */
    X509 *peer = SSL_get1_peer_certificate(ssl);
    int ok = (peer != NULL) && (SSL_get_verify_result(ssl) == X509_V_OK);
    X509_free(peer);
    if (!ok) {
      return PQC_ERR_HS_AUTH;
    }
  }
  return PQC_OK;
}

/**
 * @brief Turn a failed SSL_connect() or SSL_accept() into one of our codes.
 *
 * A timeout or a socket error says only that the link is bad (packet loss, a
 * stalled peer); it tells us nothing about the peer's identity or its group,
 * so the caller may try again. An alert, a failed verification or a protocol
 * error is final: retrying could hide a T03 or T04 failure (rules 12, 13).
 */
static int handshake_error(SSL *ssl, int rc, int as_client) {
  switch (SSL_get_error(ssl, rc)) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      /* The socket is blocking, so "would block" can only mean that
       * SO_RCVTIMEO or SO_SNDTIMEO expired. */
      return PQC_ERR_TIMEOUT;
    case SSL_ERROR_SYSCALL:
      return PQC_ERR_IO;
    default:
      break;
  }
  /* A bad certificate or hostname leaves a verify error behind. Tell it apart
   * from other failures so T03 and T04 are not confused. */
  if (as_client && SSL_get_verify_result(ssl) != X509_V_OK) {
    return PQC_ERR_HS_AUTH;
  }
  return PQC_ERR_TLS_HANDSHAKE;
}

/** @brief Shared body of tls_connect() and tls_accept(). */
static int handshake(struct tls_ctx *ctx, int fd, int as_client,
                     struct tls_conn **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  if (ctx == NULL || fd < 0 || ctx->is_client != as_client) {
    return PQC_ERR_INVALID_ARG;
  }
  ERR_clear_error();

  struct tls_conn *conn = calloc(1, sizeof(*conn));
  if (conn == NULL) {
    return PQC_ERR_TLS_INIT;
  }
  conn->ssl = SSL_new(ctx->ssl_ctx);
  if (conn->ssl == NULL || SSL_set_fd(conn->ssl, fd) != 1 ||
      (as_client && SSL_set1_host(conn->ssl, ctx->hostname) != 1)) {
    SSL_free(conn->ssl); /* NULL-safe; no close_notify before a handshake */
    free(conn);
    return PQC_ERR_TLS_INIT;
  }

  int rc = as_client ? SSL_connect(conn->ssl) : SSL_accept(conn->ssl);
  if (rc != 1) {
    int err = handshake_error(conn->ssl, rc, as_client);
    SSL_free(conn->ssl);
    free(conn);
    return err;
  }

  rc = check_negotiated(ctx, conn->ssl, conn);
  if (rc != PQC_OK) {
    SSL_free(conn->ssl);
    free(conn);
    return rc;
  }
  *out = conn;
  return PQC_OK;
}

int tls_connect(struct tls_ctx *ctx, int fd, struct tls_conn **out) {
  return handshake(ctx, fd, 1, out);
}

int tls_accept(struct tls_ctx *ctx, int fd, struct tls_conn **out) {
  return handshake(ctx, fd, 0, out);
}

/** @brief Turn a failed SSL_read_ex/SSL_write_ex into one of our codes. */
static int map_io_error(SSL *ssl) {
  switch (SSL_get_error(ssl, 0)) {
    case SSL_ERROR_ZERO_RETURN:
      return PQC_ERR_TLS_CLOSED;
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      /* The socket is blocking, so "would block" can only mean that
       * SO_RCVTIMEO or SO_SNDTIMEO expired. */
      return PQC_ERR_TIMEOUT;
    default:
      return PQC_ERR_IO;
  }
}

int tls_write_all(struct tls_conn *conn, const uint8_t *buf, size_t len) {
  if (conn == NULL || (buf == NULL && len > 0)) {
    return PQC_ERR_INVALID_ARG;
  }
  ERR_clear_error();
  size_t done = 0;
  while (done < len) {
    size_t n = 0;
    if (SSL_write_ex(conn->ssl, buf + done, len - done, &n) != 1) {
      return map_io_error(conn->ssl);
    }
    done += n;
  }
  return PQC_OK;
}

int tls_read_exact(struct tls_conn *conn, uint8_t *buf, size_t len) {
  if (conn == NULL || (buf == NULL && len > 0)) {
    return PQC_ERR_INVALID_ARG;
  }
  ERR_clear_error();
  size_t done = 0;
  while (done < len) {
    size_t n = 0;
    /* One TLS record can arrive in pieces, so keep reading until we have
     * exactly what the caller asked for. */
    if (SSL_read_ex(conn->ssl, buf + done, len - done, &n) != 1) {
      return map_io_error(conn->ssl);
    }
    done += n;
  }
  return PQC_OK;
}

int tls_conn_bytes(const struct tls_conn *conn, uint64_t *rx, uint64_t *tx) {
  if (conn == NULL || rx == NULL || tx == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* SSL_set_fd() gave the connection one socket BIO for both directions. Every
   * byte that passes through a BIO is counted by OpenSSL itself, so these
   * are exact, not estimates. */
  BIO *in = SSL_get_rbio(conn->ssl);
  BIO *out = SSL_get_wbio(conn->ssl);
  if (in == NULL || out == NULL) {
    return PQC_ERR_TLS_INIT;
  }
  *rx = BIO_number_read(in);
  *tx = BIO_number_written(out);
  return PQC_OK;
}

const char *tls_conn_group(const struct tls_conn *conn) {
  return conn == NULL ? NULL : conn->group;
}

const char *tls_conn_cipher(const struct tls_conn *conn) {
  return conn == NULL ? NULL : conn->cipher;
}

void tls_conn_free(struct tls_conn *conn) {
  if (conn == NULL) {
    return;
  }
  if (conn->ssl != NULL) {
    /* One attempt at close_notify; we do not wait for the peer's reply. */
    SSL_shutdown(conn->ssl);
    SSL_free(conn->ssl);
  }
  free(conn);
}

void tls_last_error(char *buf, size_t len) {
  if (buf == NULL || len == 0) {
    return;
  }
  unsigned long e = ERR_peek_last_error();
  if (e == 0) {
    snprintf(buf, len, "no error");
    return;
  }
  ERR_error_string_n(e, buf, len);
}
