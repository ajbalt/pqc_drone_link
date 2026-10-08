/**
 * @file test_kem.c
 * @brief kem.h lookup, argument checks, and a round trip for every mode.
 *
 * Loops over all modes, so new KEMs are covered as soon as they are added
 * to kem_table. Adversarial cases: unknown modes and names, wrong-length
 * peer inputs, undersized buffers, and NULL arguments must all be rejected.
 */

#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "crypto/kem.h"

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

/** @brief Lookup by mode and by name, including unknown values. */
static void test_lookup(void) {
  const struct kem_ops *kem = NULL;

  CHECK(kem_get(KEM_MODE_NONE, &kem) == PQC_OK);
  CHECK(kem != NULL && kem->mode == KEM_MODE_NONE);

  CHECK(kem_from_name("none", &kem) == PQC_OK);
  CHECK(kem != NULL && kem->mode == KEM_MODE_NONE);

  CHECK(kem_from_name("bogus", &kem) == PQC_ERR_KEM_UNSUPPORTED);
  CHECK(kem == NULL); /* never left pointing at an old table */
  CHECK(kem_get((enum kem_mode)99, &kem) == PQC_ERR_KEM_UNSUPPORTED);
  CHECK(kem == NULL);

  CHECK(kem_get(KEM_MODE_NONE, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(kem_from_name(NULL, &kem) == PQC_ERR_INVALID_ARG);
}

/** @brief keygen -> encaps -> decaps gives both sides the same secret. */
static void test_roundtrip(const struct kem_ops *kem) {
  uint8_t pk[KEM_MAX_PK_BYTES] = {0};
  uint8_t sk[KEM_MAX_SK_BYTES] = {0};
  uint8_t ct[KEM_MAX_CT_BYTES] = {0};
  uint8_t ss_a[KEM_MAX_SS_BYTES] = {0};
  uint8_t ss_b[KEM_MAX_SS_BYTES] = {0};

  /* Every mode must fit the fixed buffers protocol code allocates. */
  CHECK(kem->pk_len <= KEM_MAX_PK_BYTES);
  CHECK(kem->sk_len <= KEM_MAX_SK_BYTES);
  CHECK(kem->ct_len <= KEM_MAX_CT_BYTES);
  CHECK(kem->ss_len <= KEM_MAX_SS_BYTES);

  CHECK(kem_keygen(kem, pk, sizeof pk, sk, sizeof sk) == PQC_OK);
  CHECK(kem_encaps(kem, pk, kem->pk_len, ct, sizeof ct, ss_a, sizeof ss_a) ==
        PQC_OK);
  CHECK(kem_decaps(kem, sk, kem->sk_len, ct, kem->ct_len, ss_b, sizeof ss_b) ==
        PQC_OK);
  /* memcmp is fine in a test; timing doesn't matter here. */
  CHECK(memcmp(ss_a, ss_b, kem->ss_len) == 0);
}

/** @brief Malformed peer input and bad arguments are rejected. */
static void test_bad_inputs(const struct kem_ops *kem) {
  uint8_t pk[KEM_MAX_PK_BYTES + 1] = {0};
  uint8_t sk[KEM_MAX_SK_BYTES] = {0};
  uint8_t ct[KEM_MAX_CT_BYTES + 1] = {0};
  uint8_t ss[KEM_MAX_SS_BYTES] = {0};

  /* Truncated or padded peer messages. */
  CHECK(kem_encaps(kem, pk, kem->pk_len + 1, ct, sizeof ct, ss, sizeof ss) ==
        PQC_ERR_KEM_ENCAPS);
  CHECK(kem_decaps(kem, sk, kem->sk_len, ct, kem->ct_len + 1, ss, sizeof ss) ==
        PQC_ERR_KEM_DECAPS);
  if (kem->pk_len > 0) {
    CHECK(kem_encaps(kem, pk, kem->pk_len - 1, ct, sizeof ct, ss, sizeof ss) ==
          PQC_ERR_KEM_ENCAPS);
  }

  /* Output buffer one byte too small. */
  if (kem->ss_len > 0) {
    CHECK(kem_encaps(kem, pk, kem->pk_len, ct, sizeof ct, ss,
                     kem->ss_len - 1) == PQC_ERR_BUFFER_TOO_SMALL);
  }

  /* NULL arguments. */
  CHECK(kem_keygen(NULL, pk, sizeof pk, sk, sizeof sk) == PQC_ERR_INVALID_ARG);
  CHECK(kem_encaps(kem, NULL, kem->pk_len, ct, sizeof ct, ss, sizeof ss) ==
        PQC_ERR_INVALID_ARG);
  CHECK(kem_decaps(kem, sk, kem->sk_len, NULL, kem->ct_len, ss, sizeof ss) ==
        PQC_ERR_INVALID_ARG);
}

int main(void) {
  test_lookup();

  for (int m = KEM_MODE_NONE; m <= KEM_MODE_MLKEM1024; m++) {
    const struct kem_ops *kem = NULL;
    if (kem_get((enum kem_mode)m, &kem) != PQC_OK) {
      printf("mode %d: not built yet, skipped\n", m);
      continue;
    }
    printf("mode %d (%s): testing\n", m, kem->name);
    test_roundtrip(kem);
    test_bad_inputs(kem);
  }

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
