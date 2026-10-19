/**
 * @file test_uart_frame.c
 * @brief Tests for uart_frame.c: CRC, the spec's example frame, packing, and a
 *        parser that must survive bit flips, lost and extra bytes, noise and
 *        false syncs without losing the frames after a damaged one.
 */

#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "proto/uart_frame.h"

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

/** @brief Most delivered frames a sink remembers. */
#define MAX_SEEN 64

/** @brief What a parser run delivered, as sample indexes (see sample_of()). */
struct sink {
  int seen[MAX_SEEN];  /**< index of each delivered frame, in order */
  size_t n;            /**< number delivered */
  unsigned bad_length; /**< UART_EVENT_BAD_LENGTH events */
  unsigned bad_crc;    /**< UART_EVENT_BAD_CRC events */
};

/** @brief The sample with index @p idx: every field differs per index. */
static struct uart_sample sample_of(int idx) {
  struct uart_sample s;
  s.pico_ms = 1000u + 100u * (uint32_t)idx;
  for (int i = 0; i < 6; i++) {
    s.imu[i] = (int16_t)(idx * 31 - 1000 + i * 17);
  }
  return s;
}

/** @brief The index a delivered sample must have, or -1 if it is not one of
 *         ours (a frame that was invented, or corrupted yet accepted). */
static int index_of(const struct uart_sample *s) {
  if (s->pico_ms < 1000u || (s->pico_ms - 1000u) % 100u != 0) {
    return -1;
  }
  int idx = (int)((s->pico_ms - 1000u) / 100u);
  struct uart_sample want = sample_of(idx);
  return memcmp(want.imu, s->imu, sizeof(want.imu)) == 0 ? idx : -1;
}

/** @brief Build the frame for sample @p idx into @p out (21 bytes). */
static void frame_of(int idx,
                     uint8_t out[UART_SAMPLE_LEN + UART_FRAME_OVERHEAD]) {
  struct uart_sample s = sample_of(idx);
  uint8_t payload[UART_SAMPLE_LEN];
  size_t n = 0;
  CHECK(uart_sample_pack(&s, payload) == PQC_OK);
  CHECK(uart_frame_encode(payload, sizeof(payload), out,
                          UART_SAMPLE_LEN + UART_FRAME_OVERHEAD, &n) == PQC_OK);
  CHECK(n == UART_SAMPLE_LEN + UART_FRAME_OVERHEAD);
}

/** @brief Size of one sample frame. */
#define FRAME_LEN (UART_SAMPLE_LEN + UART_FRAME_OVERHEAD)

/** @brief Poll until the parser has nothing more, recording what it found. */
static void drain(struct uart_parser *p, struct sink *k) {
  for (int guard = 0; guard < 100; guard++) {
    struct uart_event ev;
    CHECK(uart_parser_poll(p, &ev) == PQC_OK);
    if (ev.type == UART_EVENT_NONE) {
      return;
    }
    if (ev.type == UART_EVENT_FRAME) {
      struct uart_sample s;
      CHECK(uart_sample_unpack(ev.payload, ev.len, &s) == PQC_OK);
      int idx = index_of(&s);
      CHECK(idx >= 0); /* a delivered frame is always one that was sent */
      if (k->n < MAX_SEEN) {
        k->seen[k->n++] = idx;
      }
    } else if (ev.type == UART_EVENT_BAD_LENGTH) {
      k->bad_length++;
    } else {
      k->bad_crc++;
    }
  }
  CHECK(0 && "poll never settled");
}

/** @brief Push @p n bytes one at a time, draining after each. */
static void feed(struct uart_parser *p, const uint8_t *data, size_t n,
                 struct sink *k) {
  for (size_t i = 0; i < n; i++) {
    CHECK(uart_parser_push(p, data[i]) == PQC_OK);
    drain(p, k);
  }
}

/** @brief Append @p n bytes to @p buf at @p *len. */
static void append(uint8_t *buf, size_t *len, const uint8_t *data, size_t n) {
  memcpy(buf + *len, data, n);
  *len += n;
}

/** @brief Check the sink saw exactly the indexes in @p want, in order. */
static int saw(const struct sink *k, const int *want, size_t n) {
  if (k->n != n) {
    return 0;
  }
  for (size_t i = 0; i < n; i++) {
    if (k->seen[i] != want[i]) {
      return 0;
    }
  }
  return 1;
}

