/**
 * @file test_sample_source.c
 * @brief Tests for sample_source.c: the gap rules, and the whole path from
 *        frame bytes on a pseudo-terminal to records and counters.
 *
 * The test plays the Pico: it builds frames with the shared encoder
 * (proto/uart_frame.h) and writes them to a pty master; the source opens the
 * slave like a real serial port.
 */

#define _XOPEN_SOURCE 600 /* posix_openpt() and friends */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/errors.h"
#include "proto/uart_frame.h"
#include "uart/sample_source.h"

/** @brief Number of failed checks; nonzero fails the test. */
static int failures = 0;

/** @brief Record a failed check and keep going, so one run shows them all. */
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/** @brief Bytes in one sample frame. */
#define FRAME_LEN (UART_SAMPLE_LEN + UART_FRAME_OVERHEAD)

/** @brief The Pico end of a test: the pty master and the slave's path. */
struct pico {
  int master;      /**< pty master, where the "Pico" writes */
  char slave[128]; /**< path of the slave, what the source opens */
};

/** @brief Create a pseudo-terminal. Returns 0 on success. */
static int pico_open(struct pico *p) {
  p->master = posix_openpt(O_RDWR | O_NOCTTY);
  if (p->master < 0 || grantpt(p->master) != 0 || unlockpt(p->master) != 0) {
    return -1;
  }
  const char *name = ptsname(p->master);
  if (name == NULL || strlen(name) >= sizeof(p->slave)) {
    return -1;
  }
  strcpy(p->slave, name);
  return 0;
}

/** @brief Write all of @p buf to the "Pico" side. */
static void pico_send(const struct pico *p, const uint8_t *buf, size_t n) {
  size_t done = 0;
  while (done < n) {
    ssize_t w = write(p->master, buf + done, n - done);
    CHECK(w > 0);
    if (w <= 0) {
      return;
    }
    done += (size_t)w;
  }
}

/** @brief Build the frame for a sample with the given pico_ms; the imu values
 *         are derived from it so each frame is recognisable. */
static void make_frame(uint32_t pico_ms, uint8_t out[FRAME_LEN]) {
  struct uart_sample s;
  s.pico_ms = pico_ms;
  for (int i = 0; i < 6; i++) {
    s.imu[i] = (int16_t)((int)(pico_ms % 1000) - 500 + i);
  }
  uint8_t payload[UART_SAMPLE_LEN];
  size_t n = 0;
  CHECK(uart_sample_pack(&s, payload) == PQC_OK);
  CHECK(uart_frame_encode(payload, sizeof(payload), out, FRAME_LEN, &n) ==
        PQC_OK);
}

/** @brief Send the frame for @p pico_ms. */
static void send_sample(const struct pico *p, uint32_t pico_ms) {
  uint8_t f[FRAME_LEN];
  make_frame(pico_ms, f);
  pico_send(p, f, sizeof(f));
}

/** @brief A source on the pico's slave with the spec's timing. */
static struct sample_source *open_source(const struct pico *p) {
  struct sample_source_config cfg = {
      .path = p->slave, .baud = 115200, .period_ms = 100, .max_gap_ms = 10000};
  struct sample_source *s = NULL;
  CHECK(sample_source_open(&cfg, &s) == PQC_OK);
  return s;
}

/** @brief Read records until none is left (waiting a little for bytes to
 *         arrive); returns how many, and stores up to @p cap of them. */
static size_t drain(struct sample_source *s, struct wire_record *recs,
                    size_t cap) {
  size_t n = 0;
  for (int guard = 0; guard < 10000; guard++) {
    struct wire_record r;
    int got = 0;
    int rc = sample_source_next(s, 40, &r, &got);
    CHECK(rc == PQC_OK);
    if (rc != PQC_OK || !got) {
      break;
    }
    if (n < cap) {
      recs[n] = r;
    }
    n++;
  }
  return n;
}

