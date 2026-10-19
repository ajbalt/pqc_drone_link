/**
 * @file sample_source.c
 * @brief UART bytes to records: the frame parser, plus gap counting.
 */

#include "uart/sample_source.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common/errors.h"
#include "proto/uart_frame.h"
#include "uart/uart_port.h"

/** @brief Bytes read from the port in one go. */
#define RX_CHUNK 256

/** @brief An open source: the port, the parser and the gap state. */
struct sample_source {
  struct uart_port *port;         /**< the serial device */
  struct uart_parser parser;      /**< finds frames in the bytes */
  uint8_t rx[RX_CHUNK];           /**< bytes read and not yet given to parser */
  size_t rx_pos;                  /**< next byte of rx to give */
  size_t rx_len;                  /**< bytes in rx */
  uint32_t period_ms;             /**< nominal time between samples */
  uint32_t max_gap_ms;            /**< reset threshold */
  int have_prev;                  /**< 1 once a sample has been seen */
  uint32_t prev_ms;               /**< pico_ms of the previous sample */
  struct sample_source_stats own; /**< the counters this layer keeps */
};

int sample_gap_classify(uint32_t prev_ms, uint32_t cur_ms, uint32_t period_ms,
                        uint32_t max_gap_ms, enum sample_gap *kind,
                        uint32_t *lost) {
  if (kind == NULL || lost == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *kind = SAMPLE_GAP_NONE;
  *lost = 0;
  /* 64-bit arithmetic below, so 2 x period cannot overflow. */
  if (period_ms == 0 || (uint64_t)max_gap_ms <= 2u * (uint64_t)period_ms) {
    return PQC_ERR_BAD_CONFIG;
  }
  uint32_t delta = cur_ms - prev_ms; /* unsigned: the wrap at 2^32 is fine */
  if (delta >= max_gap_ms) {
    *kind = SAMPLE_GAP_RESET;
  } else if (2u * (uint64_t)delta < period_ms) {
    *kind = SAMPLE_GAP_EARLY;
  } else if (2u * (uint64_t)delta >= 3u * (uint64_t)period_ms) {
    *kind = SAMPLE_GAP_LOST;
    /* delta / period rounded to the nearest whole number, minus the sample
     * that did arrive. */
    *lost = (uint32_t)(((uint64_t)delta + period_ms / 2u) / period_ms) - 1u;
  }
  return PQC_OK;
}

int sample_source_open(const struct sample_source_config *cfg,
                       struct sample_source **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  if (cfg == NULL || cfg->path == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* Check the timing settings before touching the device. */
  enum sample_gap kind;
  uint32_t lost;
  int rc =
      sample_gap_classify(0, 0, cfg->period_ms, cfg->max_gap_ms, &kind, &lost);
  if (rc != PQC_OK) {
    return rc;
  }
  struct sample_source *s = calloc(1, sizeof(*s));
  if (s == NULL) {
    return PQC_ERR_IO;
  }
  s->period_ms = cfg->period_ms;
  s->max_gap_ms = cfg->max_gap_ms;
  rc = uart_parser_init(&s->parser, UART_SAMPLE_LEN);
  if (rc == PQC_OK) {
    rc = uart_port_open(cfg->path, cfg->baud, &s->port);
  }
  if (rc != PQC_OK) {
    free(s);
    return rc;
  }
  *out = s;
  return PQC_OK;
}

/** @brief Wall-clock milliseconds since the Unix epoch. */
static int wall_ms(uint64_t *out) {
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    return PQC_ERR_INTERNAL;
  }
  *out = (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
  return PQC_OK;
}

/** @brief Count a sample against the one before it. */
static void note_sample(struct sample_source *s, uint32_t pico_ms) {
  if (s->have_prev) {
    enum sample_gap kind;
    uint32_t lost;
    /* The settings were checked when the source was opened. */
    if (sample_gap_classify(s->prev_ms, pico_ms, s->period_ms, s->max_gap_ms,
                            &kind, &lost) == PQC_OK) {
      if (kind == SAMPLE_GAP_LOST) {
        s->own.gaps++;
        s->own.samples_lost += lost;
      } else if (kind == SAMPLE_GAP_EARLY) {
        s->own.early++;
      } else if (kind == SAMPLE_GAP_RESET) {
        s->own.resets++;
      }
    }
  }
  s->have_prev = 1;
  s->prev_ms = pico_ms;
}

/**
 * @brief Feed buffered bytes to the parser until a frame comes out.
 * @param[in,out] s    The source.
 * @param[out]    rec  The record, filled in when a frame comes out.
 * @param[out]    got  1 if @p rec was filled in.
 * @return PQC_OK, or the error that stopped it.
 */
static int take_frame(struct sample_source *s, struct wire_record *rec,
                      int *got) {
  for (;;) {
    struct uart_event ev;
    int rc = uart_parser_poll(&s->parser, &ev);
    if (rc != PQC_OK) {
      return rc;
    }
    if (ev.type == UART_EVENT_FRAME) {
      struct uart_sample smp;
      rc = uart_sample_unpack(ev.payload, ev.len, &smp);
      if (rc != PQC_OK) {
        return PQC_ERR_INTERNAL; /* the parser only passes 16-byte payloads */
      }
      uint64_t now = 0;
      rc = wall_ms(&now);
      if (rc != PQC_OK) {
        return rc;
      }
      memset(rec, 0, sizeof(*rec));
      rec->ts_ms = now;
      rec->pico_ms = smp.pico_ms;
      memcpy(rec->imu, smp.imu, sizeof(rec->imu));
      note_sample(s, smp.pico_ms);
      *got = 1;
      return PQC_OK;
    }
    if (ev.type != UART_EVENT_NONE) {
      continue; /* a bad frame: the parser counted it; look for the next event
                 */
    }
    if (s->rx_pos >= s->rx_len) {
      return PQC_OK; /* nothing buffered: the caller must read more */
    }
    rc = uart_parser_push(&s->parser, s->rx[s->rx_pos]);
    if (rc != PQC_OK) {
      return rc;
    }
    s->rx_pos++;
  }
}

int sample_source_next(struct sample_source *s, unsigned timeout_ms,
                       struct wire_record *rec, int *got) {
  if (got == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *got = 0;
  if (s == NULL || rec == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  memset(rec, 0, sizeof(*rec));

  /* Bytes from an earlier read may still hold frames. */
  int rc = take_frame(s, rec, got);
  if (rc != PQC_OK || *got) {
    return rc;
  }
  size_t n = 0;
  rc = uart_port_read(s->port, s->rx, sizeof(s->rx), timeout_ms, &n);
  if (rc != PQC_OK) {
    return rc;
  }
  s->rx_len = n;
  s->rx_pos = 0;
  return take_frame(s, rec, got);
}

int sample_source_get_stats(const struct sample_source *s,
                            struct sample_source_stats *out) {
  if (s == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  struct uart_stats ps;
  int rc = uart_parser_stats(&s->parser, &ps);
  if (rc != PQC_OK) {
    return rc;
  }
  *out = s->own;
  out->frames_ok = ps.frames_ok;
  out->bad_length = ps.bad_length;
  out->bad_crc = ps.bad_crc;
  out->noise_bytes = ps.noise_bytes;
  return PQC_OK;
}

int sample_source_fd(const struct sample_source *s, int *fd) {
  if (s == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  return uart_port_fd(s->port, fd);
}

void sample_source_close(struct sample_source *s) {
  if (s == NULL) {
    return;
  }
  uart_port_close(s->port);
  free(s);
}
