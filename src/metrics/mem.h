#ifndef PQC_MEM_H
#define PQC_MEM_H

/**
 * @file mem.h
 * @brief Memory readings for the measurements: live heap and peak resident
 *        size of this process.
 *
 * Used by the handshake benchmark on both ends (docs/telemetry_format.md
 * section 10). Neither call allocates, so reading them does not change what
 * they report.
 */

#include <stdint.h>

/**
 * @brief Bytes of heap in use by this process right now (malloc and mmap).
 *
 * Take one reading before an operation and one after; the difference is the
 * live memory the operation left behind. It is not the transient peak inside
 * the operation.
 *
 * @param[out] out_bytes  Bytes in use.
 * @return PQC_OK, PQC_ERR_INVALID_ARG if @p out_bytes is NULL.
 * @note Counts every thread's heap. Only differences are meaningful.
 */
int mem_heap_in_use(int64_t *out_bytes);

/**
 * @brief Highest resident set size this process has reached so far.
 *
 * @param[out] out_kb  Kilobytes (1024 bytes), as Linux reports it.
 * @return PQC_OK, PQC_ERR_INVALID_ARG if @p out_kb is NULL, PQC_ERR_INTERNAL
 *         if the system call fails.
 * @note It only ever grows, so one reading at the end of a run is the peak of
 *       the whole run.
 */
int mem_peak_rss_kb(uint64_t *out_kb);

#endif /* PQC_MEM_H */
