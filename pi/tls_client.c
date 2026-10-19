/**
 * @file tls_client.c
 * @brief Phase 1 TLS client (Pi A): makes numbered RECORDs, keeps them in the
 *        persistent queue and sends them until Pi B has ACKed each one.
 *
 * Usage:
 *   tls_client --config A|B --host 127.0.0.1 --port N --ca ca.crt
 *              --queue queue.dat [--hostname relay.test] [--count N]
 *              [--rate-hz N] [--queue-max N] [--window N] [--timeout-ms N]
 *              [--backoff-ms N] [--backoff-max-ms N] [--deadline-ms N]
 *
 * --host is an IPv4 literal to connect to; --hostname is the name the server
 * certificate must match (default relay.test). --queue is required: every
 * record is written to the queue file and fsync()ed before it is sent, and
 * leaves the file only when its ACK arrives (rule 15). --count records are
 * made in this run, --rate-hz per second (0 = all at once); --count 0 only
 * sends what the queue file already holds. IMU values are zero (Phase 1).
 *
 * The client sends the oldest --window waiting records, then reads that many
 * ACKs. A failed TCP connect, a handshake that times out or hits a socket
 * error, or a lost session is retried with backoff. Any other handshake failure
 * (an alert, a failed verification, a policy mismatch) ends the run: no loop
 * may hide it, and nothing is retried with weaker settings (rules 12, 13).
 *
 * Exit status 0 only if nothing is waiting and nothing was rejected.
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
#include "queue/queue.h"
#include "tls/tls_link.h"

/** @brief Sender ID of this client ("rover A"). */
#define CLIENT_NODE 1

/** @brief Nanoseconds in a millisecond. */
#define NS_PER_MS 1000000ull
/** @brief Nanoseconds in a second. */
#define NS_PER_S 1000000000ull

/** @brief Command-line settings. */
struct options {
  enum tls_group group;         /**< pinned group from --config */
  const char *host;             /**< server IPv4 literal */
  unsigned long port;           /**< server port */
  const char *ca;               /**< pinned CA certificate (PEM) */
  const char *hostname;         /**< name the certificate must match */
  const char *queue;            /**< persistent queue file */
  unsigned long queue_max;      /**< most records waiting for an ACK */
  unsigned long count;          /**< records to make in this run */
  unsigned long rate_hz;        /**< records per second; 0 = all at once */
  unsigned long window;         /**< records sent before reading their ACKs */
  unsigned long timeout_ms;     /**< connect, send and ACK timeout */
  unsigned long backoff_ms;     /**< first wait after a failed connection */
  unsigned long backoff_max_ms; /**< longest wait between attempts */
  unsigned long deadline_ms;    /**< give up after this long; 0 = never */
};

/** @brief Outcome of one connection attempt or one batch. */
enum link_result {
  LINK_OK,    /**< it worked */
  LINK_RETRY, /**< the link failed; drop it and try again later */
  LINK_FATAL, /**< stop the run: verification failed or the queue failed */
};

/** @brief Everything the main loop keeps track of. */
struct client {
  struct options o;         /**< settings */
  struct queue *q;          /**< the persistent queue */
  struct tls_ctx *ctx;      /**< pinned client TLS settings */
  struct tls_conn *conn;    /**< current session, NULL while disconnected */
  int fd;                   /**< current socket, -1 while disconnected */
  uint64_t start_ns;        /**< monotonic time the run started */
  uint64_t period_ns;       /**< time between records; 0 = all at once */
  uint64_t next_tick_ns;    /**< when the next record is due */
  uint64_t gen_done_ns;     /**< when the last record was made; 0 = not yet */
  uint64_t retry_at_ns;     /**< earliest time for the next connect attempt */
  unsigned long backoff;    /**< wait after the next failure, in ms */
  unsigned long generated;  /**< records made so far */
  unsigned long accepted;   /**< ...that the queue took */
  unsigned long rejected;   /**< ...that it refused because it was full */
  unsigned long acked;      /**< records removed after their ACK */
  unsigned long lost_links; /**< sessions that ended in failure */
  unsigned long connect_failures; /**< connection attempts that failed */
  int fatal;                      /**< 1 once a LINK_FATAL happened */
};

