/**
 * @file timer.c
 * @brief clock_gettime() wrappers for wall and CPU time.
 */

#include "metrics/timer.h"

#include <stddef.h>
#include <time.h>

#include "common/errors.h"

/** @brief Read @p clock and convert it to nanoseconds. */