/**
 * @file wire.c
 * @brief Encoding and checking of RECORD and ACK messages.
 */

#include "proto/wire.h"

#include <string.h>

#include "common/errors.h"

/** @brief Write @p v at @p p, most significant byte first. */
static void put_be16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)v;
}

/** @brief Write @p v at @p p, most significant byte first. */
static void put_be32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = (uint8_t)(v >> (24 - 8 * i));
  }
}

/** @brief Write @p v at @p p, most significant byte first. */
static void put_be64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    p[i] = (uint8_t)(v >> (56 - 8 * i));
  }
}

/** @brief Read a big-endian 16-bit value from @p p. */
static uint16_t get_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/** @brief Read a big-endian 32-bit value from @p p. */
static uint32_t get_be32(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

/** @brief Read a big-endian 64-bit value from @p p. */
static uint64_t get_be64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

/** @brief Write the type and length header at @p out. */
static void put_header(uint8_t *out, uint8_t type, uint16_t len) {
  out[0] = type;
  put_be16(out + 1, len);
}

int wire_encode_record(const struct wire_record *rec, uint8_t *out,
                       size_t out_cap, size_t *out_len) {
  if (out_len == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out_len = 0;
  if (rec == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (out_cap < WIRE_RECORD_MSG_LEN) {
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  /* seq 0 is reserved: the receiver rejects it, so never send it. */
  if (rec->seq == 0) {
    return PQC_ERR_WIRE_BAD_FIELD;
  }

  put_header(out, WIRE_TYPE_RECORD, WIRE_RECORD_LEN);
  uint8_t *p = out + WIRE_HEADER_LEN;
  p[0] = rec->node;
  put_be64(p + 1, rec->seq);
  put_be64(p + 9, rec->ts_ms);
  put_be32(p + 17, rec->pico_ms);
  for (int i = 0; i < 6; i++) {
    /* Negative to unsigned is defined modulo 2^16: two's complement bits. */
    put_be16(p + 21 + 2 * i, (uint16_t)rec->imu[i]);
  }
  *out_len = WIRE_RECORD_MSG_LEN;
  return PQC_OK;
}

int wire_encode_ack(const struct wire_ack *ack, uint8_t *out, size_t out_cap,
                    size_t *out_len) {
  if (out_len == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out_len = 0;
  if (ack == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (out_cap < WIRE_ACK_MSG_LEN) {
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  if (ack->seq == 0) {
    return PQC_ERR_WIRE_BAD_FIELD;
  }

  put_header(out, WIRE_TYPE_ACK, WIRE_ACK_LEN);
  out[WIRE_HEADER_LEN] = ack->node;
  put_be64(out + WIRE_HEADER_LEN + 1, ack->seq);
  *out_len = WIRE_ACK_MSG_LEN;
  return PQC_OK;
}

int wire_parse_header(const uint8_t *hdr, uint8_t *type, uint16_t *len) {
  if (hdr == NULL || type == NULL || len == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  uint16_t announced = get_be16(hdr + 1);
  /* Check the maximum first, so a huge length is rejected for what it is
   * before any per-type comparison. */
  if (announced > WIRE_MAX_PAYLOAD) {
    return PQC_ERR_WIRE_BAD_LENGTH;
  }
  switch (hdr[0]) {
    case WIRE_TYPE_RECORD:
      if (announced != WIRE_RECORD_LEN) {
        return PQC_ERR_WIRE_BAD_LENGTH;
      }
      break;
    case WIRE_TYPE_ACK:
      if (announced != WIRE_ACK_LEN) {
        return PQC_ERR_WIRE_BAD_LENGTH;
      }
      break;
    default:
      return PQC_ERR_WIRE_BAD_TYPE;
  }
  *type = hdr[0];
  *len = announced;
  return PQC_OK;
}

int wire_decode_record(const uint8_t *payload, size_t len,
                       struct wire_record *rec) {
  if (rec == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  memset(rec, 0, sizeof(*rec));
  if (payload == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (len != WIRE_RECORD_LEN) {
    return PQC_ERR_WIRE_BAD_LENGTH;
  }
  uint64_t seq = get_be64(payload + 1);
  if (seq == 0) {
    return PQC_ERR_WIRE_BAD_FIELD;
  }

  rec->node = payload[0];
  rec->seq = seq;
  rec->ts_ms = get_be64(payload + 9);
  rec->pico_ms = get_be32(payload + 17);
  for (int i = 0; i < 6; i++) {
    /* Same bits, read back as signed. */
    rec->imu[i] = (int16_t)get_be16(payload + 21 + 2 * i);
  }
  return PQC_OK;
}

int wire_decode_ack(const uint8_t *payload, size_t len, struct wire_ack *ack) {
  if (ack == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  memset(ack, 0, sizeof(*ack));
  if (payload == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (len != WIRE_ACK_LEN) {
    return PQC_ERR_WIRE_BAD_LENGTH;
  }
  uint64_t seq = get_be64(payload + 1);
  if (seq == 0) {
    return PQC_ERR_WIRE_BAD_FIELD;
  }

  ack->node = payload[0];
  ack->seq = seq;
  return PQC_OK;
}