/** @brief Deterministic pseudo-random bytes (no rand(): runs must repeat). */
static uint32_t next_rand(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

/** @brief CRC-16/CCITT-FALSE has the published check value. */
static void test_crc(void) {
  const uint8_t digits[] = "123456789";
  CHECK(uart_frame_crc16(digits, 9) == 0x29B1);
  CHECK(uart_frame_crc16(NULL, 0) == 0xFFFF);
  uint8_t a[4] = {1, 2, 3, 4};
  uint16_t before = uart_frame_crc16(a, sizeof(a));
  a[2] ^= 0x01; /* one flipped bit must change the CRC */
  CHECK(uart_frame_crc16(a, sizeof(a)) != before);
}

/** @brief The example frame in docs/telemetry_format.md section 2, computed
 *         there with an independent CRC. */
static void test_spec_vector(void) {
  static const uint8_t expected[FRAME_LEN] = {
      0xa5, 0x5a, 0x10, 0x01, 0x02, 0x03, 0x04, 0x00, 0x01, 0xff, 0xff,
      0x7f, 0xff, 0x80, 0x00, 0x01, 0x00, 0xff, 0x00, 0xe0, 0xba};
  struct uart_sample s = {.pico_ms = 0x01020304u,
                          .imu = {1, -1, 32767, -32768, 256, -256}};
  uint8_t payload[UART_SAMPLE_LEN];
  uint8_t frame[FRAME_LEN];
  size_t n = 0;
  CHECK(uart_sample_pack(&s, payload) == PQC_OK);
  CHECK(uart_frame_encode(payload, sizeof(payload), frame, sizeof(frame), &n) ==
        PQC_OK);
  CHECK(n == FRAME_LEN);
  CHECK(memcmp(frame, expected, FRAME_LEN) == 0);

  struct uart_sample back;
  CHECK(uart_sample_unpack(&frame[3], UART_SAMPLE_LEN, &back) == PQC_OK);
  CHECK(back.pico_ms == s.pico_ms);
  CHECK(memcmp(back.imu, s.imu, sizeof(s.imu)) == 0);
}

/** @brief Encoding refuses bad lengths and small buffers, and clears out_len.
 */
static void test_encode_errors(void) {
  uint8_t payload[UART_FRAME_MAX_PAYLOAD + 1] = {0};
  uint8_t out[UART_FRAME_MAX_LEN + 1];
  size_t n = 99;

  CHECK(uart_frame_encode(payload, 0, out, sizeof(out), &n) ==
        PQC_ERR_INVALID_ARG);
  CHECK(n == 0);
  CHECK(uart_frame_encode(payload, UART_FRAME_MAX_PAYLOAD + 1, out, sizeof(out),
                          &n) == PQC_ERR_INVALID_ARG);
  CHECK(n == 0);
  CHECK(uart_frame_encode(NULL, 4, out, sizeof(out), &n) ==
        PQC_ERR_INVALID_ARG);
  CHECK(uart_frame_encode(payload, 4, NULL, sizeof(out), &n) ==
        PQC_ERR_INVALID_ARG);
  CHECK(uart_frame_encode(payload, 4, out, sizeof(out), NULL) ==
        PQC_ERR_INVALID_ARG);

  /* The buffer must hold the whole frame: exactly enough works, one less not.
   */
  CHECK(uart_frame_encode(payload, 4, out, 4 + UART_FRAME_OVERHEAD - 1, &n) ==
        PQC_ERR_BUFFER_TOO_SMALL);
  CHECK(n == 0);
  CHECK(uart_frame_encode(payload, 4, out, 4 + UART_FRAME_OVERHEAD, &n) ==
        PQC_OK);
  CHECK(n == 4 + UART_FRAME_OVERHEAD);
  /* The longest allowed payload fits the longest frame. */
  CHECK(uart_frame_encode(payload, UART_FRAME_MAX_PAYLOAD, out, sizeof(out),
                          &n) == PQC_OK);
  CHECK(n == UART_FRAME_MAX_LEN);
}

/** @brief Packing round-trips the extremes; unpacking checks the length. */
static void test_pack_unpack(void) {
  struct uart_sample s = {.pico_ms = 0xFFFFFFFFu,
                          .imu = {32767, -32768, 0, -1, 1, 255}};
  uint8_t payload[UART_SAMPLE_LEN];
  struct uart_sample back;
  CHECK(uart_sample_pack(&s, payload) == PQC_OK);
  CHECK(uart_sample_unpack(payload, sizeof(payload), &back) == PQC_OK);
  CHECK(back.pico_ms == s.pico_ms);
  CHECK(memcmp(back.imu, s.imu, sizeof(s.imu)) == 0);

  for (size_t bad = 0; bad <= UART_FRAME_MAX_PAYLOAD; bad++) {
    if (bad == UART_SAMPLE_LEN) {
      continue;
    }
    uint8_t buf[UART_FRAME_MAX_PAYLOAD] = {0xAA};
    back.pico_ms = 7;
    CHECK(uart_sample_unpack(buf, bad, &back) == PQC_ERR_WIRE_BAD_LENGTH);
    CHECK(back.pico_ms == 0); /* cleared on failure */
  }
  CHECK(uart_sample_pack(NULL, payload) == PQC_ERR_INVALID_ARG);
  CHECK(uart_sample_pack(&s, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(uart_sample_unpack(NULL, UART_SAMPLE_LEN, &back) ==
        PQC_ERR_INVALID_ARG);
  CHECK(uart_sample_unpack(payload, UART_SAMPLE_LEN, NULL) ==
        PQC_ERR_INVALID_ARG);
}

/** @brief Back-to-back frames arrive one byte at a time and come out in
 *         order; a frame is reported once. */
static void test_parser_basic(void) {
  struct uart_parser p;
  struct sink k = {0};
  CHECK(uart_parser_init(&p, UART_SAMPLE_LEN) == PQC_OK);
  uint8_t stream[3 * FRAME_LEN];
  size_t n = 0;
  for (int i = 0; i < 3; i++) {
    uint8_t f[FRAME_LEN];
    frame_of(i, f);
    append(stream, &n, f, sizeof(f));
  }
  feed(&p, stream, n, &k);
  const int want[] = {0, 1, 2};
  CHECK(saw(&k, want, 3));

  struct uart_stats st;
  CHECK(uart_parser_stats(&p, &st) == PQC_OK);
  CHECK(st.frames_ok == 3 && st.bad_length == 0 && st.bad_crc == 0 &&
        st.noise_bytes == 0);

  /* After a frame, polling again reports nothing, not the same frame. */
  struct uart_event ev;
  CHECK(uart_parser_poll(&p, &ev) == PQC_OK && ev.type == UART_EVENT_NONE);
  CHECK(uart_parser_poll(&p, &ev) == PQC_OK && ev.type == UART_EVENT_NONE);
}

/** @brief A parser that expects one payload size rejects the others even when
 *         their CRC is good; one that expects none accepts them. */
static void test_expected_length(void) {
  uint8_t eight[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  uint8_t odd[FRAME_LEN];
  size_t n = 0;
  CHECK(uart_frame_encode(eight, sizeof(eight), odd, sizeof(odd), &n) ==
        PQC_OK);

  uint8_t stream[2 * FRAME_LEN];
  size_t len = 0;
  append(stream, &len, odd, n);
  uint8_t good[FRAME_LEN];
  frame_of(5, good);
  append(stream, &len, good, sizeof(good));

  struct uart_parser p;
  struct sink k = {0};
  CHECK(uart_parser_init(&p, UART_SAMPLE_LEN) == PQC_OK);
  feed(&p, stream, len, &k);
  const int want[] = {5};
  CHECK(saw(&k, want, 1));
  CHECK(k.bad_length == 1);

  /* With no expected length the 8-byte frame is a good frame: take it from the
   * parser directly, since it is not a sample. */
  CHECK(uart_parser_init(&p, 0) == PQC_OK);
  struct uart_event ev;
  for (size_t i = 0; i < n; i++) {
    CHECK(uart_parser_push(&p, odd[i]) == PQC_OK);
  }
  CHECK(uart_parser_poll(&p, &ev) == PQC_OK);
  CHECK(ev.type == UART_EVENT_FRAME && ev.len == 8 &&
        memcmp(ev.payload, eight, 8) == 0);
}

/** @brief Lengths of 0 and above the maximum are counted and skipped, and the
 *         frame after them is still found. */
static void test_bad_length(void) {
  static const uint8_t zero_len[] = {0xA5, 0x5A, 0x00, 0x11, 0x22, 0x33};
  static const uint8_t big_len[] = {0xA5, 0x5A, UART_FRAME_MAX_PAYLOAD + 1,
                                    0x11, 0x22, 0x33};
  const uint8_t *forged[] = {zero_len, big_len};
  for (size_t f = 0; f < 2; f++) {
    struct uart_parser p;
    struct sink k = {0};
    CHECK(uart_parser_init(&p, 0) == PQC_OK);
    uint8_t good[FRAME_LEN];
    frame_of(3, good);
    feed(&p, forged[f], 6, &k);
    feed(&p, good, sizeof(good), &k);
    const int want[] = {3};
    CHECK(saw(&k, want, 1));
    CHECK(k.bad_length == 1);
  }
}

/** @brief Any single flipped bit, in any byte of a frame, is caught, and the
 *         two frames after it still come out. Run with and without an
 *         expected length. */
static void test_bit_flips(void) {
  for (int mode = 0; mode < 2; mode++) {
    for (size_t byte = 0; byte < FRAME_LEN; byte++) {
      for (int bit = 0; bit < 8; bit++) {
        uint8_t stream[3 * FRAME_LEN];
        size_t n = 0;
        uint8_t f[FRAME_LEN];
        frame_of(0, f);
        f[byte] ^= (uint8_t)(1u << bit);
        append(stream, &n, f, sizeof(f));
        frame_of(1, f);
        append(stream, &n, f, sizeof(f));
        frame_of(2, f);
        append(stream, &n, f, sizeof(f));

        struct uart_parser p;
        struct sink k = {0};
        CHECK(uart_parser_init(&p, mode == 0 ? 0 : UART_SAMPLE_LEN) == PQC_OK);
        feed(&p, stream, n, &k);
        const int want[] = {1, 2};
        if (!saw(&k, want, 2)) {
          fprintf(stderr, "bit flip: mode %d byte %zu bit %d\n", mode, byte,
                  bit);
        }
        CHECK(saw(&k, want, 2));
      }
    }
  }
}

/** @brief A lost byte, anywhere in a frame, costs that frame and no other. */
static void test_byte_drop(void) {
  for (size_t drop = 0; drop < FRAME_LEN; drop++) {
    uint8_t stream[3 * FRAME_LEN];
    size_t n = 0;
    uint8_t f[FRAME_LEN];
    frame_of(0, f);
    memmove(&f[drop], &f[drop + 1], FRAME_LEN - drop - 1);
    append(stream, &n, f, FRAME_LEN - 1);
    frame_of(1, f);
    append(stream, &n, f, sizeof(f));
    frame_of(2, f);
    append(stream, &n, f, sizeof(f));

    struct uart_parser p;
    struct sink k = {0};
    CHECK(uart_parser_init(&p, 0) == PQC_OK);
    feed(&p, stream, n, &k);
    const int want[] = {1, 2};
    if (!saw(&k, want, 2)) {
      fprintf(stderr, "byte drop at %zu\n", drop);
    }
    CHECK(saw(&k, want, 2));
  }
}

/** @brief An extra byte inserted inside a frame costs that frame; before it or
 *         after it, the frame survives. Several byte values are tried,
 *         including the sync bytes. */
static void test_byte_insert(void) {
  static const uint8_t values[] = {0x00, 0xA5, 0x5A, 0x10, 0xFF};
  for (size_t v = 0; v < sizeof(values); v++) {
    for (size_t at = 0; at <= FRAME_LEN; at++) {
      uint8_t stream[3 * FRAME_LEN + 1];
      size_t n = 0;
      uint8_t f[FRAME_LEN];
      frame_of(0, f);
      append(stream, &n, f, at);
      stream[n++] = values[v];
      append(stream, &n, f + at, FRAME_LEN - at);
      frame_of(1, f);
      append(stream, &n, f, sizeof(f));
      frame_of(2, f);
      append(stream, &n, f, sizeof(f));

      struct uart_parser p;
      struct sink k = {0};
      CHECK(uart_parser_init(&p, 0) == PQC_OK);
      feed(&p, stream, n, &k);
      /* The frame is intact if the extra byte is outside it (before the first
       * byte or after the last), or if it is an A5 placed between the two sync
       * bytes: then the inserted A5 is the sync byte and the original A5 is
       * noise in front of it. */
      int intact = (at == 0 || at == FRAME_LEN ||
                    (at == 1 && values[v] == UART_FRAME_SYNC0));
      const int with_first[] = {0, 1, 2};
      const int without_first[] = {1, 2};
      int ok = intact ? saw(&k, with_first, 3) : saw(&k, without_first, 2);
      if (!ok) {
        fprintf(stderr, "insert 0x%02x at %zu\n", values[v], at);
      }
      CHECK(ok);
    }
  }
}

/** @brief Random garbage before and between frames never hides a frame. */
static void test_noise(void) {
  for (uint32_t seed = 1; seed <= 40; seed++) {
    uint32_t rng = seed * 7919u;
    uint8_t stream[10 * (FRAME_LEN + 64)];
    size_t n = 0;
    int want[10];
    for (int i = 0; i < 10; i++) {
      size_t garbage = next_rand(&rng) % 60;
      for (size_t g = 0; g < garbage; g++) {
        stream[n++] = (uint8_t)next_rand(&rng);
      }
      uint8_t f[FRAME_LEN];
      frame_of(i, f);
      append(stream, &n, f, sizeof(f));
      want[i] = i;
    }
    struct uart_parser p;
    struct sink k = {0};
    CHECK(uart_parser_init(&p, UART_SAMPLE_LEN) == PQC_OK);
    feed(&p, stream, n, &k);
    if (!saw(&k, want, 10)) {
      fprintf(stderr, "noise seed %u: got %zu frames\n", (unsigned)seed, k.n);
    }
    CHECK(saw(&k, want, 10));
  }
}

/** @brief A false header that claims the longest payload swallows the next two
 *         frames' bytes, but they are still found inside the window. */
static void test_false_sync(void) {
  uint8_t stream[3 + 2 * FRAME_LEN];
  size_t n = 0;
  stream[n++] = 0xA5;
  stream[n++] = 0x5A;
  stream[n++] = UART_FRAME_MAX_PAYLOAD;
  uint8_t f[FRAME_LEN];
  frame_of(4, f);
  append(stream, &n, f, sizeof(f));
  frame_of(5, f);
  append(stream, &n, f, sizeof(f));

  struct uart_parser p;
  struct sink k = {0};
  CHECK(uart_parser_init(&p, 0) == PQC_OK);
  feed(&p, stream, n, &k);
  const int want[] = {4, 5};
  CHECK(saw(&k, want, 2));
  CHECK(k.bad_crc == 1);
}

/** @brief Half a frame is not an event; finishing it later gives the frame. */
static void test_truncated(void) {
  uint8_t f[FRAME_LEN];
  frame_of(8, f);
  struct uart_parser p;
  struct sink k = {0};
  CHECK(uart_parser_init(&p, UART_SAMPLE_LEN) == PQC_OK);
  feed(&p, f, FRAME_LEN / 2, &k);
  CHECK(k.n == 0 && k.bad_length == 0 && k.bad_crc == 0);
  feed(&p, f + FRAME_LEN / 2, FRAME_LEN - FRAME_LEN / 2, &k);
  const int want[] = {8};
  CHECK(saw(&k, want, 1));
}

/** @brief The window is bounded: pushing without polling reports an error
 *         instead of overflowing, and polling makes room again. */
static void test_window_full(void) {
  struct uart_parser p;
  CHECK(uart_parser_init(&p, 0) == PQC_OK);
  for (int i = 0; i < UART_FRAME_MAX_LEN; i++) {
    CHECK(uart_parser_push(&p, 0xA5) == PQC_OK);
  }
  CHECK(uart_parser_push(&p, 0xA5) == PQC_ERR_BUFFER_TOO_SMALL);
  struct sink k = {0};
  drain(&p, &k); /* all noise: the window empties except a trailing A5 */
  CHECK(uart_parser_push(&p, 0x00) == PQC_OK);
}

/** @brief The counters add up for a scripted stream. */
static void test_stats(void) {
  struct uart_parser p;
  struct sink k = {0};
  CHECK(uart_parser_init(&p, UART_SAMPLE_LEN) == PQC_OK);
  uint8_t stream[8 * FRAME_LEN];
  size_t n = 0;
  uint8_t f[FRAME_LEN];
  static const uint8_t noise[] = {0x00, 0x01, 0x02};
  append(stream, &n, noise, sizeof(noise));
  frame_of(0, f);
  append(stream, &n, f, sizeof(f));
  frame_of(1, f);
  f[FRAME_LEN - 1] ^= 0x01; /* wrong CRC */
  append(stream, &n, f, sizeof(f));
  static const uint8_t zero_len[] = {0xA5, 0x5A, 0x00};
  append(stream, &n, zero_len, sizeof(zero_len));
  frame_of(2, f);
  append(stream, &n, f, sizeof(f));
  feed(&p, stream, n, &k);

  struct uart_stats st;
  CHECK(uart_parser_stats(&p, &st) == PQC_OK);
  CHECK(st.frames_ok == 2);
  CHECK(st.bad_crc == 1);
  CHECK(st.bad_length == 1);
  CHECK(st.noise_bytes >= 3);
  const int want[] = {0, 2};
  CHECK(saw(&k, want, 2));
}

/** @brief A long stream of random bytes, with a real frame now and then, never
 *         overflows the window or stalls, and every delivered frame is real. */
static void test_random_stream(void) {
  struct uart_parser p;
  CHECK(uart_parser_init(&p, 0) == PQC_OK);
  uint32_t rng = 12345;
  int sent = 0;
  for (int i = 0; i < 300000; i++) {
    if (i % 1000 == 0 && sent < 200) {
      uint8_t f[FRAME_LEN];
      frame_of(sent % 40, f);
      sent++;
      for (size_t j = 0; j < sizeof(f); j++) {
        CHECK(uart_parser_push(&p, f[j]) == PQC_OK);
        struct uart_event ev;
        do {
          CHECK(uart_parser_poll(&p, &ev) == PQC_OK);
        } while (ev.type != UART_EVENT_NONE);
      }
    }
    CHECK(uart_parser_push(&p, (uint8_t)next_rand(&rng)) == PQC_OK);
    struct uart_event ev;
    int guard = 0;
    do {
      CHECK(uart_parser_poll(&p, &ev) == PQC_OK);
      CHECK(p.fill <= UART_FRAME_MAX_LEN);
      guard++;
    } while (ev.type != UART_EVENT_NONE && guard < 100);
    CHECK(guard < 100);
  }
  struct uart_stats st;
  CHECK(uart_parser_stats(&p, &st) == PQC_OK);
  CHECK(st.frames_ok >=
        190); /* the real frames; random bytes add almost none */
}

/** @brief NULL pointers and out-of-range arguments are refused. */
static void test_bad_args(void) {
  struct uart_parser p;
  struct uart_event ev;
  struct uart_stats st;
  CHECK(uart_parser_init(NULL, 0) == PQC_ERR_INVALID_ARG);
  CHECK(uart_parser_init(&p, UART_FRAME_MAX_PAYLOAD + 1) ==
        PQC_ERR_INVALID_ARG);
  CHECK(uart_parser_init(&p, UART_FRAME_MAX_PAYLOAD) == PQC_OK);
  CHECK(uart_parser_push(NULL, 0) == PQC_ERR_INVALID_ARG);
  CHECK(uart_parser_poll(NULL, &ev) == PQC_ERR_INVALID_ARG);
  CHECK(ev.type == UART_EVENT_NONE);
  CHECK(uart_parser_poll(&p, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(uart_parser_stats(NULL, &st) == PQC_ERR_INVALID_ARG);
  CHECK(uart_parser_stats(&p, NULL) == PQC_ERR_INVALID_ARG);
}

/** @brief Run every test; exit status is nonzero if any check failed. */
int main(void) {
  test_crc();
  test_spec_vector();
  test_encode_errors();
  test_pack_unpack();
  test_parser_basic();
  test_expected_length();
  test_bad_length();
  test_bit_flips();
  test_byte_drop();
  test_byte_insert();
  test_noise();
  test_false_sync();
  test_truncated();
  test_window_full();
  test_stats();
  test_random_stream();
  test_bad_args();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
