/**
 * @file kem_none.c
 * @brief Unsecured baseline: a key exchange that exchanges nothing.
 *
 * Exists so the none mode runs through exactly the same handshake and kem.h
 * code path as the secured modes, so the measured difference between modes
 * is the key exchange alone. All sizes are 0: nothing is sent or derived.
 */

#include <stdint.h>

#include "common/errors.h"
#include "crypto/kem.h"

/** @brief No key pair to generate. */
static int none_keygen(uint8_t *pk, uint8_t *sk) {
  (void)pk;
  (void)sk;
  return PQC_OK;
}

/** @brief Nothing to encapsulate. */
static int none_encaps(const uint8_t *pk, uint8_t *ct, uint8_t *ss) {
  (void)pk;
  (void)ct;
  (void)ss;
  return PQC_OK;
}

/** @brief Nothing to decapsulate. */
static int none_decaps(const uint8_t *sk, const uint8_t *ct, uint8_t *ss) {
  (void)sk;
  (void)ct;
  (void)ss;
  return PQC_OK;
}

/* ss_len = 0 is what keeps this mode from ever producing a real key:
 * kdf.c rejects a 0-length secret. The no-downgrade check in handshake.c
 * (crypto rule 8) stops a secured session from being talked into this mode. */
const struct kem_ops kem_none_ops = {
    .mode = KEM_MODE_NONE,
    .name = "none",
    .pk_len = 0,
    .sk_len = 0,
    .ct_len = 0,
    .ss_len = 0,
    .keygen = none_keygen,
    .encaps = none_encaps,
    .decaps = none_decaps,
};