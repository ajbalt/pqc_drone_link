/**
 * @file tls_client.c
 * @brief Phase 1 TLS client: sends numbered RECORDs and waits for each ACK.
 *
 * Usage:
 *   tls_client --config A|B --host 127.0.0.1 --port N --ca ca.crt
 *              [--hostname relay.test] [--count N] [--timeout-ms 5000]
 *
 * --host is an IPv4 literal to connect to; --hostname is the name the server
 * certificate must match (default relay.test). The program sends --count
 * records (seq 1..N, IMU values zero: no sensor in Phase 1) one at a time and
 * exits 0 only if every one was ACKed with the right node and seq.
 *
 * There is no retry and no fallback: any failure ends the run (rules 12, 13).
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/errors.h"
#include "metrics/timer.h"
#include "net/tcp.h"
#include "proto/wire.h"
#include "tls/tls_link.h"

/** @brief Sender ID of this client ("rover A"). */
#define CLIENT_NODE 1

/** @brief Command-line settings. */
struct options {
  enum tls_group group;     /**< pinned group from --config */
  const char *host;         /**< server IPv4 literal */
  unsigned long port;       /**< server port */
  const char *ca;           /**< pinned CA certificate (PEM) */
  const char *hostname;     /**< name the certificate must match */
  unsigned long count;      /**< records to send */
  unsigned long timeout_ms; /**< connect, send and receive timeout */
};

/** @brief Print usage to stderr. */
static void usage(const char *prog) {
  fprintf(stderr,
          "usage: %s --config A|B --host ADDR --port N --ca FILE\n"
          "          [--hostname NAME] [--count N] [--timeout-ms N]\n",
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
  int have_config = 0;
  o->host = NULL;
  o->port = 0;
  o->ca = NULL;
  o->hostname = "relay.test";
  o->count = 10;
  o->timeout_ms = 5000;
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
    } else if (strcmp(k, "--host") == 0) {
      o->host = v;
    } else if (strcmp(k, "--port") == 0) {
      if (parse_number(v, 65535, &o->port) != 0) {
        return -1;
      }
    } else if (strcmp(k, "--ca") == 0) {
      o->ca = v;
    } else if (strcmp(k, "--hostname") == 0) {
      o->hostname = v;
    } else if (strcmp(k, "--count") == 0) {
      if (parse_number(v, 1000000, &o->count) != 0 || o->count == 0) {
        return -1;
      }
    } else if (strcmp(k, "--timeout-ms") == 0) {
      if (parse_number(v, 600000, &o->timeout_ms) != 0) {
        return -1;
      }
    } else {
      return -1;
    }
  }
  return (have_config && o->host != NULL && o->port != 0 && o->ca != NULL) ? 0
                                                                           : -1;
}

/** @brief Wall-clock milliseconds since the Unix epoch (record ts_ms). */
static uint64_t wall_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/**
 * @brief Send one RECORD and check that its ACK matches.
 * @return PQC_OK, or the error that ended the exchange.
 */
static int send_and_wait(struct tls_conn *conn, uint64_t seq) {
  struct wire_record rec = {
      .node = CLIENT_NODE, .seq = seq, .ts_ms = wall_ms()};
  uint8_t msg[WIRE_RECORD_MSG_LEN];
  size_t msg_len = 0;
  int rc = wire_encode_record(&rec, msg, sizeof(msg), &msg_len);
  if (rc == PQC_OK) {
    rc = tls_write_all(conn, msg, msg_len);
  }
  if (rc != PQC_OK) {
    return rc;
  }

  uint8_t hdr[WIRE_HEADER_LEN];
  uint8_t payload[WIRE_MAX_PAYLOAD];
  uint8_t type = 0;
  uint16_t len = 0;
  struct wire_ack ack;
  rc = tls_read_exact(conn, hdr, sizeof(hdr));
  if (rc != PQC_OK) {
    return rc;
  }
  rc = wire_parse_header(hdr, &type, &len);
  if (rc != PQC_OK) {
    return rc;
  }
  if (type != WIRE_TYPE_ACK) {
    return PQC_ERR_WIRE_BAD_TYPE;
  }
  rc = tls_read_exact(conn, payload, len);
  if (rc == PQC_OK) {
    rc = wire_decode_ack(payload, len, &ack);
  }
  if (rc != PQC_OK) {
    return rc;
  }
  /* An ACK for some other record does not count. */
  return (ack.node == rec.node && ack.seq == rec.seq) ? PQC_OK
                                                      : PQC_ERR_WIRE_BAD_FIELD;
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
      .group = o.group, .ca_file = o.ca, .hostname = o.hostname};
  struct tls_ctx *ctx = NULL;
  struct tls_conn *conn = NULL;
  char err[160];
  int fd = -1;
  int status = 1;

  int rc = tls_ctx_new_client(&cfg, &ctx);
  if (rc != PQC_OK) {
    tls_last_error(err, sizeof(err));
    fprintf(stderr, "cannot create TLS context rc=%d: %s\n", rc, err);
    return 1;
  }

  uint64_t t0 = 0, t1 = 0;
  timer_now_ns(&t0);
  rc = tcp_connect(o.host, (uint16_t)o.port, (unsigned)o.timeout_ms, &fd);
  if (rc != PQC_OK) {
    fprintf(stderr, "tcp connect failed rc=%d\n", rc);
    tls_ctx_free(ctx);
    return 1;
  }
  rc = tls_connect(ctx, fd, &conn);
  timer_now_ns(&t1);
  if (rc != PQC_OK) {
    tls_last_error(err, sizeof(err));
    printf("handshake_failed rc=%d err=\"%s\"\n", rc, err);
    goto done;
  }
  /* Handshake time here is a sanity check only, not a benchmark result. */
  printf("connected group=%s cipher=%s handshake_ms=%.2f\n",
         tls_conn_group(conn), tls_conn_cipher(conn), (double)(t1 - t0) / 1e6);

  unsigned long acked = 0;
  for (uint64_t seq = 1; seq <= o.count; seq++) {
    rc = send_and_wait(conn, seq);
    if (rc != PQC_OK) {
      printf("failed seq=%llu rc=%d\n", (unsigned long long)seq, rc);
      break;
    }
    acked++;
  }
  printf("done sent=%lu acked=%lu\n", o.count, acked);
  status = (acked == o.count) ? 0 : 1;

done:
  tls_conn_free(conn);
  if (fd >= 0) {
    close(fd);
  }
  tls_ctx_free(ctx);
  return status;
}