/** @brief Print usage to stderr. */
static void usage(const char *prog) {
  fprintf(stderr,
          "usage: %s --config A|B --host ADDR --port N --ca FILE --queue FILE\n"
          "          [--hostname NAME] [--count N] [--rate-hz N]\n"
          "          [--queue-max N] [--window N] [--timeout-ms N]\n"
          "          [--backoff-ms N] [--backoff-max-ms N] [--deadline-ms N]\n",
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

/** @brief Parse the value of one numeric option, with its limits. */
static int parse_limited(const char *s, unsigned long min, unsigned long max,
                         unsigned long *out) {
  return (parse_number(s, max, out) == 0 && *out >= min) ? 0 : -1;
}

/** @brief Parse argv into @p o; 0 on success. */
static int parse_args(int argc, char **argv, struct options *o) {
  int have_config = 0;
  o->host = NULL;
  o->port = 0;
  o->ca = NULL;
  o->hostname = "relay.test";
  o->queue = NULL;
  o->queue_max = 10000; /* (proposed) docs/telemetry_format.md section 7 */
  o->count = 10;
  o->rate_hz = 10;      /* Build Guide gate L2 */
  o->window = 32;       /* (proposed) section 5 */
  o->timeout_ms = 2000; /* (proposed) section 5 */
  o->backoff_ms = 1000;
  o->backoff_max_ms = 10000;
  o->deadline_ms = 0;
  for (int i = 1; i < argc; i += 2) {
    if (i + 1 >= argc) {
      return -1;
    }
    const char *k = argv[i];
    const char *v = argv[i + 1];
    int bad = 0;
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
      bad = parse_limited(v, 1, 65535, &o->port);
    } else if (strcmp(k, "--ca") == 0) {
      o->ca = v;
    } else if (strcmp(k, "--hostname") == 0) {
      o->hostname = v;
    } else if (strcmp(k, "--queue") == 0) {
      o->queue = v;
    } else if (strcmp(k, "--queue-max") == 0) {
      bad = parse_limited(v, 1, 100000000, &o->queue_max);
    } else if (strcmp(k, "--count") == 0) {
      bad = parse_limited(v, 0, 1000000, &o->count);
    } else if (strcmp(k, "--rate-hz") == 0) {
      bad = parse_limited(v, 0, 10000, &o->rate_hz);
    } else if (strcmp(k, "--window") == 0) {
      bad = parse_limited(v, 1, 1000, &o->window);
    } else if (strcmp(k, "--timeout-ms") == 0) {
      bad = parse_limited(v, 1, 600000, &o->timeout_ms);
    } else if (strcmp(k, "--backoff-ms") == 0) {
      bad = parse_limited(v, 1, 600000, &o->backoff_ms);
    } else if (strcmp(k, "--backoff-max-ms") == 0) {
      bad = parse_limited(v, 1, 600000, &o->backoff_max_ms);
    } else if (strcmp(k, "--deadline-ms") == 0) {
      bad = parse_limited(v, 0, 86400000, &o->deadline_ms);
    } else {
      return -1;
    }
    if (bad) {
      return -1;
    }
  }
  if (o->backoff_max_ms < o->backoff_ms) {
    return -1;
  }
  return (have_config && o->host != NULL && o->port != 0 && o->ca != NULL &&
          o->queue != NULL)
             ? 0
             : -1;
}

/** @brief Monotonic time in nanoseconds (0 if the clock fails, which cannot
 *         happen for CLOCK_MONOTONIC on Linux). */
static uint64_t now_ns(void) {
  uint64_t t = 0;
  if (timer_now_ns(&t) != PQC_OK) {
    return 0;
  }
  return t;
}

/** @brief Wall-clock milliseconds since the Unix epoch (record ts_ms). */
static uint64_t wall_ms(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return 0;
  }
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/** @brief Sleep for @p ns nanoseconds, or less if a signal arrives. */
static void sleep_ns(uint64_t ns) {
  struct timespec ts = {.tv_sec = (time_t)(ns / NS_PER_S),
                        .tv_nsec = (long)(ns % NS_PER_S)};
  nanosleep(&ts, NULL);
}

/**
 * @brief Make every record that is due and put it in the queue.
 *
 * A rejected record (queue full) is counted, not retried: its seq is used up,
 * so the gap shows the loss (spec section 7).
 *
 * @return PQC_OK, or the queue error that ends the run.
 */
static int generate_due(struct client *c) {
  uint64_t now = now_ns();
  while (c->generated < c->o.count &&
         (c->period_ns == 0 || now >= c->next_tick_ns)) {
    /* node and seq are the queue's job; the IMU values stay zero. */
    struct wire_record rec = {.ts_ms = wall_ms()};
    int rc = queue_append(c->q, &rec);
    c->generated++;
    if (rc == PQC_OK) {
      c->accepted++;
    } else if (rc == PQC_ERR_QUEUE_FULL) {
      c->rejected++;
    } else {
      return rc;
    }
    c->next_tick_ns += c->period_ns;
  }
  if (c->generated == c->o.count && c->gen_done_ns == 0) {
    c->gen_done_ns = now_ns();
  }
  return PQC_OK;
}

/**
 * @brief Wait until @p target_ns, still making records when they are due.
 * @return PQC_OK, or the queue error that ends the run.
 */
static int wait_until(struct client *c, uint64_t target_ns) {
  for (;;) {
    int rc = generate_due(c);
    if (rc != PQC_OK) {
      return rc;
    }
    uint64_t now = now_ns();
    if (now >= target_ns) {
      return PQC_OK;
    }
    uint64_t wake = target_ns;
    if (c->generated < c->o.count && c->period_ns != 0 &&
        c->next_tick_ns < wake) {
      wake = c->next_tick_ns;
    }
    sleep_ns(wake > now ? wake - now : 0);
  }
}

/** @brief Close the session. The queue keeps every record not yet ACKed. */
static void drop_connection(struct client *c) {
  tls_conn_free(c->conn);
  c->conn = NULL;
  if (c->fd >= 0) {
    close(c->fd);
    c->fd = -1;
  }
}

/**
 * @brief Note a failure and schedule the next attempt.
 *
 * After a session that got ACKs the next attempt is immediate and the backoff
 * starts again; otherwise it waits, doubling each time up to the maximum.
 */
static void schedule_retry(struct client *c, int progressed) {
  uint64_t now = now_ns();
  if (progressed) {
    c->backoff = c->o.backoff_ms;
    c->retry_at_ns = now;
    return;
  }
  c->retry_at_ns = now + c->backoff * NS_PER_MS;
  c->backoff = (c->backoff * 2 > c->o.backoff_max_ms) ? c->o.backoff_max_ms
                                                      : c->backoff * 2;
}

/**
 * @brief Open a TCP connection and run the TLS handshake.
 * @return LINK_OK; LINK_RETRY if TCP could not connect; LINK_FATAL if the
 *         handshake failed (wrong CA, wrong hostname, group or policy
 *         mismatch). A failed handshake is never retried (rules 12, 13).
 */
static enum link_result connect_once(struct client *c) {
  int fd = -1;
  int rc = tcp_connect(c->o.host, (uint16_t)c->o.port,
                       (unsigned)c->o.timeout_ms, &fd);
  if (rc != PQC_OK) {
    printf("connect_failed rc=%d\n", rc);
    c->connect_failures++;
    return LINK_RETRY;
  }
  struct tls_conn *conn = NULL;
  rc = tls_connect(c->ctx, fd, &conn);
  if (rc != PQC_OK) {
    char err[160];
    tls_last_error(err, sizeof(err));
    close(fd);
    /* A timeout or a socket error says only that the link is bad (packet loss,
     * a stalled server): try again on a new connection. Anything else is the
     * peer's answer to our pinned settings and is final (rules 12, 13). */
    if (rc == PQC_ERR_TIMEOUT || rc == PQC_ERR_IO) {
      printf("handshake_retry rc=%d\n", rc);
      c->connect_failures++;
      return LINK_RETRY;
    }
    printf("handshake_failed rc=%d err=\"%s\"\n", rc, err);
    return LINK_FATAL;
  }
  c->fd = fd;
  c->conn = conn;
  printf("connected group=%s cipher=%s\n", tls_conn_group(conn),
         tls_conn_cipher(conn));
  return LINK_OK;
}

/** @brief Read one ACK message. @return PQC_OK or the error that ended it. */
static int read_ack(struct tls_conn *conn, struct wire_ack *ack) {
  uint8_t hdr[WIRE_HEADER_LEN];
  uint8_t payload[WIRE_MAX_PAYLOAD];
  uint8_t type = 0;
  uint16_t len = 0;
  int rc = tls_read_exact(conn, hdr, sizeof(hdr));
  if (rc == PQC_OK) {
    rc = wire_parse_header(hdr, &type, &len);
  }
  if (rc == PQC_OK && type != WIRE_TYPE_ACK) {
    rc = PQC_ERR_WIRE_BAD_TYPE;
  }
  if (rc == PQC_OK) {
    rc = tls_read_exact(conn, payload, len);
  }
  if (rc == PQC_OK) {
    rc = wire_decode_ack(payload, len, ack);
  }
  return rc;
}

/** @brief Send waiting record number @p index (0 = oldest). */
static int send_record(struct client *c, size_t index) {
  struct wire_record rec;
  uint8_t msg[WIRE_RECORD_MSG_LEN];
  size_t msg_len = 0;
  int rc = queue_get(c->q, index, &rec);
  if (rc == PQC_OK) {
    rc = wire_encode_record(&rec, msg, sizeof(msg), &msg_len);
  }
  if (rc == PQC_OK) {
    rc = tls_write_all(c->conn, msg, msg_len);
  }
  return rc;
}

/**
 * @brief Read one ACK, check it is for the oldest waiting record, and remove
 *        that record from the queue.
 *
 * Pi B ACKs in order on one connection, so anything else is a protocol error:
 * an ACK for a record we did not send is never trusted (rule 15).
 *
 * @return LINK_OK; LINK_RETRY for a link or protocol error; LINK_FATAL if the
 *         queue failed.
 */
static enum link_result take_ack(struct client *c) {
  struct wire_ack ack;
  struct wire_record oldest;
  int rc = read_ack(c->conn, &ack);
  if (rc != PQC_OK) {
    printf("link_lost rc=%d\n", rc);
    return LINK_RETRY;
  }
  if (queue_get(c->q, 0, &oldest) != PQC_OK) {
    return LINK_FATAL; /* nothing is waiting, yet an ACK was expected */
  }
  if (ack.node != oldest.node || ack.seq != oldest.seq) {
    printf("bad_ack node=%u seq=%llu expected=%llu\n", (unsigned)ack.node,
           (unsigned long long)ack.seq, (unsigned long long)oldest.seq);
    return LINK_RETRY;
  }
  int was_pending = 0;
  if (queue_ack(c->q, ack.seq, &was_pending) != PQC_OK || !was_pending) {
    return LINK_FATAL;
  }
  c->acked++;
  return LINK_OK;
}

/**
 * @brief Send the oldest waiting records (up to the window), then read their
 *        ACKs.
 * @param[out] progressed  1 if at least one ACK was taken.
 */
static enum link_result exchange_batch(struct client *c, int *progressed) {
  uint64_t waiting = 0;
  struct queue_stats st;
  queue_get_stats(c->q, &st); /* cannot fail: the queue is open */
  waiting = st.pending;
  size_t n = (waiting < c->o.window) ? (size_t)waiting : (size_t)c->o.window;

  for (size_t i = 0; i < n; i++) {
    int rc = send_record(c, i);
    if (rc != PQC_OK) {
      printf("link_lost rc=%d\n", rc);
      return LINK_RETRY;
    }
  }
  for (size_t i = 0; i < n; i++) {
    enum link_result r = take_ack(c);
    if (r != LINK_OK) {
      return r;
    }
    *progressed = 1;
  }
  return LINK_OK;
}

/** @brief Records waiting for an ACK now. */
static uint64_t waiting_now(const struct client *c) {
  struct queue_stats st;
  queue_get_stats(c->q, &st); /* cannot fail: the queue is open */
  return st.pending;
}

/**
 * @brief Run until everything is ACKed, the deadline passes, or a fatal error.
 * @return PQC_OK, or the queue error that ended the run (fatal is set for
 *         every other way a run can end badly).
 */
static int run(struct client *c) {
  uint64_t deadline =
      c->o.deadline_ms ? c->start_ns + c->o.deadline_ms * NS_PER_MS : 0;
  for (;;) {
    int rc = generate_due(c);
    if (rc != PQC_OK) {
      return rc;
    }
    uint64_t now = now_ns();
    uint64_t waiting = waiting_now(c);
    if (c->generated == c->o.count && waiting == 0) {
      return PQC_OK; /* all made, all ACKed */
    }
    if (deadline != 0 && now >= deadline) {
      return PQC_OK;
    }

    /* Nothing to send: wait for the next record, or the deadline. */
    if (waiting == 0) {
      uint64_t until = c->next_tick_ns;
      if (deadline != 0 && deadline < until) {
        until = deadline;
      }
      rc = wait_until(c, until);
      if (rc != PQC_OK) {
        return rc;
      }
      continue;
    }

    if (c->conn == NULL) {
      if (now < c->retry_at_ns) {
        uint64_t until = c->retry_at_ns;
        if (deadline != 0 && deadline < until) {
          until = deadline;
        }
        rc = wait_until(c, until);
        if (rc != PQC_OK) {
          return rc;
        }
        continue;
      }
      enum link_result r = connect_once(c);
      if (r == LINK_FATAL) {
        c->fatal = 1;
        return PQC_OK;
      }
      if (r == LINK_RETRY) {
        schedule_retry(c, 0);
      }
      continue;
    }

    int progressed = 0;
    enum link_result r = exchange_batch(c, &progressed);
    if (r == LINK_FATAL) {
      c->fatal = 1;
      return PQC_OK;
    }
    if (r == LINK_RETRY) {
      c->lost_links++;
      drop_connection(c);
      schedule_retry(c, progressed);
    }
  }
}

/** @brief Print the one-line summary of the run (spec section 7). */
static void print_summary(const struct client *c) {
  struct queue_stats st;
  queue_get_stats(c->q, &st); /* cannot fail: the queue is open */
  uint64_t end = now_ns();
  double duration_ms = (double)(end - c->start_ns) / 1e6;
  /* Drain time: from the last record made until nothing was waiting. */
  double drain_ms = -1;
  if (st.pending == 0 && c->gen_done_ns != 0) {
    drain_ms = (double)(end - c->gen_done_ns) / 1e6;
  }
  printf(
      "done generated=%lu accepted=%lu rejected=%lu acked=%lu pending=%llu "
      "peak=%llu next_seq=%llu lost_links=%lu capacity=%lu rate_hz=%lu "
      "duration_ms=%.1f drain_ms=%.1f connect_failures=%lu\n",
      c->generated, c->accepted, c->rejected, c->acked,
      (unsigned long long)st.pending, (unsigned long long)st.peak_pending,
      (unsigned long long)st.next_seq, c->lost_links, c->o.queue_max,
      c->o.rate_hz, duration_ms, drain_ms, c->connect_failures);
}

/** @brief Program entry point. */
int main(int argc, char **argv) {
  struct client c;
  memset(&c, 0, sizeof(c));
  c.fd = -1;
  if (parse_args(argc, argv, &c.o) != 0) {
    usage(argv[0]);
    return 2;
  }
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 0);

  struct tls_config cfg = {
      .group = c.o.group, .ca_file = c.o.ca, .hostname = c.o.hostname};
  char err[160];
  int rc = tls_ctx_new_client(&cfg, &c.ctx);
  if (rc != PQC_OK) {
    tls_last_error(err, sizeof(err));
    fprintf(stderr, "cannot create TLS context rc=%d: %s\n", rc, err);
    return 1;
  }

  /* Open the queue before connecting: records left by an earlier run are
   * waiting again, oldest first, before anything new is made. A corrupt queue
   * is never repaired here: the file is evidence. */
  rc = queue_open(c.o.queue, CLIENT_NODE, (size_t)c.o.queue_max, &c.q);
  if (rc != PQC_OK) {
    fprintf(stderr, "cannot open queue %s rc=%d\n", c.o.queue, rc);
    tls_ctx_free(c.ctx);
    return 1;
  }
  struct queue_stats st;
  queue_get_stats(c.q, &st); /* cannot fail: the queue is open */
  printf("queue path=%s pending=%llu next_seq=%llu truncated_bytes=%llu\n",
         c.o.queue, (unsigned long long)st.pending,
         (unsigned long long)st.next_seq,
         (unsigned long long)st.truncated_bytes);

  c.backoff = c.o.backoff_ms;
  c.period_ns = c.o.rate_hz ? NS_PER_S / c.o.rate_hz : 0;
  c.start_ns = now_ns();
  c.next_tick_ns = c.start_ns;

  rc = run(&c);
  if (rc != PQC_OK) {
    fprintf(stderr, "queue failed rc=%d\n", rc);
    c.fatal = 1;
  }
  print_summary(&c);

  uint64_t pending = waiting_now(&c);
  int status = (c.fatal || pending != 0 || c.rejected != 0) ? 1 : 0;
  drop_connection(&c);
  queue_close(c.q);
  tls_ctx_free(c.ctx);
  return status;
}
