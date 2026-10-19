/**
 * @file mem.c
 * @brief mallinfo2() and getrusage() wrappers for the memory measurements.
 */

#include "metrics/mem.h"

#include <malloc.h>
#include <stddef.h>
#include <sys/resource.h>

#include "common/errors.h"

int mem_heap_in_use(int64_t *out_bytes) {
  if (out_bytes == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* uordblks is memory in malloc's arenas, hblkhd is memory in separate mmap
   * blocks (large allocations). Both are live data. */
  struct mallinfo2 mi = mallinfo2();
  *out_bytes = (int64_t)mi.uordblks + (int64_t)mi.hblkhd;
  return PQC_OK;
}

int mem_peak_rss_kb(uint64_t *out_kb) {
  if (out_kb == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  struct rusage ru;
  if (getrusage(RUSAGE_SELF, &ru) != 0 || ru.ru_maxrss < 0) {
    return PQC_ERR_INTERNAL;
  }
  *out_kb = (uint64_t)ru.ru_maxrss;
  return PQC_OK;
}
