/**
 * @file test_rng.c
 * @brief Tests for rng_bytes(), secure_wipe() and ct_memcmp().
 *
 * Statistical quality of the randomness is the OS's job; these tests check
 * that our wrapper fills buffers, handles bad arguments, and that the
 * comparison catches a difference at any position.
 */

#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "crypto/rng.h"

/** @brief Number of failed checks; nonzero fails the test. */
static int failures = 0;

/** @brief Record a failed check and keep going, so one run shows them all. */
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/** @brief Return 1 if every byte of @p buf is zero. */
static int all_zero(const uint8_t *buf, size_t len) {
  for (size_t i = 0; i < len; i++) {
    if (buf[i] != 0) {
      return 0;
    }
  }
  return 1;
}

/** @brief rng_bytes() fills buffers and rejects bad arguments. */
static void test_rng_bytes(void) {
  uint8_t a[32] = {0};
  uint8_t b[32] = {0};

  CHECK(rng_bytes(a, sizeof a) == PQC_OK);
  CHECK(rng_bytes(b, sizeof b) == PQC_OK);
  /* Chance of a false failure is 2^-256. */
  CHECK(!all_zero(a, sizeof a));
  CHECK(memcmp(a, b, sizeof a) != 0);

  /* Bigger than one getrandom() call is guaranteed to fill (256 bytes),
   * and the size of an ML-KEM-1024 secret key, to exercise the loop. */
  static uint8_t big[4096];
  CHECK(rng_bytes(big, sizeof big) == PQC_OK);
  CHECK(!all_zero(big + sizeof big - 32, 32)); /* tail was filled too */

  CHECK(rng_bytes(a, 0) == PQC_OK);
  CHECK(rng_bytes(NULL, 16) == PQC_ERR_INVALID_ARG);
}

/** @brief secure_wipe() zeroes exactly the requested bytes. */
static void test_secure_wipe(void) {
  uint8_t buf[16];
  memset(buf, 0xAA, sizeof buf);

  secure_wipe(buf, 8);
  CHECK(all_zero(buf, 8));
  CHECK(buf[8] == 0xAA); /* doesn't run past len */

  secure_wipe(buf, sizeof buf);
  CHECK(all_zero(buf, sizeof buf));

  secure_wipe(NULL, 16); /* must not crash */
}

/** @brief ct_memcmp() detects a difference at any position. */
static void test_ct_memcmp(void) {
  uint8_t a[16];
  uint8_t b[16];
  memset(a, 0x5C, sizeof a);
  memcpy(b, a, sizeof b);

  CHECK(ct_memcmp(a, b, sizeof a) == 0);
  CHECK(ct_memcmp(a, b, 0) == 0);

  /* A single flipped bit at the first, middle and last byte, so a bug
   * that only checks part of the buffer is caught. */
  size_t positions[] = {0, sizeof a / 2, sizeof a - 1};
  for (size_t i = 0; i < sizeof positions / sizeof positions[0]; i++) {
    b[positions[i]] ^= 0x01;
    CHECK(ct_memcmp(a, b, sizeof a) == 1);
    b[positions[i]] ^= 0x01;
  }

  CHECK(ct_memcmp(NULL, b, sizeof b) == 1);
  CHECK(ct_memcmp(a, NULL, sizeof a) == 1);
}

int main(void) {
  test_rng_bytes();
  test_secure_wipe();
  test_ct_memcmp();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}