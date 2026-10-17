/**
 * @file test_wire.c
 * @brief Tests for the RECORD and ACK wire format.
 *
 * Includes a fixed byte vector so the big-endian layout in
 * docs/telemetry_format.md is checked byte by byte, not only by round trip.
 */

#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "proto/wire.h"

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

/** @brief A record with distinct, easy-to-spot values in every field. */
static struct wire_record sample_record(void) {
  struct wire_record r = {
      .node = 1,
      .seq = 0x0102030405060708ULL,
      .ts_ms = 0x1112131415161718ULL,
      .pico_ms = 0x21222324U,
      .imu = {1, -1, 0x1234, -32768, 32767, 0},
  };
  return r;
}

/** @brief The exact bytes the spec says sample_record() must produce. */
static const uint8_t expected_record_msg[WIRE_RECORD_MSG_LEN] = {
    0x01, 0x00, 0x21,                               /* type, length 33 */
    0x01,                                           /* node */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, /* seq */
    0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, /* ts_ms */
    0x21, 0x22, 0x23, 0x24,                         /* pico_ms */
    0x00, 0x01, 0xFF, 0xFF, 0x12, 0x34,             /* ax ay az */
    0x80, 0x00, 0x7F, 0xFF, 0x00, 0x00,             /* gx gy gz */
};

/** @brief Encoding matches the fixed vector and decodes back. */
static void test_record_roundtrip(void) {
  struct wire_record in = sample_record();
  uint8_t buf[WIRE_RECORD_MSG_LEN];
  size_t n = 99;

  CHECK(wire_encode_record(&in, buf, sizeof(buf), &n) == PQC_OK);
  CHECK(n == WIRE_RECORD_MSG_LEN);
  CHECK(memcmp(buf, expected_record_msg, sizeof(buf)) == 0);

  uint8_t type = 0;
  uint16_t len = 0;
  CHECK(wire_parse_header(buf, &type, &len) == PQC_OK);
  CHECK(type == WIRE_TYPE_RECORD);
  CHECK(len == WIRE_RECORD_LEN);

  struct wire_record out;
  CHECK(wire_decode_record(buf + WIRE_HEADER_LEN, len, &out) == PQC_OK);
  CHECK(out.node == in.node);
  CHECK(out.seq == in.seq);
  CHECK(out.ts_ms == in.ts_ms);
  CHECK(out.pico_ms == in.pico_ms);
  CHECK(memcmp(out.imu, in.imu, sizeof(in.imu)) == 0);
}

/** @brief ACK encodes to the documented bytes and decodes back. */
static void test_ack_roundtrip(void) {
  struct wire_ack in = {.node = 1, .seq = 0x0A0B0C0D0E0F1011ULL};
  const uint8_t expected[WIRE_ACK_MSG_LEN] = {
      0x02, 0x00, 0x09, 0x01, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11,
  };
  uint8_t buf[WIRE_ACK_MSG_LEN];
  size_t n = 0;

  CHECK(wire_encode_ack(&in, buf, sizeof(buf), &n) == PQC_OK);
  CHECK(n == WIRE_ACK_MSG_LEN);
  CHECK(memcmp(buf, expected, sizeof(buf)) == 0);

  uint8_t type = 0;
  uint16_t len = 0;
  CHECK(wire_parse_header(buf, &type, &len) == PQC_OK);
  CHECK(type == WIRE_TYPE_ACK && len == WIRE_ACK_LEN);

  struct wire_ack out;
  CHECK(wire_decode_ack(buf + WIRE_HEADER_LEN, len, &out) == PQC_OK);
  CHECK(out.node == in.node && out.seq == in.seq);
}

/** @brief Encoders reject NULL, a small buffer and seq 0. */
static void test_encode_bad_input(void) {
  struct wire_record r = sample_record();
  struct wire_ack a = {.node = 1, .seq = 5};
  uint8_t buf[WIRE_RECORD_MSG_LEN];
  size_t n = 99;

  CHECK(wire_encode_record(NULL, buf, sizeof(buf), &n) == PQC_ERR_INVALID_ARG);
  CHECK(n == 0);
  CHECK(wire_encode_record(&r, NULL, sizeof(buf), &n) == PQC_ERR_INVALID_ARG);
  CHECK(wire_encode_record(&r, buf, sizeof(buf), NULL) == PQC_ERR_INVALID_ARG);
  CHECK(wire_encode_record(&r, buf, WIRE_RECORD_MSG_LEN - 1, &n) ==
        PQC_ERR_BUFFER_TOO_SMALL);
  CHECK(n == 0);
  r.seq = 0;
  CHECK(wire_encode_record(&r, buf, sizeof(buf), &n) == PQC_ERR_WIRE_BAD_FIELD);
  CHECK(n == 0);

  CHECK(wire_encode_ack(NULL, buf, sizeof(buf), &n) == PQC_ERR_INVALID_ARG);
  CHECK(wire_encode_ack(&a, buf, WIRE_ACK_MSG_LEN - 1, &n) ==
        PQC_ERR_BUFFER_TOO_SMALL);
  a.seq = 0;
  CHECK(wire_encode_ack(&a, buf, sizeof(buf), &n) == PQC_ERR_WIRE_BAD_FIELD);
}

