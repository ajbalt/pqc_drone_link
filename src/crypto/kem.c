/**
 * @file kem.c
 * @brief Mode lookup and length checking for the kem.h interface.
 */

#include "crypto/kem.h"

#include <string.h>

#include "common/errors.h"

/* The implementation tables live in kem_*.c. They are declared here rather
 * than in kem.h so the only way to reach one is through kem_get() or
 * kem_from_name(); protocol code can't hard-wire a specific KEM
 * (crypto rule 2). */
extern const struct kem_ops kem_none_ops;

/* Week 2: add &kem_x25519_ops and the three ML-KEM tables here. */
static const struct kem_ops *const kem_table[] = {
    &kem_none_ops,
};

/** @brief Number of entries in kem_taable */
#define KEM_TABLE_LEN (sizeof kem_table / sizeof kem_table[0])

int kem_get(enum kem_mode mode, const struct kem_ops **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* Clear first so a caller that ignores the error can't keep using a
   * table from an earlier call. */
  *out = NULL;

  for (size_t i = 0; i < KEM_TABLE_LEN; i++) {
    if (kem_table[i]->mode == mode) {
      *out = kem_table[i];
      return PQC_OK;
    }
  }
  return PQC_ERR_KEM_UNSUPPORTED;
}

int kem_from_name(const char *name, const struct kem_ops **out) {
  if (name == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;

  for (size_t i = 0; i < KEM_TABLE_LEN; i++) {
    /*Plain strcmp is fine: the mode name comes from config, not a secret. */
    if (strcmp(kem_table[i]->name, name) == 0) {
      *out = kem_table[i];
      return PQC_OK;
    }
  }
  return PQC_ERR_KEM_UNSUPPORTED;
}

int kem_keygen(const struct kem_ops *kem, uint8_t *pk, size_t pk_cap,
               uint8_t *sk, size_t sk_cap) {
  if (kem == NULL || pk == NULL || sk == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (pk_cap < kem->pk_len || sk_cap < kem->sk_len) {
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  return kem->keygen(pk, sk);
}

int kem_encaps(const struct kem_ops *kem, const uint8_t *pk, size_t pk_len,
               uint8_t *ct, size_t ct_cap, uint8_t *ss, size_t ss_cap) {
  if (kem == NULL || pk == NULL || ct == NULL || ss == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* pk came off the radio, so an attacker controls it. Require the exact
   * length, not just "fits": a short key would make the library read past
   * the received data, and a long one would mean ignoring bytes we can't
   * account for. Either way it is a malformed key. */
  if (pk_len != kem->pk_len) {
    return PQC_ERR_KEM_ENCAPS;
  }
  if (ct_cap < kem->ct_len || ss_cap < kem->ss_len) {
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  return kem->encaps(pk, ct, ss);
}

int kem_decaps(const struct kem_ops *kem, const uint8_t *sk, size_t sk_len,
               const uint8_t *ct, size_t ct_len, uint8_t *ss, size_t ss_cap) {
  if (kem == NULL || sk == NULL || ct == NULL || ss == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* sk is our own key, so a wrong length is a bug on our side... */
  if (sk_len != kem->sk_len) {
    return PQC_ERR_INVALID_ARG;
  }
  /* ...but ct came from the peer, so a wrong length is a malformed message. */
  if (ct_len != kem->ct_len) {
    return PQC_ERR_KEM_DECAPS;
  }
  if (ss_cap < kem->ss_len) {
    return PQC_ERR_BUFFER_TOO_SMALL;
  }
  return kem->decaps(sk, ct, ss);
}