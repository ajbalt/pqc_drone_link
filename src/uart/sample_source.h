#ifndef PQC_SAMPLE_SOURCE_H
#define PQC_SAMPLE_SOURCE_H

/**
 * @file sample_source.h
 * @brief Pi A's source of sensor samples: reads the UART, finds frames and
 *        turns each one into the start of a record, counting what went wrong.
 *
 * Implements docs/telemetry_format.md section 2 ("Pi A's sample source"). The
 * record it returns has the sample's `pico_ms` and `imu` values and Pi A's
 * wall-clock `ts_ms`; `node` and `seq` are left 0 for queue_append() to fill
 * in (CONTRIBUTING.md crypto rule 15: the queue owns the sequence numbers).
 *
 * Missing samples are found from `pico_ms`: the Pico stamps every sample, so a
 * jump of several periods means samples never arrived (a frame damaged on the
 * cable, or Pi A busy for too long and the driver's buffer overflowing).
 *
 * @warning Not thread-safe.
 */

#include <stddef.h>
#include <stdint.h>

#include "proto/wire.h"

/** @brief An open sample source (opaque). */
struct sample_source;

/** @brief Settings for a sample source. */
struct sample_source_config {
  const char *path;    /**< serial device, such as /dev/serial0 */
  unsigned baud;       /**< 115200 in the spec; see uart_port_open() */
  uint32_t period_ms;  /**< nominal time between samples (100 at 10 Hz) */
  uint32_t max_gap_ms; /**< a jump this long or longer is a restart, not a
                            gap (10000 proposed); must exceed 2 x period_ms */
};

/** @brief How the time between two samples compares with the period. */
enum sample_gap {
  SAMPLE_GAP_NONE,  /**< on time, within jitter */
  SAMPLE_GAP_LOST,  /**< one or more samples never arrived */
  SAMPLE_GAP_EARLY, /**< closer than half a period (including 0) */
  SAMPLE_GAP_RESET, /**< max_gap_ms or more: the Pico restarted or stalled */
};

/** @brief Counters, for the evidence files. They wrap at 2^32. */
struct sample_source_stats {
  uint32_t frames_ok;    /**< good frames delivered */
  uint32_t bad_length;   /**< frame headers with a length that is not allowed */
  uint32_t bad_crc;      /**< complete frames with a wrong CRC */
  uint32_t noise_bytes;  /**< bytes skipped while looking for a frame start */
  uint32_t gaps;         /**< times samples were found missing */
  uint32_t samples_lost; /**< samples found missing, in total */
  uint32_t early;        /**< samples that came too soon after the last */
  uint32_t resets;       /**< jumps of max_gap_ms or more */
};

/**
 * @brief Compare two consecutive samples' `pico_ms`.
 *
 * The difference is taken in unsigned 32-bit arithmetic, so the wrap at 2^32
 * is not a gap. Rules (docs/telemetry_format.md section 2): a difference of
 * max_gap_ms or more is a reset; below half a period is early; 1.5 periods or
 * more is lost samples, round(delta / period) - 1 of them; anything else is on
 * time.
 *
 * @param[in]  prev_ms     `pico_ms` of the previous sample.
 * @param[in]  cur_ms      `pico_ms` of this sample.
 * @param[in]  period_ms   Nominal period; must not be 0.
 * @param[in]  max_gap_ms  Reset threshold; must exceed 2 x @p period_ms.
 * @param[out] kind        The classification.
 * @param[out] lost        Samples missing (nonzero only for SAMPLE_GAP_LOST).
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_BAD_CONFIG for a period or threshold that breaks the rules
 *         above.
 */
int sample_gap_classify(uint32_t prev_ms, uint32_t cur_ms, uint32_t period_ms,
                        uint32_t max_gap_ms, enum sample_gap *kind,
                        uint32_t *lost);

/**
 * @brief Open the serial device and start a source.
 * @param[in]  cfg  Settings.
 * @param[out] out  The source; NULL on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_BAD_CONFIG for a baud rate, period or threshold that is not
 *         allowed, PQC_ERR_IO if the device cannot be opened or configured.
 */
int sample_source_open(const struct sample_source_config *cfg,
                       struct sample_source **out);

/**
 * @brief Return the next sample as a record, waiting up to a timeout for bytes.
 *
 * Returns at most one record per call. If a read brought more than one frame,
 * the rest are returned by the following calls without waiting, so call again
 * until @p got is 0. @p got can be 0 before the timeout has passed (a read
 * that brought only part of a frame), so the caller loops on its own clock.
 *
 * @param[in,out] s           The source.
 * @param[in]     timeout_ms  Longest wait for new bytes; 0 = do not wait.
 * @param[out]    rec         The record: `ts_ms`, `pico_ms` and `imu` filled
 *                            in, `node` and `seq` 0. Zeroed when @p got is 0.
 * @param[out]    got         1 if @p rec holds a sample, 0 if not.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer, PQC_ERR_IO if the
 *         device failed or the Pico side went away.
 * @note `ts_ms` is when Pi A read the frame, not when the Pico sampled: the
 *       driver holds bytes until we read them.
 */
int sample_source_next(struct sample_source *s, unsigned timeout_ms,
                       struct wire_record *rec, int *got);

/**
 * @brief Read the counters.
 * @param[in]  s    The source.
 * @param[out] out  Counters; `uart_bad_frames` in the spec is bad_length +
 * bad_crc.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int sample_source_get_stats(const struct sample_source *s,
                            struct sample_source_stats *out);

/**
 * @brief The serial descriptor, for a caller that waits on it with poll().
 * @param[in]  s   The source.
 * @param[out] fd  The descriptor, still owned by the source.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int sample_source_fd(const struct sample_source *s, int *fd);

/**
 * @brief Close the source and free it. Safe to call with NULL.
 * @param[in] s  Source to close.
 */
void sample_source_close(struct sample_source *s);

#endif /* PQC_SAMPLE_SOURCE_H */