/** @brief The header check rejects every bad type and length. */
static void test_parse_header_bad_input(void) {
  uint8_t type = 0xEE;
  uint16_t len = 0xEEEE;
  const uint8_t unknown_type[3] = {0x03, 0x00, 0x21};
  const uint8_t type_zero[3] = {0x00, 0x00, 0x21};
  const uint8_t record_short[3] = {0x01, 0x00, 0x20};
  const uint8_t record_long[3] = {0x01, 0x00, 0x22};
  const uint8_t ack_as_record_len[3] = {0x02, 0x00, 0x21};
  const uint8_t huge[3] = {0x01, 0xFF, 0xFF};
  const uint8_t just_over_max[3] = {0x01, 0x00, WIRE_MAX_PAYLOAD + 1};
  const uint8_t good[3] = {0x01, 0x00, 0x21};

  CHECK(wire_parse_header(NULL, &type, &len) == PQC_ERR_INVALID_ARG);
  CHECK(wire_parse_header(good, NULL, &len) == PQC_ERR_INVALID_ARG);
  CHECK(wire_parse_header(good, &type, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(wire_parse_header(unknown_type, &type, &len) == PQC_ERR_WIRE_BAD_TYPE);
  CHECK(wire_parse_header(type_zero, &type, &len) == PQC_ERR_WIRE_BAD_TYPE);
  CHECK(wire_parse_header(record_short, &type, &len) ==
        PQC_ERR_WIRE_BAD_LENGTH);
  CHECK(wire_parse_header(record_long, &type, &len) == PQC_ERR_WIRE_BAD_LENGTH);
  CHECK(wire_parse_header(ack_as_record_len, &type, &len) ==
        PQC_ERR_WIRE_BAD_LENGTH);
  CHECK(wire_parse_header(huge, &type, &len) == PQC_ERR_WIRE_BAD_LENGTH);
  CHECK(wire_parse_header(just_over_max, &type, &len) ==
        PQC_ERR_WIRE_BAD_LENGTH);
  /* Outputs must not be written on failure. */
  CHECK(type == 0xEE && len == 0xEEEE);
}

/** @brief Decoders reject wrong lengths, NULL and seq 0, and zero the output.
 */
static void test_decode_bad_input(void) {
  uint8_t payload[WIRE_RECORD_LEN];
  memcpy(payload, expected_record_msg + WIRE_HEADER_LEN, sizeof(payload));
  struct wire_record r;
  struct wire_ack a;

  CHECK(wire_decode_record(NULL, WIRE_RECORD_LEN, &r) == PQC_ERR_INVALID_ARG);
  CHECK(wire_decode_record(payload, WIRE_RECORD_LEN, NULL) ==
        PQC_ERR_INVALID_ARG);
  CHECK(wire_decode_record(payload, WIRE_RECORD_LEN - 1, &r) ==
        PQC_ERR_WIRE_BAD_LENGTH);
  CHECK(r.seq == 0 && r.node == 0);
  CHECK(wire_decode_record(payload, WIRE_RECORD_LEN + 1, &r) ==
        PQC_ERR_WIRE_BAD_LENGTH);

  memset(payload + 1, 0, 8); /* seq field */
  CHECK(wire_decode_record(payload, WIRE_RECORD_LEN, &r) ==
        PQC_ERR_WIRE_BAD_FIELD);
  CHECK(r.seq == 0 && r.ts_ms == 0);

  uint8_t ack_payload[WIRE_ACK_LEN] = {1, 0, 0, 0, 0, 0, 0, 0, 7};
  CHECK(wire_decode_ack(ack_payload, WIRE_ACK_LEN, &a) == PQC_OK);
  CHECK(a.node == 1 && a.seq == 7);
  CHECK(wire_decode_ack(NULL, WIRE_ACK_LEN, &a) == PQC_ERR_INVALID_ARG);
  CHECK(wire_decode_ack(ack_payload, WIRE_ACK_LEN, NULL) ==
        PQC_ERR_INVALID_ARG);
  CHECK(wire_decode_ack(ack_payload, WIRE_ACK_LEN - 1, &a) ==
        PQC_ERR_WIRE_BAD_LENGTH);
  CHECK(wire_decode_ack(ack_payload, WIRE_ACK_LEN + 1, &a) ==
        PQC_ERR_WIRE_BAD_LENGTH);
  memset(ack_payload + 1, 0, 8);
  CHECK(wire_decode_ack(ack_payload, WIRE_ACK_LEN, &a) ==
        PQC_ERR_WIRE_BAD_FIELD);
  CHECK(a.seq == 0 && a.node == 0);
}

/** @brief Largest and smallest legal values survive a round trip. */
static void test_extreme_values(void) {
  struct wire_record in = {.node = 255,
                           .seq = UINT64_MAX,
                           .ts_ms = UINT64_MAX,
                           .pico_ms = UINT32_MAX,
                           .imu = {INT16_MIN, INT16_MAX, -1, 1, 0, -2}};
  uint8_t buf[WIRE_RECORD_MSG_LEN];
  size_t n = 0;
  struct wire_record out;

  CHECK(wire_encode_record(&in, buf, sizeof(buf), &n) == PQC_OK);
  CHECK(wire_decode_record(buf + WIRE_HEADER_LEN, WIRE_RECORD_LEN, &out) ==
        PQC_OK);
  CHECK(out.node == 255 && out.seq == UINT64_MAX && out.ts_ms == UINT64_MAX);
  CHECK(out.pico_ms == UINT32_MAX);
  CHECK(memcmp(out.imu, in.imu, sizeof(in.imu)) == 0);
}

/** @brief Run every test and report. */
int main(void) {
  test_record_roundtrip();
  test_ack_roundtrip();
  test_encode_bad_input();
  test_parse_header_bad_input();
  test_decode_bad_input();
  test_extreme_values();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
