/**
 * @file bench_handshake.c
 * @brief T10 handshake benchmark client: many full TLS 1.3 handshakes, timed.
 *
 * Usage:
 *   bench_handshake --host 127.0.0.1 --ca ca.crt --run-id ID
 *                   --out ok.csv --fail-out fail.csv
 *                   [--port-a N] [--port-b N] [--hostname relay.test]
 *                   [--successes 100] [--warmup 5] [--max-attempts N]
 *                   [--timeout-ms 5000] [--interval-ms 0] [--allow-debug]
 *
 * Connects to a configuration-A server (--port-a) and a configuration-B server
 * (--port-b), alternating A, B, A, B, ... so slow changes in load or
 * temperature affect both equally. Give only one port to measure one
 * configuration.
 *
 * Every attempt is a new TCP connection and a full handshake (no tickets, no
 * resumption). The TLS contexts are built once, before timing starts.
 *
 * Measured on THIS (client) side only. Per attempt:
 *  - latency_total: from just before connect() to the end of the handshake
 *  - latency_tls:   the TLS handshake alone (tls_connect())
 *  - client_cpu:    CPU time of this process over the same span as
 * latency_total
 *  - bytes_tx/rx:   TLS stream bytes of the handshake (no TCP/IP headers)
 *  - heap_delta:    live heap held by the established connection, via
 *                   mallinfo2(). The transient peak during the handshake is
 *                   NOT captured; the process peak RSS is printed once at the
 * end.
 *
 * Nothing is written to a file or printed inside a timed region
 * (CONTRIBUTING.md): rows are kept in memory and written after the run.
 * Successful and failed attempts go to separate files and are never mixed.
 * Existing output files are refused so a rerun cannot overwrite earlier
 * results.
 *
 * Refuses to run in a Debug build (no NDEBUG) unless --allow-debug is given:
 * Debug numbers are never reported.
 */

#include <errno.h>
#include <malloc.h>
#include <openssl/crypto.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#include "common/errors.h"
#include "metrics/timer.h"
#include "net/tcp.h"
#include "tls/tls_link.h"

/** @brief Upper bound on --max-attempts, so the row array stays small. */
#define MAX_ATTEMPTS_LIMIT 100000UL

/** @brief Command-line settings. */
struct options {
  const char *host;        /**< server IPv4 literal */
  const char *ca;          /**< pinned CA certificate (PEM) */
  const char *hostname;    /**< name the certificate must match */
  const char *run_id;      /**< label written into every row */
  const char *out;         /**< CSV for successful attempts */
  const char *fail_out;    /**< CSV for failed attempts */
  unsigned long port[2];   /**< server port for A (0) and B (1); 0 = not used */
  unsigned long successes; /**< measured successes needed per configuration */
  unsigned long warmup; /**< successes per configuration excluded from stats */
  unsigned long max_attempts;
  unsigned long timeout_ms;
  unsigned long interval_ms;
  int allow_debug; /**< 1 = permit a build without NDEBUG (smoke tests only) */
};

/** @brief One attempt, kept in memory until the run ends. */
struct row {
  unsigned attempt;      /**< 0-based attempt number over the whole run */
  int cfg;               /**< 0 = A, 1 = B */
  int warmup;            /**< 1 = excluded from statistics */
  int rc;                /**< PQC_OK or the error code */
  uint64_t t_start_ns;   /**< monotonic clock at the start of the attempt */
  uint64_t lat_total_ns; /**< connect() start to handshake end */
  uint64_t lat_tls_ns;   /**< handshake alone */
  uint64_t cpu_ns;       /**< client CPU time over lat_total */
  uint64_t tx;           /**< handshake bytes sent */
  uint64_t rx;           /**< handshake bytes received */
  int64_t heap_delta;    /**< live heap added by the open connection */
  char group[32];        /**< negotiated group as OpenSSL reports it */
  char cipher[48];       /**< negotiated cipher suite */
  char detail[96];       /**< failure text, cleaned for CSV */
};

/** @brief Name of configuration index @p cfg. */
static const char *cfg_name(int cfg) { return cfg == 0 ? "A" : "B"; }

/** @brief "release" or "debug", from NDEBUG (CMake sets it for Release). */
static const char *build_name(void) {
#ifdef NDEBUG
  return "release";
#else
  return "debug";
#endif
}

