/**
 * @file tls_server.c
 * @brief Phase 1 TLS server: accepts RECORD messages and sends ACKs.
 *
 * Usage:
 *   tls_server --config A|B --port N --cert srv.crt --key srv.key
 *              --store records.dat [--store-max 100000]
 *              [--bind 127.0.0.1] [--timeout-ms 5000] [--max-conns N]
 *
 * --config A pins X25519, --config B pins X25519MLKEM768 (CONTRIBUTING.md rule
 * 11). --port 0 picks a free port, printed as "listening port=N". --max-conns
 * stops the server after N connections (0 = run until killed).
 *
 * Every RECORD is committed to the durable store (store.h) before its ACK is
 * sent (rule 15); a repeated node+seq is ACKed again and changes nothing
 * (rule 16, test T05). --store is required: there is no mode that ACKs
 * without storing. If the store fails, the server stops, so that a restart
 * reopens it and reads back what is really on disk.
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
#include "store/store.h"
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
  const char *store;       /**< durable store file */
  unsigned long store_max; /**< most records the store may hold */
};

/** @brief Why a connection ended. */
enum conn_end {
  CONN_END_CLOSED,     /**< the client closed cleanly */
  CONN_END_PROTOCOL,   /**< bad message, timeout or link error */
  CONN_END_STORE_FULL, /**< a new record did not fit; it was not ACKed */
  CONN_END_STORE_FAIL, /**< the store failed; the server must stop */
};

/** @brief What one connection did. */
struct conn_stats {
  unsigned records;     /**< RECORDs ACKed */
  unsigned new_records; /**< RECORDs stored for the first time */
  unsigned duplicates;  /**< RECORDs already stored (ACKed again, T05) */
};

/** @brief Print usage to stderr. */
static void usage(const char *prog) {
  fprintf(stderr,
          "usage: %s --config A|B --port N --cert FILE --key FILE\n"
          "          --store FILE [--store-max N]\n"
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
  o->store = NULL;
  o->store_max = 100000; /* (proposed) docs/telemetry_format.md section 7 */
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
    } else if (strcmp(k, "--store") == 0) {
      o->store = v;
    } else if (strcmp(k, "--store-max") == 0) {
      if (parse_number(v, 100000000, &o->store_max) != 0 || o->store_max == 0) {
        return -1;
      }
    } else {
      return -1;
    }
  }
  return (have_config && have_port && o->cert != NULL && o->key != NULL &&
          o->store != NULL)
             ? 0
             : -1;
}

/**
 * @brief Read RECORDs from one connection, commit and ACK each until it ends.
 * @param[in]  conn   Open TLS connection.
 * @param[in]  st     Open store.
 * @param[out] stats  What the connection did.
 * @return Why the connection ended.
 */
static enum conn_end serve_messages(struct tls_conn *conn, struct store *st,
                                    struct conn_stats *stats) {
  memset(stats, 0, sizeof(*stats));
  for (;;) {
    uint8_t hdr[WIRE_HEADER_LEN];
    int rc = tls_read_exact(conn, hdr, sizeof(hdr));
    if (rc == PQC_ERR_TLS_CLOSED) {
      return CONN_END_CLOSED; /* the client finished and said goodbye */
    }
    if (rc != PQC_OK) {
      return CONN_END_PROTOCOL;
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
      return CONN_END_PROTOCOL;
    }

    /* ACK only after the record is durable (rule 15). */
    int is_new = 0;
    rc = store_insert(st, &rec, &is_new);
    if (rc == PQC_ERR_STORE_FULL) {
      /* Not ACKed, so the client keeps it. Ending the connection tells the
       * client now, instead of after its ACK timeout. */
      printf("store_full node=%u seq=%llu\n", (unsigned)rec.node,
             (unsigned long long)rec.seq);
      return CONN_END_STORE_FULL;
    }
    if (rc != PQC_OK) {
      printf("store_failed rc=%d seq=%llu\n", rc, (unsigned long long)rec.seq);
      return CONN_END_STORE_FAIL;
    }
    /* Only a new insert counts (rule 16): a duplicate is ACKed again and
     * changes nothing else. */
    if (is_new) {
      stats->new_records++;
    } else {
      stats->duplicates++;
    }

    struct wire_ack ack = {.node = rec.node, .seq = rec.seq};
    uint8_t msg[WIRE_ACK_MSG_LEN];
    size_t msg_len = 0;
    if (wire_encode_ack(&ack, msg, sizeof(msg), &msg_len) != PQC_OK ||
        tls_write_all(conn, msg, msg_len) != PQC_OK) {
      /* The record is stored; a lost ACK only means the client resends it,
       * and the resend is deduplicated. */
      return CONN_END_PROTOCOL;
    }
    stats->records++;
  }
}