/** @brief Wall-clock milliseconds. */
static uint64_t wall_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/** @brief Milliseconds on the monotonic clock. */
static long mono_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/** @brief The gap rules, as a table. */
static void test_classify(void) {
  struct {
    uint32_t prev, cur;
    enum sample_gap kind;
    uint32_t lost;
  } rows[] = {
      {1000, 1100, SAMPLE_GAP_NONE, 0},  /* exactly on time */
      {1000, 1149, SAMPLE_GAP_NONE, 0},  /* late, but under 1.5 periods */
      {1000, 1051, SAMPLE_GAP_NONE, 0},  /* early, but over half a period */
      {1000, 1050, SAMPLE_GAP_NONE, 0},  /* exactly half a period: on time */
      {1000, 1049, SAMPLE_GAP_EARLY, 0}, /* under half a period */
      {1000, 1000, SAMPLE_GAP_EARLY, 0}, /* the same time twice */
      {1000, 1150, SAMPLE_GAP_LOST, 1},  /* 1.5 periods: one lost */
      {1000, 1200, SAMPLE_GAP_LOST, 1},  /* one lost */
      {1000, 1300, SAMPLE_GAP_LOST, 2},  /* two lost */
      {1000, 1500, SAMPLE_GAP_LOST, 4},  /* four lost */
      {1000, 10999, SAMPLE_GAP_LOST,
       99}, /* just under max_gap: lost, rounded */
      {1000, 11000, SAMPLE_GAP_RESET, 0},      /* max_gap exactly: a reset */
      {5000, 100, SAMPLE_GAP_RESET, 0},        /* the Pico restarted */
      {0xFFFFFFC8u, 0x2C, SAMPLE_GAP_NONE, 0}, /* wraps at 2^32: 100 ms */
      {0xFFFFFF00u, 0x2C, SAMPLE_GAP_LOST, 2}, /* wrap with a gap: 300 ms */
  };
  for (size_t i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
    enum sample_gap kind = SAMPLE_GAP_RESET;
    uint32_t lost = 99;
    CHECK(sample_gap_classify(rows[i].prev, rows[i].cur, 100, 10000, &kind,
                              &lost) == PQC_OK);
    if (kind != rows[i].kind || lost != rows[i].lost) {
      fprintf(stderr, "classify row %zu: got kind %d lost %u\n", i, (int)kind,
              (unsigned)lost);
    }
    CHECK(kind == rows[i].kind && lost == rows[i].lost);
  }

  enum sample_gap kind;
  uint32_t lost;
  CHECK(sample_gap_classify(0, 0, 0, 10000, &kind, &lost) ==
        PQC_ERR_BAD_CONFIG);
  CHECK(sample_gap_classify(0, 0, 100, 200, &kind, &lost) ==
        PQC_ERR_BAD_CONFIG);
  CHECK(sample_gap_classify(0, 0, 100, 201, &kind, &lost) == PQC_OK);
  CHECK(sample_gap_classify(0, 0, 100, 10000, NULL, &lost) ==
        PQC_ERR_INVALID_ARG);
  CHECK(sample_gap_classify(0, 0, 100, 10000, &kind, NULL) ==
        PQC_ERR_INVALID_ARG);
  /* Sums and products are done in 64 bits: a period of 0x70000000 ms makes
   * 2 x period and 1.5 x period larger than 32 bits. */
  CHECK(sample_gap_classify(0, 5, 0x70000000u, 0xFFFFFFFFu, &kind, &lost) ==
            PQC_OK &&
        kind == SAMPLE_GAP_EARLY);
  CHECK(sample_gap_classify(0, 0xE0000000u, 0x70000000u, 0xFFFFFFFFu, &kind,
                            &lost) == PQC_OK &&
        kind == SAMPLE_GAP_LOST && lost == 1);
  /* A threshold that is not above 2 x period is refused, even if it is huge. */
  CHECK(sample_gap_classify(0, 0, 0x80000000u, 0xFFFFFFFFu, &kind, &lost) ==
        PQC_ERR_BAD_CONFIG);
}

/** @brief Bad settings and bad devices are refused. */
static void test_open_errors(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = (struct sample_source *)1;
  struct sample_source_config good = {.path = pico.slave,
                                      .baud = 115200,
                                      .period_ms = 100,
                                      .max_gap_ms = 10000};
  CHECK(sample_source_open(NULL, &s) == PQC_ERR_INVALID_ARG && s == NULL);
  CHECK(sample_source_open(&good, NULL) == PQC_ERR_INVALID_ARG);

  struct sample_source_config bad = good;
  bad.path = NULL;
  CHECK(sample_source_open(&bad, &s) == PQC_ERR_INVALID_ARG);
  bad = good;
  bad.baud = 1234;
  CHECK(sample_source_open(&bad, &s) == PQC_ERR_BAD_CONFIG);
  bad = good;
  bad.period_ms = 0;
  CHECK(sample_source_open(&bad, &s) == PQC_ERR_BAD_CONFIG);
  bad = good;
  bad.max_gap_ms = 200;
  CHECK(sample_source_open(&bad, &s) == PQC_ERR_BAD_CONFIG);
  bad = good;
  bad.path = "/no/such/device";
  CHECK(sample_source_open(&bad, &s) == PQC_ERR_IO && s == NULL);
  sample_source_close(NULL); /* must be safe */
  close(pico.master);
}

/** @brief Five frames in one write come out as five records with the right
 *         fields; node and seq are left for the queue. */