/** @brief Print usage to stderr. */
static void usage(const char *prog) {
  fprintf(
      stderr,
      "usage: %s --host ADDR --ca FILE --run-id ID --out FILE --fail-out FILE\n"
      "          [--port-a N] [--port-b N] [--hostname NAME] [--successes N]\n"
      "          [--warmup N] [--max-attempts N] [--timeout-ms N]\n"
      "          [--interval-ms N] [--allow-debug]\n",
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

/** @brief True if @p s is a safe label: letters, digits, '_', '-', '.'. */
static int is_label(const char *s) {
  if (s[0] == '\0' || strlen(s) > 63) {
    return 0;
  }
  for (; *s != '\0'; s++) {
    char c = *s;
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) {
      return 0;
    }
  }
  return 1;
}

/** @brief Parse argv into @p o; 0 on success. */
static int parse_args(int argc, char **argv, struct options *o) {
  memset(o, 0, sizeof(*o));
  o->hostname = "relay.test";
  o->successes = 100;
  o->warmup = 5;
  o->timeout_ms = 5000;
  int have_max = 0;
  for (int i = 1; i < argc;) {
    const char *k = argv[i];
    if (strcmp(k, "--allow-debug") == 0) {
      o->allow_debug = 1;
      i++;
      continue;
    }
    if (i + 1 >= argc) {
      return -1;
    }
    const char *v = argv[i + 1];
    i += 2;
    int bad = 0;
    if (strcmp(k, "--host") == 0) {
      o->host = v;
    } else if (strcmp(k, "--ca") == 0) {
      o->ca = v;
    } else if (strcmp(k, "--hostname") == 0) {
      o->hostname = v;
    } else if (strcmp(k, "--run-id") == 0) {
      o->run_id = v;
    } else if (strcmp(k, "--out") == 0) {
      o->out = v;
    } else if (strcmp(k, "--fail-out") == 0) {
      o->fail_out = v;
    } else if (strcmp(k, "--port-a") == 0) {
      bad = parse_number(v, 65535, &o->port[0]);
    } else if (strcmp(k, "--port-b") == 0) {
      bad = parse_number(v, 65535, &o->port[1]);
    } else if (strcmp(k, "--successes") == 0) {
      bad = parse_number(v, 1000000, &o->successes) || o->successes == 0;
    } else if (strcmp(k, "--warmup") == 0) {
      bad = parse_number(v, 1000, &o->warmup);
    } else if (strcmp(k, "--max-attempts") == 0) {
      bad = parse_number(v, MAX_ATTEMPTS_LIMIT, &o->max_attempts) ||
            o->max_attempts == 0;
      have_max = 1;
    } else if (strcmp(k, "--timeout-ms") == 0) {
      bad = parse_number(v, 600000, &o->timeout_ms);
    } else if (strcmp(k, "--interval-ms") == 0) {
      bad = parse_number(v, 600000, &o->interval_ms);
    } else {
      return -1;
    }
    if (bad) {
      return -1;
    }
  }
  if (o->host == NULL || o->ca == NULL || o->run_id == NULL || o->out == NULL ||
      o->fail_out == NULL || !is_label(o->run_id) ||
      (o->port[0] == 0 && o->port[1] == 0)) {
    return -1;
  }
  if (!have_max) {
    unsigned long active = (o->port[0] != 0) + (o->port[1] != 0);
    o->max_attempts = (o->successes + o->warmup) * active * 2;
  }
  return o->max_attempts <= MAX_ATTEMPTS_LIMIT ? 0 : -1;
}

/** @brief Heap bytes currently in use by this process (malloc and mmap). */
static int64_t heap_in_use(void) {
  struct mallinfo2 mi = mallinfo2();
  return (int64_t)mi.uordblks + (int64_t)mi.hblkhd;
}

/** @brief Copy @p src into @p dst with CSV-unsafe characters replaced. */
static void clean_text(char *dst, size_t dst_size, const char *src) {
  size_t i = 0;
  for (; src[i] != '\0' && i + 1 < dst_size; i++) {
    char c = src[i];
    dst[i] = (c == ',' || c == '"' || c == '\n' || c == '\r') ? ' ' : c;
  }
  dst[i] = '\0';
}

/** @brief Read and discard until the server closes, so it is ready again. */
static void wait_for_close(int fd) {
  uint8_t buf[256];
  /* Bounded: a server that never closes costs at most the socket timeout. */
  for (int i = 0; i < 16; i++) {
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n <= 0) {
      return;
    }
  }
}

