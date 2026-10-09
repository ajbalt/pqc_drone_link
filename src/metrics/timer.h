#ifndef PQC_TIMER_H
#define PQC_TIMER_H

/**
 * @file timer.h
 * @brief High-resolution wall and CPU timing for measurements.
 *
 * Wall time uses CLOCK_MONOTONIC: it never jumps backwards when NTP or a
 * user changes the date, so differences between two readings are always
 * real elapsed time. It is the same clock Python's time.monotonic_ns()
 * reads on Linux, which lets scripts/power_logger.py samples be aligned
 * with our event logs on the same machine.
 */

#include <stdint.h>

/**
 * @brief Read the monotonic clock.
 * @param[out] out_ns  Nanoseconds since an arbitrary fixed point (boot).
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p out_ns is NULL,
 *         PQC_ERR_INTERNAL if clock_gettime() fails.
 * @note Only differences between readings are meaningful, and only
 *       between readings taken on the same machine.
 */
int timer_now_ns(uint64_t *out_ns);

/**
 * @brief Read the CPU time used by this process so far.
 * @param[out] out_ns  CPU nanoseconds used by all threads of the process.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p out_ns is NULL,
 *         PQC_ERR_INTERNAL if clock_gettime() fails.
 * @note CPU time / wall time over the same interval gives CPU utilization.
 */
int timer_cpu_ns(uint64_t *out_ns);

#endif /* PQC_TIMER_H */