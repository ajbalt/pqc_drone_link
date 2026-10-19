#ifndef PQC_QUEUE_H
#define PQC_QUEUE_H

/**
 * @file queue.h
 * @brief Pi A's persistent queue: records wait here, on disk, until Pi B
 *        ACKs them.
 *
 * Implements docs/telemetry_format.md sections 1, 3 and 7 and CONTRIBUTING.md
 * crypto rule 15. A record is "queued" once queue_append() returns PQC_OK: it
 * has been written and fsync()ed, so it survives a crash or power cut, and
 * only then may the client send it. It stays until queue_ack() is called for
 * it.
 *
 * The queue owns the sequence numbers. queue_append() gives each record the
 * next seq (1, 2, 3, ...), so a seq is never reused, even across restarts and
 * even for a record that was rejected because the queue was full.
 *
 * File layout (all fields big-endian). The file is append-only except for
 * compaction, which replaces it whole with an atomic rename():
 *
 *     header  20 bytes  "PQCQUEU1" (8), next_seq floor (8), CRC-32 (4)
 *                       The CRC covers the 16 bytes before it.
 *     entry   40 bytes  RECORD message (36, as wire_encode_record() writes it)
 *                       + CRC-32 of those 36 bytes (4)
 *     entry   16 bytes  ACK message (12, as wire_encode_ack() writes it)
 *                       + CRC-32 of those 12 bytes (4)
 *     ...               entries in the order they happened
 *
 * The first byte of an entry is the message type, which gives its length. A
 * record is waiting if it has no ACK entry after it. The header's next_seq
 * floor is what stops a seq being reused after compaction has dropped the
 * entries that mentioned it. On open, the next seq is the larger of the floor
 * and one more than the last record in the file.
 *
 * @warning Not thread-safe, and only one process may open a given file. Two
 *          writers would interleave entries and corrupt it.
 * @note If a rejected (queue full) record's seq was the last one handed out
 *       before a restart, the same seq can be handed out again after it. That
 *       is safe: a rejected record was never stored or sent.
 */

#include <stddef.h>
#include <stdint.h>

#include "proto/wire.h"

/** @brief Bytes of magic at the start of the file. */
#define QUEUE_MAGIC_LEN 8
/** @brief Bytes in the file header: magic, next_seq floor and CRC-32. */
#define QUEUE_HEADER_LEN (QUEUE_MAGIC_LEN + 8 + 4)
/** @brief Bytes in a record entry: a RECORD message and its CRC-32. */
#define QUEUE_RECORD_ENTRY_LEN (WIRE_RECORD_MSG_LEN + 4)
/** @brief Bytes in an ACK entry: an ACK message and its CRC-32. */
#define QUEUE_ACK_ENTRY_LEN (WIRE_ACK_MSG_LEN + 4)

/** @brief An open queue (opaque). */
struct queue;

/** @brief Counters for the evidence files (spec section 7). */
struct queue_stats {
  uint64_t pending;         /**< records waiting for an ACK now */
  uint64_t next_seq;        /**< seq the next queue_append() will use */
  uint64_t overflow;        /**< records rejected because the queue was full,
                                 since queue_open() */
  uint64_t peak_pending;    /**< most records waiting at once, since
                                 queue_open() */
  uint64_t truncated_bytes; /**< bytes cut off the file by queue_open() */
};