/**
 * @brief Run one full handshake and fill @p r.
 *
 * Only the section between the two clock reads at the top and bottom is timed.
 * Everything else (byte counts, heap, text, waiting for the server to finish)
 * is done outside it.
 */
static void run_attempt(const struct options *o, struct tls_ctx *ctx,
                        unsigned long port, struct row *r) {
  struct tls_conn *conn = NULL;
  int fd = -1;
  uint64_t t0 = 0, t1 = 0, t2 = 0, c0 = 0, c1 = 0;
  int64_t heap0 = heap_in_use();

  int clock_bad = timer_now_ns(&t0) != PQC_OK || timer_cpu_ns(&c0) != PQC_OK;
  int rc = tcp_connect(o->host, (uint16_t)port, (unsigned)o->timeout_ms, &fd);
  clock_bad |= timer_now_ns(&t1) != PQC_OK;
  if (rc == PQC_OK) {
    rc = tls_connect(ctx, fd, &conn);
  }
  clock_bad |= timer_now_ns(&t2) != PQC_OK || timer_cpu_ns(&c1) != PQC_OK;

  r->rc = (rc == PQC_OK && clock_bad) ? PQC_ERR_INTERNAL : rc;
  r->t_start_ns = t0;
  if (r->rc == PQC_OK) {
    r->lat_total_ns = t2 - t0;
    r->lat_tls_ns = t2 - t1;
    r->cpu_ns = c1 - c0;
    if (tls_conn_bytes(conn, &r->rx, &r->tx) != PQC_OK) {
      r->rc = PQC_ERR_INTERNAL;
    }
    r->heap_delta = heap_in_use() - heap0;
    clean_text(r->group, sizeof(r->group), tls_conn_group(conn));
    clean_text(r->cipher, sizeof(r->cipher), tls_conn_cipher(conn));
  } else {
    char err[96];
    tls_last_error(err, sizeof(err));
    clean_text(r->detail, sizeof(r->detail), err);
  }

  tls_conn_free(conn);
  if (fd >= 0) {
    if (rc == PQC_OK) {
      wait_for_close(fd);
    }
    close(fd);
  }
}

/** @brief Write the successful attempts; 0 on success. */
static int write_ok(FILE *f, const struct options *o, const struct row *rows,
                    size_t n) {
  fprintf(f,
          "run_id,config,attempt,warmup,t_start_ns,latency_total_ns,"
          "latency_tls_ns,client_cpu_ns,bytes_tx,bytes_rx,heap_delta_bytes,"
          "group,cipher,verify_ok,build\n");
  for (size_t i = 0; i < n; i++) {
    const struct row *r = &rows[i];
    if (r->rc != PQC_OK) {
      continue;
    }
    fprintf(f, "%s,%s,%u,%d,%llu,%llu,%llu,%llu,%llu,%llu,%lld,%s,%s,1,%s\n",
            o->run_id, cfg_name(r->cfg), r->attempt, r->warmup,
            (unsigned long long)r->t_start_ns,
            (unsigned long long)r->lat_total_ns,
            (unsigned long long)r->lat_tls_ns, (unsigned long long)r->cpu_ns,
            (unsigned long long)r->tx, (unsigned long long)r->rx,
            (long long)r->heap_delta, r->group, r->cipher, build_name());
  }
  return ferror(f) ? -1 : 0;
}

/** @brief Write the failed attempts; 0 on success. */
static int write_fail(FILE *f, const struct options *o, const struct row *rows,
                      size_t n) {
  fprintf(f, "run_id,config,attempt,t_start_ns,rc,detail,build\n");
  for (size_t i = 0; i < n; i++) {
    const struct row *r = &rows[i];
    if (r->rc == PQC_OK) {
      continue;
    }
    fprintf(f, "%s,%s,%u,%llu,%d,%s,%s\n", o->run_id, cfg_name(r->cfg),
            r->attempt, (unsigned long long)r->t_start_ns, r->rc, r->detail,
            build_name());
  }
  return ferror(f) ? -1 : 0;
}

/** @brief Sleep for @p ms milliseconds. */
static void pause_ms(unsigned long ms) {
  struct timespec ts = {.tv_sec = (time_t)(ms / 1000),
                        .tv_nsec = (long)(ms % 1000) * 1000000L};
  nanosleep(&ts, NULL);
}

