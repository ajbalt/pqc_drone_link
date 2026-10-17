/**
 * @file tls_server.c
 * @brief Phase 1 TLS server: accepts RECORD messages and sends ACKs.
 *
 * Usage:
 *   tls_server --config A|B --port N --cert srv.crt --key srv.key
 *              [--bind 127.0.0.1] [--timeout-ms 5000] [--max-conns N]
 *
 * --config A pins X25519, --config B pins X25519MLKEM768 (CONTRIBUTING.md rule
 * 11). --port 0 picks a free port, printed as "listening port=N". --max-conns
 * stops the server after N connections (0 = run until killed).
 *
 * @warning commit_record() is a stub: nothing is stored durably yet, so an ACK
 *          from this server does NOT yet mean "committed" (rule 15). The store
 *          module replaces it.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/errors.h"
#include "net/tcp.h"
#include "proto/wire.h"
#include "tls/tls_link.h"

/** @brief The only sender ID this server accepts (docs/telemetry_format.md). */
#define KNOWN_NODE 1

/** @brief Command-line settings. */
struct options {
  enum tls_group group; /**< pinned group from --config */
  const char *bind;     /**< IPv4 literal to bind */
  unsigned long port;   /**< port, 0 = pick one */
  const char *cert;     /**< server certificate chain (PEM) */
  const char *key;      /**< server private key (PEM); never commit it */
  unsigned long timeout_ms;
  unsigned long max_conns; /**< 0 = no limit */
};

/** @brief Print usage to stderr. */
static void usage(const char *prog) {
  fprintf(stderr,
          "usage: %s --config A|B --port N --cert FILE --key FILE\n"
          "          [--bind ADDR] [--timeout-ms N] [--max-conns N]\n",
          prog);
}

/** @brief Parse a decimal number up to @p max; 0 on success. */
static int parse_number(const char *s, unsigned long max, unsigned long *out) {
  if (s[0] < '0' || s[0] > '9') {
    return -1;
  }
  char *end = NULL;
  errno = 0;
  unsigned long v = strtoul(s, &end, 10);
  if (errno != 0 || *end != '\0' || v > max) {
    return -1;
  }
  *out = v;
  return 0;
}

/** @brief Parse argv into @p o; 0 on success. */
static int parse_args(int argc, char **argv, struct options *o) {
  int have_config = 0, have_port = 0;
  o->bind = "127.0.0.1";
  o->cert = NULL;
  o->key = NULL;
  o->timeout_ms = 5000;
  o->max_conns = 0;
  o->port = 0;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) {
      return -1;
    }
    const char *k = argv[i];
    const char *v = argv[i + 1];
    if (strcmp(k, "--config") == 0) {
      if (strcmp(v, "A") == 0) {
        o->group = TLS_GROUP_X25519;
      } else if (strcmp(v, "B") == 0) {
        o->group = TLS_GROUP_HYBRID;
      } else {
        return -1;
      }
      have_config = 1;
    } else if (strcmp(k, "--port") == 0) {
      if (parse_number(v, 65535, &o->port) != 0) {
        return -1;
      }
      have_port = 1;
    } else if (strcmp(k, "--cert") == 0) {
      o->cert = v;
    } else if (strcmp(k, "--key") == 0) {
      o->key = v;
    } else if (strcmp(k, "--bind") == 0) {
      o->bind = v;
    } else if (strcmp(k, "--timeout-ms") == 0) {
      if (parse_number(v, 600000, &o->timeout_ms) != 0) {
        return -1;
      }
    } else if (strcmp(k, "--max-conns") == 0) {
      if (parse_number(v, 1000000, &o->max_conns) != 0) {
        return -1;
      }
    } else {
      return -1;
    }
  }
  return (have_config && have_port && o->cert != NULL && o->key != NULL) ? 0
                                                                         : -1;
}

/**
 * @brief Store a record. STUB: stores nothing (see the file warning).
 * @return PQC_OK when the record may be ACKed.
 */
static int commit_record(const struct wire_record *rec) {
  (void)rec;
  return PQC_OK;
}

/**
 * @brief Read RECORDs from one connection and ACK each until it ends.
 * @param[in]  conn     Open TLS connection.
 * @param[out] records  RECORDs ACKed.
 * @param[out] bad      1 if the connection ended on a protocol error.
 */
