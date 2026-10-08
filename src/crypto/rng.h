#ifndef PQC_RNG_H
#define PQC_RNG_H

/**
 * @file rng.h
 * @brief Secure randomness, secret wiping, and constant-time comparison.
 *
 * All randomness in the project comes from rng_bytes() (CLAUDE.md crypto
 * rule 3). secure_wipe() and ct_memcmp() live here too because they are the
 * other two primitives every crypto file needs (crypto rules 5 and 6).
 */

 #include <stddef.h>
 #include <stdint.h>

 /**
 * @brief Fill a buffer with cryptographically secure random bytes.
 * @param[out] buf  Destination buffer.
 * @param[in]  len  Number of bytes to write.
 * @return PQC_OK on success, PQC_ERR_INVALID_ARG if @p buf is NULL,
 *         PQC_ERR_RNG_FAIL if getrandom() fails.
 * @note Blocks until the OS entropy pool is initialised (only possible very
 *       early in boot). Never falls back to a weaker source.
 * @warning On failure @p buf is zeroed and must not be used.
 */
int rng_bytes(uint8_t *buf, size_t len);

/**
 * @brief Overwrite a buffer with zeros in a way the compiler cannot remove.
 * @param[out] buf  Buffer holding a secret. May be NULL only if @p len is 0.
 * @param[in]  len  Number of bytes to wipe.
 * @note Returns void, unlike other project functions: it cannot fail, and it
 *       is called on error paths where there is nothing to do with a code.
 */
void secure_wipe(void *buf, size_t len);

/**
 * @brief Compare two buffers in time that depends only on @p len.
 * @param[in] a    First buffer.
 * @param[in] b    Second buffer.
 * @param[in] len  Number of bytes to compare.
 * @return 0 if the buffers are equal, 1 if they differ or either pointer is
 *         NULL. Unlike memcmp() there is no ordering, only equal/not equal.
 * @warning Use this, never memcmp(), for tags, MACs and secrets
 *          (crypto rule 6).
 */
int ct_memcmp(const void *a, const void *b, size_t len);

#endif /* PQC_RNG_H */