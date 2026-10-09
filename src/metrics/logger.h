#ifndef PQC_LOGGER_H
#define PQC_LOGGER_H

/**
 * @file logger.h
 * @brief CSV event log: one row per event, buffered in memory.
 *
 * Columns: run_id, mode, timestamp_ns, endpoint, event, bytes, detail.
 *
 * logger_event() only stores a record in a caller-provided array: no I/O,
 * no allocation, so it is safe to call inside timed regions and on the
 * packet path. logger_flush() does the file writing and must only be called
 * outside timed regions (CLAUDE.md memory and timing rules).
 *
 * Not thread-safe: use one logger per thread.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/** @brief Max length of run_id, including the terminating NUL. */
#define LOG_RUN_ID_MAX 64
/** @brief Max length of the mode name, including the terminating NUL. */
#define LOG_MODE_MAX 16

/**
 * @brief Events that can be logged.
 *
 * Logs store the name, not the number, so reordering is harmless; but add
 * new events before LOG_EV_COUNT and give each a name in logger.c.
 */
enum log_event {
  LOG_EV_HANDSHAKE_START, /**< handshake begins */
  LOG_EV_HANDSHAKE_DONE,  /**< session keys ready */
  LOG_EV_HANDSHAKE_FAIL,  /**< handshake aborted; detail = error code */
  LOG_EV_FRAG_TX,         /**< fragment sent; detail = fragment index */
  LOG_EV_FRAG_RX,         /**< fragment received; detail = fragment index */
  LOG_EV_FRAG_RETX,       /**< fragment retransmitted; detail = index */
  LOG_EV_PKT_TX,          /**< packet sent; detail = sequence number */
  LOG_EV_PKT_RX,          /**< packet received; detail = sequence number */
  LOG_EV_PKT_DROP,        /**< packet discarded; detail = error code */
  LOG_EV_DECRYPT_FAIL,    /**< AEAD verify failed; detail = sequence number */
  LOG_EV_REKEY_START,     /**< rekey begins; detail = new epoch */
  LOG_EV_REKEY_DONE,      /**< rekey complete; detail = new epoch */
  LOG_EV_LOG_DROPPED,     /**< written by logger_flush(); detail = number of
                               events lost because the buffer was full */
  LOG_EV_COUNT            /**< number of events; not an event */
};

/** @brief Which side of the link wrote the log. */
enum log_endpoint {
  LOG_ENDPOINT_DRONE,  /**< written as "drone" */
  LOG_ENDPOINT_GROUND, /**< written as "ground" */
};

/** @brief One buffered event. */
struct log_record {
  uint64_t timestamp_ns; /**< timer_now_ns() when the event was recorded */
  enum log_event event;  /**< what happened */
  uint32_t bytes;        /**< on-air bytes, including all overhead */
  int64_t detail;        /**< event-specific number, see enum log_event */
};

/** @brief Logger state. Treat as opaque; use the functions below. */
struct logger {
  FILE *fp;                    /**< open CSV file, NULL when closed */
  char run_id[LOG_RUN_ID_MAX]; /**< copied from config */
  char mode[LOG_MODE_MAX];     /**< copied from the kem_ops name */
  enum log_endpoint endpoint;  /**< which side this is */
  struct log_record *records;  /**< caller-provided storage */
  size_t capacity;             /**< number of entries in records */
  size_t count;                /**< entries in use */
  uint64_t dropped;            /**< events lost since the last flush */
};

/**
 * @brief Create a new CSV log file and write the header row.
 * @param[out] lg        Logger to initialise.
 * @param[in]  storage   Array the logger buffers events in. Must outlive
 *                       @p lg; typically a static array.
 * @param[in]  capacity  Number of entries in @p storage (at least 1).
 * @param[in]  path      File to create. Must not already exist.
 * @param[in]  run_id    Run identifier from the experiment config.
 * @param[in]  mode      Mode name, e.g. kem->name.
 * @param[in]  endpoint  Which side of the link this is.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if a pointer is NULL,
 *         @p capacity is 0 or @p endpoint is unknown, PQC_ERR_BAD_CONFIG if
 *         @p run_id or @p mode is empty, too long, or contains a comma,
 *         quote or newline, PQC_ERR_IO if the file exists or can't be
 *         created or written.
 * @note Refusing to open an existing file means a rerun can never silently
 *       overwrite earlier results.
 */
int logger_init(struct logger *lg, struct log_record *storage, size_t capacity,
                const char *path, const char *run_id, const char *mode,
                enum log_endpoint endpoint);

/**
 * @brief Record an event in memory, timestamped now. No I/O.
 * @param[in,out] lg      Initialised logger.
 * @param[in]     event   What happened.
 * @param[in]     bytes   On-air bytes for this event (0 if none).
 * @param[in]     detail  Event-specific number (see enum log_event).
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p lg is NULL or not
 *         open or @p event is unknown, PQC_ERR_BUFFER_TOO_SMALL if the
 *         buffer is full (the event is counted and reported at the next
 *         flush), PQC_ERR_INTERNAL if the clock can't be read.
 */
int logger_event(struct logger *lg, enum log_event event, uint32_t bytes,
                 int64_t detail);

/**
 * @brief Write all buffered events to the file and empty the buffer.
 * @param[in,out] lg  Initialised logger.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p lg is NULL or not
 *         open, PQC_ERR_IO if writing fails.
 * @warning Does file I/O: never call inside a timed region.
 */
int logger_flush(struct logger *lg);

/**
 * @brief Flush remaining events and close the file.
 * @param[in,out] lg  Initialised logger. Closed even if the flush fails.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p lg is NULL or not
 *         open, PQC_ERR_IO if the final write or close fails.
 */
int logger_close(struct logger *lg);

#endif /* PQC_LOGGER_H */