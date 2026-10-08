/**
 * @file rng.c
 * @brief OS CSPRNG wrapper, secure_wipe() and ct_memcmp().
 */

#include "crypto/rng.h"

#include <errno.h>
#include <sys/random.h>
#include <sys/types.h>

#include "common/errors.h"

int rng_bytes(uint8_t *buf, size_t len) {
  if (buf == NULL) {
    return PQC_ERR_INVALID_ARG;
  }

  /* getrandom() may return fewer bytes than asked for large requests, or be
   * interrupted by a signal, so keep going until the buffer is full. */
  size_t done = 0;
  while (done < len) {
    ssize_t n = getrandom(buf + done, len - done, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      /* Don't leave partly random data behind for a caller that ignores the
       * error: zero it, so a misuse shows up as an obviously bad key. */
      secure_wipe(buf, len);
      return PQC_ERR_RNG_FAIL;
    }
    done += (size_t)n;
  }
  return PQC_OK;
}

void secure_wipe(void *buf, size_t len) {
  if (buf == NULL) {
    return;
  }
  /* A plain memset() right before a buffer goes out of scope is a "dead
   * store" the compiler may delete, leaving the secret in memory. Writes
   * through a volatile pointer must be performed, so they can't be removed.
   * This is portable C, so it also works on microcontrollers that lack
   * explicit_bzero() or memset_s(). */
  volatile uint8_t *p = (volatile uint8_t *)buf;
  for (size_t i = 0; i < len; i++) {
    p[i] = 0;
  }
}

int ct_memcmp(const void *a, const void *b, size_t len) {
  if (a == NULL || b == NULL) {
    /* Fail closed: "not equal" means a tag check rejects the packet. */
    return 1;
  }
  const uint8_t *pa = (const uint8_t *)a;
  const uint8_t *pb = (const uint8_t *)b;

  /* memcmp() stops at the first differing byte, so its run time tells an
   * attacker how many leading bytes of a forged tag were right; they can
   * then guess a tag byte by byte. Instead, always look at every byte and
   * OR together the differences. volatile stops the compiler from adding
   * its own early exit. */
  volatile uint8_t diff = 0;
  for (size_t i = 0; i < len; i++) {
    diff |= (uint8_t)(pa[i] ^ pb[i]);
  }
  return diff != 0;
}