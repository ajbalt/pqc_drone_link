#ifndef PQC_STORE_H
#define PQC_STORE_H

/**
 * @file store.h
 * @brief Pi B's durable record store: append-only file, deduplicated by
 *        node+seq.
 *
 * Implements docs/telemetry_format.md section 6 and CONTRIBUTING.md crypto
 * rules 15 and 16. A record is "committed" once store_insert() returns
 * PQC_OK: it has been written and fsync()ed, so it survives a crash or power
 * cut. Only then may the server send its ACK.
 *
 * File layout (all fields big-endian):
 *
 *     header  8 bytes   "PQCSTOR1"
 *     entry   40 bytes  RECORD message (36, as wire_encode_record() writes it)
 *                       + CRC-32 of those 36 bytes (4)
 *     entry   ...
 *
 * On open the whole file is read back to rebuild the dedup index, so the
 * count after a reboot is the number of records on disk, never a saved
 * variable.
 *
 * @warning Not thread-safe, and only one process may open a given file. Two
 *          writers would interleave entries and corrupt it.
 */

#include <stddef.h>
#include <stdint.h>

#include "proto/wire.h"

/** @brief Bytes in the file header. */
#define STORE_HEADER_LEN 8
/** @brief Bytes in one entry: a RECORD message and its CRC-32. */
#define STORE_ENTRY_LEN (WIRE_RECORD_MSG_LEN + 4)
/** @brief Paths must be shorter than this, so the parent directory can be
 *         found and synced. */
#define STORE_PATH_MAX 4096

/** @brief An open store (opaque). */
struct store;

/**
 * @brief Open a store file, creating it if it does not exist.
 *
 * Reads every entry to rebuild the dedup index. If the last entry is
 * incomplete or fails its CRC (a write cut short by a crash), it is cut off:
 * it was never ACKed, so the client still holds it and will resend it.
 *
 * @param[in]  path         Path of the store file.
 * @param[in]  max_records  Most records the store will hold (a config value).
 * @param[out] out          The open store; NULL on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer or a path of
 *         STORE_PATH_MAX bytes or more,
 *         PQC_ERR_BAD_CONFIG if @p max_records is 0, too large to allocate,
 *         or smaller than the number of records already in the file,
 *         PQC_ERR_STORE_CORRUPT if the file is not a
 *         store or an entry before the last one is damaged or repeated,
 *         PQC_ERR_IO if a file operation fails.
 * @warning PQC_ERR_STORE_CORRUPT means records that were ACKed may be lost.
 *          The file is left untouched; never delete it to "fix" the error.
 */
int store_open(const char *path, size_t max_records, struct store **out);

/**
 * @brief Commit a record unless its node+seq is already stored.
 *
 * A new record is appended and fsync()ed before this returns. A duplicate
 * changes nothing (test T05). Duplicates are checked before capacity, so a
 * resent record is still recognised, and can be ACKed again, when the store
 * is full.
 *
 * @param[in]  s       Open store.
 * @param[in]  rec     Record to commit.
 * @param[out] is_new  1 if the record was added, 0 if it was a duplicate.
 * @return PQC_OK (the record is durable either way, so it may be ACKed),
 *         PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_WIRE_BAD_FIELD if @p rec has seq 0,
 *         PQC_ERR_STORE_FULL if the record is new and the store is full,
 *         PQC_ERR_IO if a write or fsync fails, and on every call after one
 *         has failed.
 * @warning Send the ACK only after PQC_OK (crypto rule 15), and change the
 *          displayed count only when @p is_new is 1 (crypto rule 16). After
 *          PQC_ERR_IO, close the store and open it again.
 */
int store_insert(struct store *s, const struct wire_record *rec, int *is_new);

/**
 * @brief Number of unique records in the store.
 * @param[in]  s      Open store.
 * @param[out] count  Number of records.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int store_count(const struct store *s, uint64_t *count);

/**
 * @brief Bytes cut off the end of the file by the last store_open().
 *
 * Nonzero means the previous run stopped in the middle of a write. Log it.
 *
 * @param[in]  s      Open store.
 * @param[out] bytes  Bytes removed (0 if the file was clean).
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int store_truncated_bytes(const struct store *s, uint64_t *bytes);

/**
 * @brief Close the store and free it. Safe to call with NULL.
 * @param[in] s  Store to close.
 */
void store_close(struct store *s);

#endif /* PQC_STORE_H */