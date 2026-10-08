#ifndef PQC_KEM_H
#define PQC_KEM_H

/**
 * @file kem.h
 * @brief Common interface every key exchange implements.
 *
 * Protocol and application code select a key exchange once, at runtime, from
 * config (kem_get() / kem_from_name()) and then only call through the
 * returned table. They never call a crypto library directly or branch on
 * which KEM is in use (CLAUDE.md crypto rule 2). This is what keeps the key
 * exchange the only variable between modes.
 *
 * Every mode is expressed as a KEM with three operations:
 *   - keygen: responder makes a key pair and sends pk
 *   - encaps: initiator uses pk to produce (ct, ss) and sends ct
 *   - decaps: responder uses sk and ct to recover the same ss
 *
 * X25519 fits this shape: ct is the initiator's ephemeral public key.
 * The none mode fits it with every size equal to 0.
 */

#include <stddef.h>
#include <stdint.h>

/** @brief Largest public key of any supported mode (ML-KEM-1024), bytes. */
#define KEM_MAX_PK_BYTES 1568
/** @brief Largest secret key of any supported mode (ML-KEM-1024), bytes. */
#define KEM_MAX_SK_BYTES 3168
/** @brief Largest ciphertext of any supported mode (ML-KEM-1024), bytes. */
#define KEM_MAX_CT_BYTES 1568
/** @brief Largest shared secret of any supported mode, bytes. */
#define KEM_MAX_SS_BYTES 32

/**
 * @brief Key exchange modes.
 *
 * Values are fixed: they are sent in the handshake and written to logs.
 * Add new modes; never renumber.
 */
enum kem_mode {
  KEM_MODE_NONE = 0,      /**< no key exchange; plaintext baseline */
  KEM_MODE_X25519 = 1,    /**< classical baseline (X25519 ECDH) */
  KEM_MODE_MLKEM512 = 2,  /**< ML-KEM-512 (FIPS 203) */
  KEM_MODE_MLKEM768 = 3,  /**< ML-KEM-768 (FIPS 203), primary PQC mode */
  KEM_MODE_MLKEM1024 = 4, /**< ML-KEM-1024 (FIPS 203) */
};

/**
 * @brief Sizes and operations for one key exchange mode.
 *
 * Call the operations through kem_keygen(), kem_encaps() and kem_decaps(),
 * never directly: the wrappers check every length first, so the
 * implementations can assume correctly sized buffers.
 */
struct kem_ops {
  enum kem_mode mode; /**< which mode this table implements */
  const char *name;   /**< config and log name, e.g. "mlkem768" */
  size_t pk_len;      /**< public key length, bytes */
  size_t sk_len;      /**< secret key length, bytes */
  size_t ct_len;      /**< ciphertext length, bytes */
  size_t ss_len; /**< shared secret length, bytes; 0 for none. kdf.c must reject
                    0 so the none mode can never produce an encryption key */

  int (*keygen)(uint8_t *pk, uint8_t *sk); /**< see kem_keygen() */
  int (*encaps)(const uint8_t *pk, uint8_t *ct,
                uint8_t *ss); /**< see kem_encaps() */
  int (*decaps)(const uint8_t *sk, const uint8_t *ct,
                uint8_t *ss); /**< see kem_decaps() */
};

/**
 * @brief Look up the implementation for a mode.
 * @param[in]  mode  Mode to look up.
 * @param[out] out   Set to the mode's table, or NULL on failure.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p out is NULL,
 *         PQC_ERR_KEM_UNSUPPORTED if the mode is unknown or not built.
 */
int kem_get(enum kem_mode mode, const struct kem_ops **out);

/**
 * @brief Look up the implementation by its config name (e.g. "mlkem768").
 * @param[in]  name  NUL-terminated mode name.
 * @param[out] out   Set to the mode's table, or NULL on failure.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if an argument is NULL,
 *         PQC_ERR_KEM_UNSUPPORTED if the name is unknown or not built.
 */
int kem_from_name(const char *name, const struct kem_ops **out);

/**
 * @brief Generate a key pair (responder side).
 * @param[in]  kem     Mode table from kem_get().
 * @param[out] pk      Public key, kem->pk_len bytes written.
 * @param[in]  pk_cap  Size of @p pk.
 * @param[out] sk      Secret key, kem->sk_len bytes written.
 * @param[in]  sk_cap  Size of @p sk.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if a pointer is NULL,
 *         PQC_ERR_BUFFER_TOO_SMALL if a capacity is too small,
 *         PQC_ERR_KEM_KEYGEN or PQC_ERR_RNG_FAIL if generation fails.
 * @warning Caller must secure_wipe() @p sk once the handshake ends,
 *          on every exit path.
 */
int kem_keygen(const struct kem_ops *kem, uint8_t *pk, size_t pk_cap,
               uint8_t *sk, size_t sk_cap);

/**
 * @brief Encapsulate to the peer's public key (initiator side).
 * @param[in]  kem     Mode table from kem_get().
 * @param[in]  pk      Peer's public key, as received.
 * @param[in]  pk_len  Length of @p pk; must equal kem->pk_len exactly.
 * @param[out] ct      Ciphertext to send, kem->ct_len bytes written.
 * @param[in]  ct_cap  Size of @p ct.
 * @param[out] ss      Shared secret, kem->ss_len bytes written.
 * @param[in]  ss_cap  Size of @p ss.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if a pointer is NULL,
 *         PQC_ERR_BUFFER_TOO_SMALL if a capacity is too small,
 *         PQC_ERR_KEM_ENCAPS if @p pk is malformed or encapsulation fails,
 *         PQC_ERR_RNG_FAIL if randomness is unavailable.
 * @warning Caller must secure_wipe() @p ss after deriving session keys.
 *          Never use @p ss directly as an Ascon key (crypto rule 9).
 */
int kem_encaps(const struct kem_ops *kem, const uint8_t *pk, size_t pk_len,
               uint8_t *ct, size_t ct_cap, uint8_t *ss, size_t ss_cap);

/**
 * @brief Decapsulate the peer's ciphertext (responder side).
 * @param[in]  kem     Mode table from kem_get().
 * @param[in]  sk      Own secret key from kem_keygen().
 * @param[in]  sk_len  Length of @p sk; must equal kem->sk_len.
 * @param[in]  ct      Peer's ciphertext, as received.
 * @param[in]  ct_len  Length of @p ct; must equal kem->ct_len exactly.
 * @param[out] ss      Shared secret, kem->ss_len bytes written.
 * @param[in]  ss_cap  Size of @p ss.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if a pointer is NULL or
 *         @p sk_len is wrong, PQC_ERR_BUFFER_TOO_SMALL if @p ss_cap is too
 *         small, PQC_ERR_KEM_DECAPS if @p ct is malformed or decapsulation
 *         fails.
 * @warning Caller must secure_wipe() @p ss after deriving session keys.
 */
int kem_decaps(const struct kem_ops *kem, const uint8_t *sk, size_t sk_len,
               const uint8_t *ct, size_t ct_len, uint8_t *ss, size_t ss_cap);

#endif /* PQC_KEM_H */