/** @brief Program entry point. */
int main(int argc, char **argv) {
  struct options o;
  if (parse_args(argc, argv, &o) != 0) {
    usage(argv[0]);
    return 2;
  }
#ifndef NDEBUG
  if (!o.allow_debug) {
    fprintf(stderr,
            "refusing to run: this is a Debug build, whose numbers are never "
            "reported. Build with -DCMAKE_BUILD_TYPE=Release.\n");
    return 2;
  }
#endif
  signal(SIGPIPE, SIG_IGN);
  setvbuf(stdout, NULL, _IOLBF, 0);

  /* "wx": fail if the file exists, so a rerun cannot overwrite a result. */
  FILE *okf = fopen(o.out, "wx");
  FILE *failf = fopen(o.fail_out, "wx");
  if (okf == NULL || failf == NULL) {
    fprintf(stderr, "cannot create %s or %s (already exists?)\n", o.out,
            o.fail_out);
    if (okf != NULL) {
      fclose(okf);
    }
    if (failf != NULL) {
      fclose(failf);
    }
    return 1;
  }

  int active[2];
  int n_active = 0;
  struct tls_ctx *ctx[2] = {NULL, NULL};
  for (int c = 0; c < 2; c++) {
    if (o.port[c] == 0) {
      continue;
    }
    struct tls_config cfg = {
        .group = c == 0 ? TLS_GROUP_X25519 : TLS_GROUP_HYBRID,
        .ca_file = o.ca,
        .hostname = o.hostname};
    if (tls_ctx_new_client(&cfg, &ctx[c]) != PQC_OK) {
      char err[96];
      tls_last_error(err, sizeof(err));
      fprintf(stderr, "cannot create TLS context for %s: %s\n", cfg_name(c),
              err);
      return 1;
    }
    active[n_active++] = c;
  }

  struct row *rows = calloc(o.max_attempts, sizeof(*rows));
  if (rows == NULL) {
    fprintf(stderr, "out of memory\n");
    return 1;
  }

  unsigned long ok_count[2] = {0, 0};
  unsigned long fail_count[2] = {0, 0};
  size_t n = 0;
  for (size_t i = 0; i < o.max_attempts; i++) {
    int done = 1;
    for (int k = 0; k < n_active; k++) {
      done &= ok_count[active[k]] >= o.warmup + o.successes;
    }
    if (done) {
      break;
    }
    int c = active[i % (size_t)n_active];
    struct row *r = &rows[n++];
    r->attempt = (unsigned)i;
    r->cfg = c;
    r->warmup = ok_count[c] < o.warmup;
    run_attempt(&o, ctx[c], o.port[c], r);
    if (r->rc == PQC_OK) {
      ok_count[c]++;
    } else {
      fail_count[c]++;
    }
    if (o.interval_ms > 0) {
      pause_ms(o.interval_ms);
    }
  }

  /* All files are written after the last timed handshake. */
  int werr = write_ok(okf, &o, rows, n) != 0;
  werr |= write_fail(failf, &o, rows, n) != 0;
  werr |= fclose(okf) != 0;
  werr |= fclose(failf) != 0;

  struct rusage ru;
  long peak_kb = getrusage(RUSAGE_SELF, &ru) == 0 ? ru.ru_maxrss : -1;
  printf("run_id=%s build=%s\n", o.run_id, build_name());
  printf("openssl_runtime=\"%s\" openssl_compiled=\"%s\"\n",
         OpenSSL_version(OPENSSL_VERSION), OPENSSL_VERSION_TEXT);
  printf("attempts=%zu warmup_per_config=%lu successes_target=%lu\n", n,
         o.warmup, o.successes);
  int complete = 1;
  for (int k = 0; k < n_active; k++) {
    int c = active[k];
    unsigned long measured =
        ok_count[c] > o.warmup ? ok_count[c] - o.warmup : 0;
    printf("config=%s measured_ok=%lu failed=%lu\n", cfg_name(c), measured,
           fail_count[c]);
    complete &= measured >= o.successes;
  }
  printf("peak_rss_kb=%ld\n", peak_kb);

  for (int c = 0; c < 2; c++) {
    tls_ctx_free(ctx[c]);
  }
  free(rows);
  return (werr || !complete) ? 1 : 0;
}
