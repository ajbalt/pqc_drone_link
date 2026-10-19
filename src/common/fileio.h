#ifndef PQC_FILEIO_H
#define PQC_FILEIO_H

/**
 * @file fileio.h
 * @brief Small helpers shared by the durable files (Pi A's queue, and later
 *        Pi B's store): positioned reads and writes, directory sync, CRC-32
 *        and big-endian integers.
 *
 * These are the pieces every append-only file needs for the rules in
 * CONTRIBUTING.md "Protocol rules": write all of an entry, make it durable,
 * and tell a torn write from damage with a CRC.
 */

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/** @brief Paths must be shorter than this, so the parent directory can be
 *         found and synced. */
#define FILEIO_PATH_MAX 4096

/**
 * @brief Read up to @p len bytes at @p off, retrying after signals.
 * @param[in]  fd   Open file.
 * @param[out] buf  Destination, at least @p len bytes.
 * @param[in]  len  Bytes wanted.
 * @param[in]  off  File offset to read from.
 * @param[out] got  Bytes actually read: fewer than @p len only at end of file.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer, PQC_ERR_IO if the
 *         read fails.
 */
int fileio_read_at(int fd, uint8_t *buf, size_t len, off_t off, size_t *got);

/**
 * @brief Write all @p len bytes at @p off, retrying after signals and short
 *        writes.
 * @param[in] fd   Open file.
 * @param[in] buf  Bytes to write.
 * @param[in] len  Number of bytes.
 * @param[in] off  File offset to write at.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer, PQC_ERR_IO if the
 *         write fails.
 * @note This does not fsync(); the caller decides when the data is durable.
 */
int fileio_write_at(int fd, const uint8_t *buf, size_t len, off_t off);

/**
 * @brief fsync() the directory that holds @p path.
 *
 * A new or renamed file's name lives in its directory. Without this, a crash
 * right after creating or renaming a file can leave a synced file that no
 * directory points to.
 *
 * @param[in] path  Path of a file; its parent directory is synced.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL path or one of
 *         FILEIO_PATH_MAX bytes or more, PQC_ERR_IO if the directory cannot
 *         be opened or synced.
 */
int fileio_sync_parent_dir(const char *path);

/**
 * @brief CRC-32 (IEEE 802.3, the one zlib uses).
 * @param[in] p    Bytes to check; may be NULL only if @p len is 0.
 * @param[in] len  Number of bytes.
 * @return The CRC-32 (0 for no bytes).
 * @note It catches torn and damaged writes, not deliberate changes: anyone who
 *       can write the file can also fix up the CRC.
 */
uint32_t fileio_crc32(const uint8_t *p, size_t len);

/**
 * @brief Write @p v at @p p, most significant byte first.
 * @param[out] p  Destination, at least 4 bytes.
 * @param[in]  v  Value.
 */
void fileio_put_be32(uint8_t *p, uint32_t v);

/**
 * @brief Read a big-endian 32-bit value.
 * @param[in] p  Source, at least 4 bytes.
 * @return The value.
 */
uint32_t fileio_get_be32(const uint8_t *p);

/**
 * @brief Write @p v at @p p, most significant byte first.
 * @param[out] p  Destination, at least 8 bytes.
 * @param[in]  v  Value.
 */
void fileio_put_be64(uint8_t *p, uint64_t v);

/**
 * @brief Read a big-endian 64-bit value.
 * @param[in] p  Source, at least 8 bytes.
 * @return The value.
 */
uint64_t fileio_get_be64(const uint8_t *p);

#endif /* PQC_FILEIO_H */