static void test_basic(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  uint8_t stream[5 * FRAME_LEN];
  for (int i = 0; i < 5; i++) {
    make_frame(1000u + 100u * (uint32_t)i, stream + i * FRAME_LEN);
  }
  uint64_t before = wall_ms();
  pico_send(&pico, stream, sizeof(stream));

  struct wire_record recs[8];
  size_t n = drain(s, recs, 8);
  uint64_t after = wall_ms();
  CHECK(n == 5);
  for (size_t i = 0; i < n && i < 5; i++) {
    uint32_t ms = 1000u + 100u * (uint32_t)i;
    CHECK(recs[i].pico_ms == ms);
    CHECK(recs[i].node == 0 && recs[i].seq == 0); /* the queue's job */
    CHECK(recs[i].ts_ms >= before && recs[i].ts_ms <= after);
    for (int k = 0; k < 6; k++) {
      CHECK(recs[i].imu[k] == (int16_t)((int)(ms % 1000) - 500 + k));
    }
  }
  struct sample_source_stats st;
  CHECK(sample_source_get_stats(s, &st) == PQC_OK);
  CHECK(st.frames_ok == 5 && st.bad_length == 0 && st.bad_crc == 0 &&
        st.noise_bytes == 0 && st.gaps == 0 && st.samples_lost == 0 &&
        st.early == 0 && st.resets == 0);
  sample_source_close(s);
  close(pico.master);
}

/** @brief Frames dribbled in one byte at a time still come out whole. */
static void test_dribble(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  uint8_t stream[3 * FRAME_LEN];
  for (int i = 0; i < 3; i++) {
    make_frame(2000u + 100u * (uint32_t)i, stream + i * FRAME_LEN);
  }
  size_t seen = 0;
  for (size_t i = 0; i < sizeof(stream); i++) {
    pico_send(&pico, &stream[i], 1);
    struct wire_record r;
    int got = 0;
    CHECK(sample_source_next(s, 100, &r, &got) == PQC_OK);
    if (got) {
      CHECK(r.pico_ms == 2000u + 100u * (uint32_t)seen);
      seen++;
    }
  }
  CHECK(seen == 3);
  sample_source_close(s);
  close(pico.master);
}

/** @brief Noise, a frame with a wrong CRC and a frame that never arrives are
 *         counted, and the samples they cost show up as a gap. */
static void test_damage(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  uint8_t stream[8 * FRAME_LEN];
  size_t len = 0;
  uint8_t f[FRAME_LEN];
  static const uint8_t noise[] = {0x00, 0x13, 0xA5, 0xFF};

  make_frame(1000, f);
  memcpy(stream + len, f, sizeof(f));
  len += sizeof(f);
  memcpy(stream + len, noise, sizeof(noise));
  len += sizeof(noise);
  make_frame(1100, f);
  f[FRAME_LEN - 1] ^= 0x01; /* wrong CRC: sample 1100 is lost */
  memcpy(stream + len, f, sizeof(f));
  len += sizeof(f);
  /* sample 1200 is never sent */
  make_frame(1300, f);
  memcpy(stream + len, f, sizeof(f));
  len += sizeof(f);
  make_frame(1400, f);
  memcpy(stream + len, f, sizeof(f));
  len += sizeof(f);
  pico_send(&pico, stream, len);

  struct wire_record recs[8];
  size_t n = drain(s, recs, 8);
  CHECK(n == 3);
  CHECK(recs[0].pico_ms == 1000 && recs[1].pico_ms == 1300 &&
        recs[2].pico_ms == 1400);
  struct sample_source_stats st;
  CHECK(sample_source_get_stats(s, &st) == PQC_OK);
  CHECK(st.frames_ok == 3);
  CHECK(st.bad_crc == 1);
  CHECK(st.noise_bytes >= sizeof(noise) - 1);
  CHECK(st.gaps == 1 && st.samples_lost == 2); /* 1100 and 1200 */
  sample_source_close(s);
  close(pico.master);
}

/** @brief The wrap at 2^32 is not a gap; a Pico restart is a reset, not tens
 *         of millions of lost samples; samples too close are counted. */
static void test_wrap_reset_early(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  static const uint32_t times[] = {
      0xFFFFFF9Cu, 0x00000000u, /* the wrap at 2^32: 100 ms, no gap */
      100u,        200u,
      300u,        60u,  /* the Pico restarted: time went backwards */
      160u,        170u, /* 10 ms after the last: early */
      270u};
  for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
    send_sample(&pico, times[i]);
  }
  struct wire_record recs[16];
  CHECK(drain(s, recs, 16) == 9);
  struct sample_source_stats st;
  CHECK(sample_source_get_stats(s, &st) == PQC_OK);
  CHECK(st.frames_ok == 9);
  CHECK(st.gaps == 0 && st.samples_lost == 0);
  CHECK(st.resets == 1);
  CHECK(st.early == 1);
  sample_source_close(s);
  close(pico.master);
}