/**
 * @brief Open a queue file, creating it if it does not exist.
 *
 * Reads every entry to rebuild the list of waiting records. If the last entry
 * is incomplete or fails its CRC (a write cut short by a crash), it is cut
 * off: queue_append() or queue_ack() never returned for it, so nothing
 * depended on it. If the file holds ACKed records, it is then compacted.
 *
 * @param[in]  path         Path of the queue file. A scratch file named
 *                          `<path>.tmp` is used (and replaced) by compaction.
 * @param[in]  node         Sender ID stamped on every record (1 = rover A).
 *                          Must not be 0.
 * @param[in]  max_pending  Most records that may wait for an ACK (a config
 *                          value).
 * @param[out] out          The open queue; NULL on failure.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer or a path that is too
 *         long, PQC_ERR_BAD_CONFIG if @p node or @p max_pending is 0, if
 *         @p max_pending is too large to allocate, or if it is smaller than
 *         the number of records already waiting in the file,
 *         PQC_ERR_QUEUE_CORRUPT if the file is not a queue, or an entry
 *         before the last one is damaged, or an entry makes no sense (a
 *         record out of order or from another node, or an ACK for a record
 *         that is not waiting), PQC_ERR_IO if a file operation fails.
 * @warning PQC_ERR_QUEUE_CORRUPT means records that were never ACKed may be
 *          lost. The file is left untouched; never delete it to "fix" the
 *          error.
 */
int queue_open(const char *path, uint8_t node, size_t max_pending,
               struct queue **out);

/**
 * @brief Give a record the next seq and make it durable.
 *
 * Fills in @p rec->node and @p rec->seq (anything the caller put there is
 * overwritten); the other fields are stored as given. When this returns
 * PQC_OK the record is fsync()ed and may be sent.
 *
 * @param[in]     q    Open queue.
 * @param[in,out] rec  The record to queue. On return it holds the node and seq
 *                     the queue assigned, also when the queue was full, so the
 *                     caller can log the gap.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_QUEUE_FULL if @p max_pending records are already waiting
 *         (the record is not stored and is counted in
 *         queue_stats::overflow; its seq is used up, so the gap in seq shows
 *         the loss),
 *         PQC_ERR_INTERNAL if the seq space is used up,
 *         PQC_ERR_IO if a write or fsync fails, and on every call after one
 *         has failed.
 * @warning Send the record only after PQC_OK (crypto rule 15). After
 *          PQC_ERR_IO, close the queue and open it again.
 */
int queue_append(struct queue *q, struct wire_record *rec);

/**
 * @brief Record that Pi B has ACKed @p seq, and stop keeping the record.
 *
 * The ACK is written to the file before the record leaves memory. An ACK for a
 * record that is not waiting (a second ACK after a resend) changes nothing.
 *
 * @param[in]  q            Open queue.
 * @param[in]  seq          Sequence number from a verified ACK for this
 *                          queue's node.
 * @param[out] was_pending  1 if the record was waiting and is now removed, 0
 *                          if it was not waiting.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer,
 *         PQC_ERR_WIRE_BAD_FIELD if @p seq is 0,
 *         PQC_ERR_QUEUE_BAD_ACK if @p seq is not below the next seq (the
 *         queue never issued it),
 *         PQC_ERR_IO if a write, fsync or compaction fails, and on every call
 *         after one has failed.
 * @warning Call this only for an ACK that passed the wire checks and matches a
 *          record that was actually sent on this connection. After
 *          PQC_ERR_IO, close the queue and open it again.
 */
int queue_ack(struct queue *q, uint64_t seq, int *was_pending);

/**
 * @brief Copy out the @p index-th oldest waiting record.
 *
 * Index 0 is the oldest. Use it to send (and resend) in seq order.
 *
 * @param[in]  q      Open queue.
 * @param[in]  index  0 to pending - 1.
 * @param[out] rec    The record.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer or an index that is
 *         not below the number of waiting records.
 * @note The index of a record changes whenever one before it is ACKed.
 */
int queue_get(const struct queue *q, size_t index, struct wire_record *rec);

/**
 * @brief Read the counters.
 * @param[in]  q    Open queue.
 * @param[out] out  Counters.
 * @return PQC_OK, PQC_ERR_INVALID_ARG for a NULL pointer.
 */
int queue_get_stats(const struct queue *q, struct queue_stats *out);

/**
 * @brief Close the queue and free it. Safe to call with NULL.
 * @param[in] q  Queue to close.
 */
void queue_close(struct queue *q);

#endif /* PQC_QUEUE_H */
