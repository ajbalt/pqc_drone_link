#ifndef PQC_ERRORS_H
#define PQC_ERRORS_H

/**
 * @file errors.h
 * @brief Error codes returned by every project function.
 *
 * Convention: functions return int. 0 = success, negative = one of the
 * codes below. Callers must check every return value (CONTRIBUTING.md crypto
 * rule 7).
 *
 * Codes are grouped in ranges of 10 by module, so a number seen in a CSV log
 * tells you where it came from without looking it up.
 *
 * Values are fixed: they get written into experiment logs, so changing one
 * would break comparison with older runs. Add new codes; never renumber.
 *
 * The PQC_ prefix avoids collisions with crypto libraries (e.g. OpenSSL
 * defines ERR_* macros) if one is chosen for the X25519 baseline.
 */

/** @brief Project-wide error codes. Values are fixed; never renumber. */
enum pqc_err {
  PQC_OK = 0, /**< Success */

  /* Generic (-1 .. -9) */
  PQC_ERR_INVALID_ARG = -1,      /**< NULL pointer or out-of-range value */
  PQC_ERR_BUFFER_TOO_SMALL = -2, /**< destination smaller than data to copy */
  PQC_ERR_BAD_CONFIG = -3,       /**< invalid or missing config value */
  PQC_ERR_INTERNAL = -4,         /**< "can't happen" path; indicates a bug */

  /* Randomness, rng.c (-10 .. -19) */
  PQC_ERR_RNG_FAIL = -10, /**< getrandom() failed or returned short;
                             never fall back to a weaker source */

  /* Key exchange, kem.h (-20 .. -29) */
  PQC_ERR_KEM_UNSUPPORTED = -20, /**< mode not compiled in or unknown */
  PQC_ERR_KEM_KEYGEN = -21,      /**< key pair generation failed */
  PQC_ERR_KEM_ENCAPS = -22,      /**< encapsulation failed; includes a malformed
                                    peer public key */
  PQC_ERR_KEM_DECAPS = -23,      /**< decapsulation failed */
  PQC_ERR_KDF_FAIL = -24,        /**< session key derivation failed */

  /* Packet protection, aead_ascon.c (-30 .. -39) */
  PQC_ERR_AEAD_ENCRYPT = -30, /**< encryption failed */
  PQC_ERR_AEAD_AUTH = -31,    /**< decrypt/verify failed. Deliberately one
                                 code for every failure cause, so the
                                 receiver reveals nothing about why. The
                                 packet is dropped, never used as plaintext. */

  /* Session: nonces and replay window, session.c (-40 .. -49) */
  PQC_ERR_REPLAY = -40,         /**< duplicate or too-old sequence number */
  PQC_ERR_REKEY_REQUIRED = -41, /**< packet counter would overflow; nonce
                                   reuse is never allowed, so stop and rekey */
  PQC_ERR_BAD_EPOCH = -42,      /**< packet for a key epoch we don't hold */
  PQC_ERR_NO_SESSION = -43,     /**< encrypt/decrypt before handshake is done */

  /* Fragmentation, fragment.c (-50 .. -59) */
  PQC_ERR_FRAG_MALFORMED = -50,  /**< bad header, index >= count, or length
                                    mismatch */
  PQC_ERR_FRAG_TOO_LARGE = -51,  /**< message exceeds reassembly buffer */
  PQC_ERR_FRAG_TIMEOUT = -52,    /**< retransmit limit reached */
  PQC_ERR_FRAG_INCOMPLETE = -53, /**< not an error to log; more fragments
                                    needed */

  /* Handshake, handshake.c (-60 .. -69) */
  PQC_ERR_HS_BAD_STATE = -60,   /**< message not valid in current state */
  PQC_ERR_HS_BAD_MESSAGE = -61, /**< malformed or wrong-length handshake
                                   message */
  PQC_ERR_HS_MODE_MISMATCH =
      -62,                  /**< peer offered a different KEM. Always
                               fatal: we never downgrade (crypto rule 8) */
  PQC_ERR_HS_TIMEOUT = -63, /**< handshake did not complete in time */
  PQC_ERR_HS_AUTH = -64,    /**< peer failed authentication (PSK/signature) */

  /* Transport, udp.c / serial_radio.c (-70 .. -79) */
  PQC_ERR_IO = -70,            /**< socket or serial read/write failed */
  PQC_ERR_TIMEOUT = -71,       /**< no data within receive timeout */
  PQC_ERR_PKT_TOO_LARGE = -72, /**< packet exceeds link_mtu */
};

#endif /* PQC_ERRORS_H */