/**
 * @brief Handshake and serve one accepted socket.
 * @return Why the connection ended (CONN_END_PROTOCOL for a failed handshake).
 */
static enum conn_end handle_client(struct tls_ctx *ctx, int fd,
                                   struct store *st) {
  struct tls_conn *conn = NULL;
  int rc = tls_accept(ctx, fd, &conn);
  if (rc != PQC_OK) {
    char err[160];
    tls_last_error(err, sizeof(err));
    printf("handshake_failed rc=%d err=\"%s\"\n", rc, err);
    return CONN_END_PROTOCOL;
  }
  /* The negotiated group is logged for every connection (test T02). */
  printf("connected group=%s cipher=%s\n", tls_conn_group(conn),
         tls_conn_cipher(conn));

  struct conn_stats stats;
  enum conn_end end = serve_messages(conn, st, &stats);
  uint64_t count = 0;
  store_count(st, &count); /* cannot fail: st is open */
  printf(
      "conn_done records=%u protocol_error=%d new=%u duplicates=%u "
      "count=%llu\n",
      stats.records, end == CONN_END_PROTOCOL, stats.new_records,
      stats.duplicates, (unsigned long long)count);
  tls_conn_free(conn);
  return end;
}

/**
 * @brief Open the store and log what it holds.
 * @return The open store, or NULL after printing why it could not be opened.
 */
static struct store *open_store(const struct options *o) {
  struct store *st = NULL;
  int rc = store_open(o->store, (size_t)o->store_max, &st);
  if (rc != PQC_OK) {
    /* A corrupt store is never repaired here: the file is evidence. */
    fprintf(stderr, "cannot open store %s rc=%d\n", o->store, rc);
    return NULL;
  }
  uint64_t count = 0, cut = 0;
  store_count(st, &count); /* neither can fail: st is open */
  store_truncated_bytes(st, &cut);
  /* The count is read back from disk, so a restart cannot change it (L8). A
   * nonzero cut means the last run stopped in the middle of a write. */
  printf("store path=%s count=%llu truncated_bytes=%llu\n", o->store,
         (unsigned long long)count, (unsigned long long)cut);
  return st;
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

  /* Open the store before listening: no client is accepted until every
   * record already on disk is in the dedup index. */
  struct store *st = open_store(&o);
  if (st == NULL) {
    tls_ctx_free(ctx);
    return 1;
  }

  int lfd = -1;
  uint16_t port = 0;
  if (tcp_listen(o.bind, (uint16_t)o.port, &lfd) != PQC_OK ||
      tcp_local_port(lfd, &port) != PQC_OK) {
    fprintf(stderr, "cannot listen on %s:%lu\n", o.bind, o.port);
    store_close(st);
    tls_ctx_free(ctx);
    return 1;
  }
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
    enum conn_end end = CONN_END_PROTOCOL;
    if (tcp_set_timeout_ms(fd, (unsigned)o.timeout_ms) == PQC_OK) {
      end = handle_client(ctx, fd, st);
    }
    close(fd);
    if (end == CONN_END_STORE_FAIL) {
      /* Never keep ACKing on a store that failed: a retried fsync can
       * report success with nothing on disk. */
      fprintf(stderr, "store failed; stopping\n");
      status = 1;
      break;
    }
  }

  close(lfd);
  store_close(st);
  tls_ctx_free(ctx);
  return status;
}