/** @brief Every lost sample is accounted for over a long stream: frames
 *         dropped on the cable and frames damaged on it both end up in
 *         samples_lost, damaged ones also in bad_crc. */
static void test_accounting(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  const int total = 1200;
  unsigned dropped = 0, damaged = 0, delivered = 0;
  struct wire_record r;
  for (int base = 0; base < total; base += 40) {
    uint8_t chunk[40 * FRAME_LEN];
    size_t len = 0;
    for (int i = base; i < base + 40; i++) {
      uint8_t f[FRAME_LEN];
      make_frame(1000u + 100u * (uint32_t)i, f);
      /* Never touch the first or last frame: a loss there leaves no later
       * sample to show the gap. */
      if (i != 0 && i != total - 1 && i % 7 == 3) {
        dropped++;
        continue;
      }
      if (i != 0 && i != total - 1 && i % 11 == 5) {
        f[10] ^= 0x40; /* damaged inside the payload */
        damaged++;
      }
      memcpy(chunk + len, f, sizeof(f));
      len += sizeof(f);
    }
    pico_send(&pico, chunk, len);
    for (;;) {
      int got = 0;
      CHECK(sample_source_next(s, 40, &r, &got) == PQC_OK);
      if (!got) {
        break;
      }
      delivered++;
    }
  }
  struct sample_source_stats st;
  CHECK(sample_source_get_stats(s, &st) == PQC_OK);
  CHECK(st.frames_ok == delivered);
  CHECK(delivered + dropped + damaged == (unsigned)total);
  CHECK(st.bad_crc == damaged);
  CHECK(st.samples_lost == dropped + damaged);
  CHECK(st.resets == 0 && st.early == 0);
  sample_source_close(s);
  close(pico.master);
}

/** @brief With nothing to read, a call returns about at its timeout. */
static void test_timeout(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  struct wire_record r;
  int got = 5;
  long t0 = mono_ms();
  CHECK(sample_source_next(s, 100, &r, &got) == PQC_OK && got == 0);
  long waited = mono_ms() - t0;
  CHECK(waited >= 90 && waited < 2000);
  sample_source_close(s);
  close(pico.master);
}

/** @brief When the Pico side goes away the source reports an error, and a
 *         frame it had already read is not lost. */
static void test_pico_gone(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  send_sample(&pico, 1000);
  send_sample(&pico, 1100);
  /* Let both frames cross the pseudo-terminal, so one read returns them both
   * (Linux drops bytes still queued when the master closes). */
  struct timespec pause = {.tv_sec = 0, .tv_nsec = 150 * 1000000L};
  nanosleep(&pause, NULL);
  struct wire_record r;
  int got = 0;
  CHECK(sample_source_next(s, 200, &r, &got) == PQC_OK && got == 1);
  CHECK(r.pico_ms == 1000);
  close(pico.master); /* the second frame was read together with the first */
  CHECK(sample_source_next(s, 0, &r, &got) == PQC_OK && got == 1);
  CHECK(r.pico_ms == 1100);
  int rc = PQC_OK;
  for (int i = 0; i < 20 && rc == PQC_OK; i++) {
    rc = sample_source_next(s, 50, &r, &got);
  }
  CHECK(rc == PQC_ERR_IO);
  sample_source_close(s);
}

/** @brief NULL pointers are refused and the descriptor is reachable. */
static void test_bad_args(void) {
  struct pico pico;
  CHECK(pico_open(&pico) == 0);
  struct sample_source *s = open_source(&pico);
  if (s == NULL) {
    return;
  }
  struct wire_record r;
  int got = 7;
  struct sample_source_stats st;
  int fd = -1;
  CHECK(sample_source_next(NULL, 0, &r, &got) == PQC_ERR_INVALID_ARG);
  CHECK(got == 0);
  CHECK(sample_source_next(s, 0, NULL, &got) == PQC_ERR_INVALID_ARG);
  CHECK(sample_source_next(s, 0, &r, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(sample_source_get_stats(NULL, &st) == PQC_ERR_INVALID_ARG);
  CHECK(sample_source_get_stats(s, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(sample_source_fd(s, &fd) == PQC_OK && fd >= 0);
  CHECK(sample_source_fd(NULL, &fd) == PQC_ERR_INVALID_ARG);
  CHECK(sample_source_fd(s, NULL) == PQC_ERR_INVALID_ARG);
  sample_source_close(s);
  close(pico.master);
}

/** @brief Run every test; exit status is nonzero if any check failed. */
int main(void) {
  test_classify();
  test_open_errors();
  test_basic();
  test_dribble();
  test_damage();
  test_wrap_reset_early();
  test_accounting();
  test_timeout();
  test_pico_gone();
  test_bad_args();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
