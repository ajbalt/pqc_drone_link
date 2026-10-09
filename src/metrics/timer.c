/**
 * @file timer.c
 * @brief clock_gettime() wrappers for wall and CPU time.
 */

#include "metrics/timer.h"

#include <stddef.h>
#include <time.h>

#include "common/errors.h"

/** @brief Read @p clock and convert it to nanoseconds. */
static int read_clock_ns(clockid_t clock, uint64_t *out_ns) {
  if (out_ns == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  struct timespec ts;
  if (clock_gettime(clock, &ts) != 0) {
    return PQC_ERR_INTERNAL;
  }
  /* uint64_t nanoseconds overflows after ~584 years of uptime. */
  *out_ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
  return PQC_OK;
}

int timer_now_ns(uint64_t *out_ns) {
  return read_clock_ns(CLOCK_MONOTONIC, out_ns);
}

int timer_cpu_ns(uint64_t *out_ns) {
  return read_clock_ns(CLOCK_PROCESS_CPUTIME_ID, out_ns);
}