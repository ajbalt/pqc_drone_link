/**
 * @file logger.c
 * @brief In-memory event buffer and CSV writer.
 */

#include "metrics/logger.h"

#include <inttypes.h>

#include "common/errors.h"
#include "metrics/timer.h"

/**
 * @brief CSV name of each event.
 *
 * Names are what end up in the CSV and what analysis/parse_logs.py matches
 * on, so they must stay exactly as listed in CONTRIBUTING.md.
 */
static const char *const event_names[] = {
    [LOG_EV_HANDSHAKE_START] = "handshake_start",
    [LOG_EV_HANDSHAKE_DONE] = "handshake_done",
    [LOG_EV_HANDSHAKE_FAIL] = "handshake_fail",
    [LOG_EV_FRAG_TX] = "frag_tx",
    [LOG_EV_FRAG_RX] = "frag_rx",
    [LOG_EV_FRAG_RETX] = "frag_retx",
    [LOG_EV_PKT_TX] = "pkt_tx",
    [LOG_EV_PKT_RX] = "pkt_rx",
    [LOG_EV_PKT_DROP] = "pkt_drop",
    [LOG_EV_DECRYPT_FAIL] = "decrypt_fail",
    [LOG_EV_REKEY_START] = "rekey_start",
    [LOG_EV_REKEY_DONE] = "rekey_done",
    [LOG_EV_LOG_DROPPED] = "log_dropped",
};

/* Faisl the buidl if an event is added to the enum without a name here. */
_Static_assert(sizeof event_names / sizeof event_names[0] == LOG_EV_COUNT,
               "every log_event needs a name in event_name");

/** @brief CSV name fo each endpoint.  */
static const char *const endpoint_names[] = {
    [LOG_ENDPOINT_DRONE] = "drone",
    [LOG_ENDPOINT_GROUND] = "ground",
};

/**
 * @brief Copy a config string that will be written into a CSV column.
 *
 * Rejects anything that would break the CSV: a comma or newline would shift
 * every following column for that row, and quoting rules differ between
 * readers. Rejecting is simpler and safer than escaping.
 */

static int copy_csv_field(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  for (; src[i] != '\0'; i++) {
    if (i + 1 >= cap) {
      return PQC_ERR_BAD_CONFIG; /* no room for this char and the NUL */
    }
    char c = src[i];
    if (c == ',' || c == '"' || c == '\n' || c == '\r') {
      return PQC_ERR_BAD_CONFIG;
    }
    dst[i] = c;
  }
  if (i == 0) {
    return PQC_ERR_BAD_CONFIG;
  }
  dst[i] = '\0';
  return PQC_OK;
}

int logger_init(struct logger *lg, struct log_record *storage, size_t capacity,
                const char *path, const char *run_id, const char *mode,
                enum log_endpoint endpoint) {
  if (lg == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  lg->fp = NULL; /* so the other functions see "not open" if we fail */

  if (storage == NULL || capacity == 0 || path == NULL || run_id == NULL ||
      mode == NULL || (unsigned)endpoint > LOG_ENDPOINT_GROUND) {
    return PQC_ERR_INVALID_ARG;
  }
  /* Validate everything before creating the file, so bad config never
   * leaves an empty log behind. */
  int rc = copy_csv_field(lg->run_id, sizeof lg->run_id, run_id);
  if (rc != PQC_OK) {
    return rc;
  }
  rc = copy_csv_field(lg->mode, sizeof lg->mode, mode);
  if (rc != PQC_OK) {
    return rc;
  }

  /* "x" (C11): fail if the file exists instead of truncating it. */
  FILE *fp = fopen(path, "wx");
  if (fp == NULL) {
    return PQC_ERR_IO;
  }
  if (fputs("run_id,mode,timestamp_ns,endpoint,event,bytes,detail\n", fp) < 0) {
    fclose(fp);
    return PQC_ERR_IO;
  }

  lg->fp = fp;
  lg->endpoint = endpoint;
  lg->records = storage;
  lg->capacity = capacity;
  lg->count = 0;
  lg->dropped = 0;
  return PQC_OK;
}

int logger_event(struct logger *lg, enum log_event event, uint32_t bytes,
                 int64_t detail) {
  if (lg == NULL || lg->fp == NULL || (unsigned)event >= LOG_EV_COUNT) {
    return PQC_ERR_INVALID_ARG;
  }
  /* Full buffer: drop the event rather than flush, because flushing here
   * would do file I/O inside whatever is being timed. The drop is counted
   * and written out as a log_dropped row, so the analysis can tell the
   * log for this run is incomplete. */
  if (lg->count >= lg->capacity) {
    lg->dropped++;
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  uint64_t now = 0;
  int rc = timer_now_ns(&now);
  if (rc != PQC_OK) {
    return rc;
  }
  struct log_record *r = &lg->records[lg->count];
  r->timestamp_ns = now;
  r->event = event;
  r->bytes = bytes;
  r->detail = detail;
  lg->count++;
  return PQC_OK;
}

/** @brief Write one CSV row. */
static int write_row(const struct logger *lg, uint64_t timestamp_ns,
                     enum log_event event, uint32_t bytes, int64_t detail) {
  int n =
      fprintf(lg->fp, "%s,%s,%" PRIu64 ",%s,%s,%" PRIu32 ",%" PRId64 "\n",
              lg->run_id, lg->mode, timestamp_ns, endpoint_names[lg->endpoint],
              event_names[event], bytes, detail);
  return n < 0 ? PQC_ERR_IO : PQC_OK;
}

int logger_flush(struct logger *lg) {
  if (lg == NULL || lg->fp == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  for (size_t i = 0; i < lg->count; i++) {
    const struct log_record *r = &lg->records[i];
    int rc = write_row(lg, r->timestamp_ns, r->event, r->bytes, r->detail);
    if (rc != PQC_OK) {
      return rc;
    }
  }
  lg->count = 0;

  if (lg->dropped > 0) {
    uint64_t now = 0;
    int rc = timer_now_ns(&now);
    if (rc != PQC_OK) {
      return rc;
    }
    rc = write_row(lg, now, LOG_EV_LOG_DROPPED, 0, (int64_t)lg->dropped);
    if (rc != PQC_OK) {
      return rc;
    }
    lg->dropped = 0;
  }

  return fflush(lg->fp) == 0 ? PQC_OK : PQC_ERR_IO;
}

int logger_close(struct logger *lg) {
  if (lg == NULL || lg->fp == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  int rc = logger_flush(lg);
  /* Close even if the flush failed, so the file handle is never leaked. */
  if (fclose(lg->fp) != 0 && rc == PQC_OK) {
    rc = PQC_ERR_IO;
  }
  lg->fp = NULL;
  return rc;
}