static void serve_messages(struct tls_conn *conn, unsigned *records,
                           unsigned *bad) {
  *records = 0;
  *bad = 0;
  for (;;) {
    uint8_t hdr[WIRE_HEADER_LEN];
    int rc = tls_read_exact(conn, hdr, sizeof(hdr));
    if (rc == PQC_ERR_TLS_CLOSED) {
      return; /* the client finished and said goodbye */
    }
    if (rc != PQC_OK) {
      *bad = 1;
      return;
    }

    /* Never read a payload before its announced length has been checked. */
    uint8_t type = 0;
    uint16_t len = 0;
    uint8_t payload[WIRE_MAX_PAYLOAD];
    struct wire_record rec;
    if (wire_parse_header(hdr, &type, &len) != PQC_OK ||
        type != WIRE_TYPE_RECORD ||
        tls_read_exact(conn, payload, len) != PQC_OK ||
        wire_decode_record(payload, len, &rec) != PQC_OK ||
        rec.node != KNOWN_NODE) {
      *bad = 1;
      return;
    }

    /* ACK only after the commit step succeeds (rule 15). */
    struct wire_ack ack = {.node = rec.node, .seq = rec.seq};
    uint8_t msg[WIRE_ACK_MSG_LEN];
    size_t msg_len = 0;
    if (commit_record(&rec) != PQC_OK ||
        wire_encode_ack(&ack, msg, sizeof(msg), &msg_len) != PQC_OK ||
        tls_write_all(conn, msg, msg_len) != PQC_OK) {
      *bad = 1;
      return;
    }
    (*records)++;
  }
}

/** @brief Handshake and serve one accepted socket. */
static void handle_client(struct tls_ctx *ctx, int fd) {
  struct tls_conn *conn = NULL;
  int rc = tls_accept(ctx, fd, &conn);
  if (rc != PQC_OK) {
    char err[160];
    tls_last_error(err, sizeof(err));
    printf("handshake_failed rc=%d err=\"%s\"\n", rc, err);
    return;
  }
  /* The negotiated group is logged for every connection (test T02). */
  printf("connected group=%s cipher=%s\n", tls_conn_group(conn),
         tls_conn_cipher(conn));

  unsigned records = 0, bad = 0;
  serve_messages(conn, &records, &bad);
  printf("conn_done records=%u protocol_error=%u\n", records, bad);
  tls_conn_free(conn);
}

/** @brief Program entry point. */
int main(int argc, char **argv) {
  struct options o;
  if (parse_args(argc, argv, &o) != 0) {
    usage(argv[0]);
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 0);

  struct tls_config cfg = {
      .group = o.group, .cert_file = o.cert, .key_file = o.key};
  struct tls_ctx *ctx = NULL;
  int rc = tls_ctx_new_server(&cfg, &ctx);
  if (rc != PQC_OK) {
    char err[160];
    tls_last_error(err, sizeof(err));
    fprintf(stderr, "cannot create TLS context rc=%d: %s\n", rc, err);
    return 1;
  }

  int lfd = -1;
  uint16_t port = 0;
  if (tcp_listen(o.bind, (uint16_t)o.port, &lfd) != PQC_OK ||
      tcp_local_port(lfd, &port) != PQC_OK) {
    fprintf(stderr, "cannot listen on %s:%lu\n", o.bind, o.port);
    tls_ctx_free(ctx);
    return 1;
  }
  fprintf(stderr,
          "WARNING: commit is a stub; ACK does not yet mean durably stored\n");
  printf("listening port=%u group=%s\n", (unsigned)port,
         tls_group_name(o.group));

  int status = 0;
  for (unsigned long served = 0; o.max_conns == 0 || served < o.max_conns;
       served++) {
    int fd = -1;
    if (tcp_accept(lfd, &fd) != PQC_OK) {
      fprintf(stderr, "accept failed\n");
      status = 1;
      break;
    }
    /* A silent client must not hold the server forever. */
    if (tcp_set_timeout_ms(fd, (unsigned)o.timeout_ms) == PQC_OK) {
      handle_client(ctx, fd);
    }
    close(fd);
  }

  close(lfd);
  tls_ctx_free(ctx);
  return status;
}
