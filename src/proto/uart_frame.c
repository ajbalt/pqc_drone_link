/**
 * @file uart_frame.c
 * @brief UART frame encoder, sample packing and the resynchronising parser.
 */

#include "proto/uart_frame.h"

#include <stddef.h>

#include "common/errors.h"

/** @brief Write @p v at @p p, most significant byte first. */
static void put_be16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v >> 8);
  p[1] = (uint8_t)(v & 0xFFu);
}

/** @brief Read a big-endian 16-bit value. */
static uint16_t get_be16(const uint8_t *p) {
  return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

/** @brief Write @p v at @p p, most significant byte first. */
static void put_be32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = (uint8_t)(v >> (24 - 8 * i));
  }
}

/** @brief Read a big-endian 32-bit value. */
static uint32_t get_be32(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

/** @brief Read a big-endian two's-complement 16-bit value.
 *
 * Converting an unsigned value above 32767 to int16_t is implementation-defined
 * in C, so the sign is handled by arithmetic instead of a cast. */
static int16_t get_be16_signed(const uint8_t *p) {
  uint16_t u = get_be16(p);
  if ((u & 0x8000u) != 0) {
    return (int16_t)(-(int32_t)(0x10000u - u));
  }
  return (int16_t)u;
}

uint16_t uart_frame_crc16(const uint8_t *p, size_t n) {
  /* 32-bit working value: shifts of a uint16_t are done in (signed) int, and
   * mixing that with unsigned constants is what -Wsign-compare objects to. */
  uint32_t crc = 0xFFFFu;
  for (size_t i = 0; i < n; i++) {
    crc ^= (uint32_t)p[i] << 8;
    for (int bit = 0; bit < 8; bit++) {
      crc = ((crc & 0x8000u) != 0) ? ((crc << 1) ^ 0x1021u) : (crc << 1);
      crc &= 0xFFFFu;
    }
  }
  return (uint16_t)crc;
}

int uart_frame_encode(const uint8_t *payload, size_t len, uint8_t *out,
                      size_t out_cap, size_t *out_len) {
  if (out_len == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out_len = 0;
  if (payload == NULL || out == NULL || len < 1 ||
      len > UART_FRAME_MAX_PAYLOAD) {
    return PQC_ERR_INVALID_ARG;
  }
  size_t total = len + UART_FRAME_OVERHEAD;
  if (out_cap < total) {
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  out[0] = UART_FRAME_SYNC0;
  out[1] = UART_FRAME_SYNC1;
  out[2] = (uint8_t)len;
  for (size_t i = 0; i < len; i++) {
    out[3 + i] = payload[i];
  }
  /* The CRC covers the length byte and the payload, not the sync bytes. */
  put_be16(&out[3 + len], uart_frame_crc16(&out[2], 1 + len));
  *out_len = total;
  return PQC_OK;
}

int uart_sample_pack(const struct uart_sample *s, uint8_t *out) {
  if (s == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  put_be32(out, s->pico_ms);
  for (int i = 0; i < 6; i++) {
    /* int16_t to uint16_t is defined modulo 2^16, so negatives are fine. */
    put_be16(&out[4 + 2 * i], (uint16_t)s->imu[i]);
  }
  return PQC_OK;
}

int uart_sample_unpack(const uint8_t *payload, size_t len,
                       struct uart_sample *s) {
  if (s == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  s->pico_ms = 0;
  for (int i = 0; i < 6; i++) {
    s->imu[i] = 0;
  }
  if (payload == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (len != UART_SAMPLE_LEN) {
    return PQC_ERR_WIRE_BAD_LENGTH;
  }
  s->pico_ms = get_be32(payload);
  for (int i = 0; i < 6; i++) {
    s->imu[i] = get_be16_signed(&payload[4 + 2 * i]);
  }
  return PQC_OK;
}

int uart_parser_init(struct uart_parser *p, size_t expected_payload_len) {
  if (p == NULL || expected_payload_len > UART_FRAME_MAX_PAYLOAD) {
    return PQC_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < UART_FRAME_MAX_LEN; i++) {
    p->win[i] = 0;
  }
  p->fill = 0;
  p->drop = 0;
  p->expect_len = (uint8_t)expected_payload_len;
  p->stats.frames_ok = 0;
  p->stats.bad_length = 0;
  p->stats.bad_crc = 0;
  p->stats.noise_bytes = 0;
  return PQC_OK;
}

/** @brief Remove the first @p n bytes of the window. */
static void drop_front(struct uart_parser *p, size_t n) {
  size_t i = 0;
  for (; i + n < p->fill; i++) {
    p->win[i] = p->win[i + n];
  }
  p->fill = (uint8_t)(p->fill - n);
}

/** @brief Remove the frame handed out by the last UART_EVENT_FRAME. */
static void drop_delivered(struct uart_parser *p) {
  if (p->drop != 0) {
    drop_front(p, p->drop);
    p->drop = 0;
  }
}

int uart_parser_push(struct uart_parser *p, uint8_t byte) {
  if (p == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  drop_delivered(p);
  if (p->fill >= UART_FRAME_MAX_LEN) {
    return PQC_ERR_BUFFER_TOO_SMALL; /* the caller did not poll */
  }
  p->win[p->fill] = byte;
  p->fill++;
  return PQC_OK;
}

int uart_parser_poll(struct uart_parser *p, struct uart_event *ev) {
  if (ev == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  ev->type = UART_EVENT_NONE;
  ev->payload = NULL;
  ev->len = 0;
  if (p == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  drop_delivered(p);

  /* The window starts at the first byte not yet judged. Each pass either
   * decides about it or needs more bytes, so the loop always ends. */
  for (;;) {
    if (p->fill == 0) {
      return PQC_OK;
    }
    if (p->win[0] != UART_FRAME_SYNC0) {
      drop_front(p, 1);
      p->stats.noise_bytes++;
      continue;
    }
    if (p->fill < 2) {
      return PQC_OK; /* an A5 that may start a frame: wait */
    }
    if (p->win[1] != UART_FRAME_SYNC1) {
      drop_front(p, 1);
      p->stats.noise_bytes++;
      continue;
    }
    if (p->fill < 3) {
      return PQC_OK;
    }

    size_t len = p->win[2];
    if (len < 1 || len > UART_FRAME_MAX_PAYLOAD ||
        (p->expect_len != 0 && len != p->expect_len)) {
      /* Drop only the first byte: the bytes after it may hold a real frame
       * (a false sync, or a damaged length). */
      drop_front(p, 1);
      p->stats.bad_length++;
      ev->type = UART_EVENT_BAD_LENGTH;
      return PQC_OK;
    }

    size_t total = len + UART_FRAME_OVERHEAD;
    if (p->fill < total) {
      return PQC_OK;
    }
    if (uart_frame_crc16(&p->win[2], 1 + len) != get_be16(&p->win[3 + len])) {
      drop_front(p, 1); /* again only one byte, so nothing behind it is lost */
      p->stats.bad_crc++;
      ev->type = UART_EVENT_BAD_CRC;
      return PQC_OK;
    }

    p->stats.frames_ok++;
    p->drop = (uint8_t)total; /* removed on the next call: ev points into win */
    ev->type = UART_EVENT_FRAME;
    ev->payload = &p->win[3];
    ev->len = len;
    return PQC_OK;
  }
}

int uart_parser_stats(const struct uart_parser *p, struct uart_stats *out) {
  if (p == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = p->stats;
  return PQC_OK;
}
