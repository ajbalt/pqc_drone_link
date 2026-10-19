/**
 * @file queue.c
 * @brief Append-only queue file with an in-memory ring of waiting records.
 */

#include "queue/queue.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/errors.h"
#include "common/fileio.h"

/** @brief First bytes of every queue file. The last byte is the format
 *         version, so a future layout change cannot be misread as this one. */
static const uint8_t queue_magic[QUEUE_MAGIC_LEN] = {'P', 'Q', 'C', 'Q',
                                                     'U', 'E', 'U', '1'};

/** @brief Suffix of the scratch file compaction writes before it renames. */
#define QUEUE_TMP_SUFFIX ".tmp"

/** @brief An open queue: the file and the records waiting in it. */
struct queue {
  int fd;                   /**< the queue file, open read-write */
  char *path;               /**< copy of the path, for compaction */
  uint8_t node;             /**< sender ID of every record */
  struct wire_record *recs; /**< ring of waiting records, oldest first */
  size_t max_pending;       /**< capacity of @p recs */
  size_t head;              /**< ring index of the oldest record */
  size_t len;               /**< records waiting */
  uint64_t next_seq;        /**< seq of the next record, never reused */
  off_t end;                /**< file offset where the next entry goes */
  uint64_t acked_in_file;   /**< ACKed records still in the file */
  uint64_t truncated;       /**< bytes cut off by queue_open() */
  uint64_t overflow;        /**< records rejected because the queue was full */
  uint64_t peak;            /**< most records waiting at once */
  int failed;               /**< 1 after a failed write, fsync or compaction */
};

/** @brief The @p i-th oldest waiting record (@p i < len). */
static struct wire_record *slot(const struct queue *q, size_t i) {
  return &q->recs[(q->head + i) % q->max_pending];
}

/** @brief Add @p rec as the newest waiting record. The caller has checked
 *         that there is room. */
static void ring_push(struct queue *q, const struct wire_record *rec) {
  *slot(q, q->len) = *rec;
  q->len++;
  if (q->len > q->peak) {
    q->peak = q->len;
  }
}

/** @brief Find the waiting record with @p seq. Returns 1 if found. */
static int ring_find(const struct queue *q, uint64_t seq, size_t *index) {
  for (size_t i = 0; i < q->len; i++) {
    uint64_t s = slot(q, i)->seq;
    if (s == seq) {
      *index = i;
      return 1;
    }
    if (s > seq) {
      break; /* the ring is sorted by seq, so it cannot be further on */
    }
  }
  return 0;
}

/** @brief Remove the @p index-th waiting record, keeping the order. */
static void ring_remove(struct queue *q, size_t index) {
  if (index == 0) {
    /* The usual case: ACKs arrive in order, so nothing has to move. */
    q->head = (q->head + 1) % q->max_pending;
  } else {
    for (size_t j = index; j + 1 < q->len; j++) {
      *slot(q, j) = *slot(q, j + 1);
    }
  }
  q->len--;
}

/** @brief Build the file header for a given next_seq floor. */
static void encode_header(uint8_t hdr[QUEUE_HEADER_LEN], uint64_t next_seq) {
  memcpy(hdr, queue_magic, QUEUE_MAGIC_LEN);
  fileio_put_be64(hdr + QUEUE_MAGIC_LEN, next_seq);
  fileio_put_be32(hdr + QUEUE_MAGIC_LEN + 8,
                  fileio_crc32(hdr, QUEUE_MAGIC_LEN + 8));
}

/** @brief Build the on-disk entry for @p rec: RECORD message, then CRC. */
static int encode_record_entry(const struct wire_record *rec,
                               uint8_t entry[QUEUE_RECORD_ENTRY_LEN]) {
  /* Reusing the wire encoder means the file and the network share one
   * layout, and its checks (such as seq 0) apply to the file too. */
  size_t len = 0;
  int rc = wire_encode_record(rec, entry, QUEUE_RECORD_ENTRY_LEN, &len);
  if (rc != PQC_OK) {
    return rc;
  }
  if (len != WIRE_RECORD_MSG_LEN) {
    return PQC_ERR_INTERNAL;
  }
  fileio_put_be32(entry + WIRE_RECORD_MSG_LEN,
                  fileio_crc32(entry, WIRE_RECORD_MSG_LEN));
  return PQC_OK;
}

/** @brief Build the on-disk entry for an ACK of node+seq: ACK message, then
 *         CRC. */
static int encode_ack_entry(uint8_t node, uint64_t seq,
                            uint8_t entry[QUEUE_ACK_ENTRY_LEN]) {
  struct wire_ack ack = {.node = node, .seq = seq};
  size_t len = 0;
  int rc = wire_encode_ack(&ack, entry, QUEUE_ACK_ENTRY_LEN, &len);
  if (rc != PQC_OK) {
    return rc;
  }
  if (len != WIRE_ACK_MSG_LEN) {
    return PQC_ERR_INTERNAL;
  }
  fileio_put_be32(entry + WIRE_ACK_MSG_LEN,
                  fileio_crc32(entry, WIRE_ACK_MSG_LEN));
  return PQC_OK;
}

/** @brief Length of an entry that starts with type byte @p type, or 0 if the
 *         type is unknown. */
static size_t entry_len_for(uint8_t type) {
  if (type == WIRE_TYPE_RECORD) {
    return QUEUE_RECORD_ENTRY_LEN;
  }
  if (type == WIRE_TYPE_ACK) {
    return QUEUE_ACK_ENTRY_LEN;
  }
  return 0;
}

/** @brief 1 if the last 4 bytes of the @p len-byte entry are the CRC-32 of the
 *         bytes before them. */
static int entry_crc_ok(const uint8_t *entry, size_t len) {
  return fileio_get_be32(entry + len - 4) == fileio_crc32(entry, len - 4);
}

/** @brief Write the header to an empty or torn file and make it durable. */
static int write_new_file(struct queue *q) {
  uint8_t hdr[QUEUE_HEADER_LEN];
  encode_header(hdr, q->next_seq);
  if (ftruncate(q->fd, 0) != 0 ||
      fileio_write_at(q->fd, hdr, sizeof(hdr), 0) != PQC_OK ||
      fsync(q->fd) != 0) {
    return PQC_ERR_IO;
  }
  q->end = QUEUE_HEADER_LEN;
  return fileio_sync_parent_dir(q->path);
}

/** @brief Apply one entry whose CRC is good to the in-memory state.
 *
 * A good CRC with a nonsensical meaning is never a torn write, so it is
 * PQC_ERR_QUEUE_CORRUPT wherever it is in the file. */
static int apply_entry(struct queue *q, const uint8_t *entry,
                       uint64_t *last_seq) {
  uint8_t type = 0;
  uint16_t len = 0;
  if (wire_parse_header(entry, &type, &len) != PQC_OK) {
    return PQC_ERR_QUEUE_CORRUPT;
  }
  if (type == WIRE_TYPE_RECORD) {
    struct wire_record rec;
    if (wire_decode_record(entry + WIRE_HEADER_LEN, len, &rec) != PQC_OK ||
        rec.node != q->node || rec.seq <= *last_seq || rec.seq == UINT64_MAX) {
      return PQC_ERR_QUEUE_CORRUPT;
    }
    if (q->len == q->max_pending) {
      return PQC_ERR_BAD_CONFIG; /* max_pending is smaller than the file */
    }
    ring_push(q, &rec);
    *last_seq = rec.seq;
    return PQC_OK;
  }
  struct wire_ack ack;
  size_t index = 0;
  if (wire_decode_ack(entry + WIRE_HEADER_LEN, len, &ack) != PQC_OK ||
      ack.node != q->node || !ring_find(q, ack.seq, &index)) {
    return PQC_ERR_QUEUE_CORRUPT; /* ACK of a record that is not waiting */
  }
  ring_remove(q, index);
  q->acked_in_file++;
  return PQC_OK;
}

/** @brief Read every entry; cut off a torn last entry. */
static int load_entries(struct queue *q, off_t size) {
  off_t off = QUEUE_HEADER_LEN;
  uint64_t last_seq = 0;
  while (off < size) {
    uint8_t entry[QUEUE_RECORD_ENTRY_LEN] = {0};
    size_t got = 0;
    int rc = fileio_read_at(q->fd, entry, sizeof(entry), off, &got);
    if (rc != PQC_OK) {
      return rc;
    }
    size_t elen = entry_len_for(entry[0]);
    if (elen == 0 || got < elen || !entry_crc_ok(entry, elen)) {
      /* Appends only ever tear the last entry. Damage anywhere else would
       * mean losing records that were never ACKed, so refuse to start. An
       * unknown type has no known length; allow the longest entry. */
      size_t span = (elen != 0) ? elen : QUEUE_RECORD_ENTRY_LEN;
      if (off + (off_t)span < size) {
        return PQC_ERR_QUEUE_CORRUPT;
      }
      break;
    }
    rc = apply_entry(q, entry, &last_seq);
    if (rc != PQC_OK) {
      return rc;
    }
    off += (off_t)elen;
  }

  /* Never go below the header's floor, or a seq that was ACKed and then
   * compacted away could be handed out again. */
  if (last_seq + 1 > q->next_seq) {
    q->next_seq = last_seq + 1;
  }
  q->end = off;
  q->peak = q->len; /* the peak counts from this open, not from the log */
  if (off < size) {
    /* Safe to drop: queue_append() and queue_ack() return only after fsync()
     * of a whole entry, so nothing depended on a torn one. */
    q->truncated = (uint64_t)(size - off);
    if (ftruncate(q->fd, off) != 0 || fsync(q->fd) != 0) {
      return PQC_ERR_IO;
    }
  }
  return PQC_OK;
}

/**
 * @brief Replace the file with a new one holding only the waiting records.
 *
 * Writes `<path>.tmp`, fsync()s it, and renames it over the queue file, so a
 * crash leaves either the old file or the new one, whole. The new header's
 * next_seq floor carries the seq counter across the entries that are dropped.
 */
static int compact(struct queue *q) {
  size_t path_len = strlen(q->path);
  char tmp_path[FILEIO_PATH_MAX];
  memcpy(tmp_path, q->path, path_len);
  memcpy(tmp_path + path_len, QUEUE_TMP_SUFFIX, sizeof(QUEUE_TMP_SUFFIX));

  size_t total = QUEUE_HEADER_LEN + q->len * QUEUE_RECORD_ENTRY_LEN;
  uint8_t *buf = malloc(total);
  if (buf == NULL) {
    return PQC_ERR_IO;
  }
  encode_header(buf, q->next_seq);
  for (size_t i = 0; i < q->len; i++) {
    int rc = encode_record_entry(
        slot(q, i), buf + QUEUE_HEADER_LEN + i * QUEUE_RECORD_ENTRY_LEN);
    if (rc != PQC_OK) {
      free(buf);
      return rc;
    }
  }

  /* A leftover from a crash during an earlier compaction; it is our scratch
   * file, never the queue, so it is safe to remove. */
  if (unlink(tmp_path) != 0 && errno != ENOENT) {
    free(buf);
    return PQC_ERR_IO;
  }
  int fd = open(tmp_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  int rc = PQC_ERR_IO;
  if (fd >= 0 && fileio_write_at(fd, buf, total, 0) == PQC_OK &&
      fsync(fd) == 0 && rename(tmp_path, q->path) == 0) {
    /* From here the new file is the queue, whatever happens next. */
    close(q->fd);
    q->fd = fd;
    q->end = (off_t)total;
    q->acked_in_file = 0;
    rc = fileio_sync_parent_dir(q->path);
  } else if (fd >= 0) {
    close(fd);
    unlink(tmp_path);
  }
  free(buf);
  return rc;
}

/** @brief Check the open file and either initialise it or load it. */
static int load_file(struct queue *q) {
  struct stat st;
  if (fstat(q->fd, &st) != 0) {
    return PQC_ERR_IO;
  }
  if (!S_ISREG(st.st_mode)) {
    return PQC_ERR_QUEUE_CORRUPT; /* a device or pipe is not a queue */
  }
  uint8_t hdr[QUEUE_HEADER_LEN];
  size_t want =
      (st.st_size < QUEUE_HEADER_LEN) ? (size_t)st.st_size : QUEUE_HEADER_LEN;
  size_t got = 0;
  int rc = fileio_read_at(q->fd, hdr, want, 0, &got);
  if (rc != PQC_OK) {
    return rc;
  }
  if (got != want) {
    return PQC_ERR_IO;
  }
  /* Anything that is not our magic is someone else's file, and is not
   * touched. */
  size_t magic_bytes = (want < QUEUE_MAGIC_LEN) ? want : QUEUE_MAGIC_LEN;
  if (memcmp(hdr, queue_magic, magic_bytes) != 0) {
    return PQC_ERR_QUEUE_CORRUPT;
  }
  if (want < QUEUE_HEADER_LEN) {
    /* Empty, or the start of a header: a crash while creating the file,
     * before any record could be queued. */
    q->truncated = (uint64_t)st.st_size;
    q->next_seq = 1;
    return write_new_file(q);
  }
  if (fileio_get_be32(hdr + QUEUE_MAGIC_LEN + 8) !=
      fileio_crc32(hdr, QUEUE_MAGIC_LEN + 8)) {
    return PQC_ERR_QUEUE_CORRUPT;
  }
  q->next_seq = fileio_get_be64(hdr + QUEUE_MAGIC_LEN);
  if (q->next_seq == 0) {
    return PQC_ERR_QUEUE_CORRUPT; /* seq starts at 1 */
  }
  rc = load_entries(q, st.st_size);
  if (rc == PQC_OK && q->acked_in_file > 0) {
    rc = compact(q);
  }
  return rc;
}

int queue_open(const char *path, uint8_t node, size_t max_pending,
               struct queue **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  /* Room for ".tmp" must remain, so compaction can build its path. */
  if (path == NULL ||
      strlen(path) + sizeof(QUEUE_TMP_SUFFIX) > FILEIO_PATH_MAX - 1) {
    return PQC_ERR_INVALID_ARG;
  }
  if (node == 0 || max_pending == 0 ||
      max_pending > SIZE_MAX / sizeof(struct wire_record)) {
    return PQC_ERR_BAD_CONFIG;
  }

  struct queue *q = calloc(1, sizeof(*q));
  if (q == NULL) {
    return PQC_ERR_BAD_CONFIG;
  }
  q->fd = -1;
  q->node = node;
  q->max_pending = max_pending;
  q->path = strdup(path);
  /* The whole ring is allocated now, so queue_append() never allocates and
   * cannot fail for lack of memory after the record is on disk. */
  q->recs = calloc(max_pending, sizeof(q->recs[0]));
  if (q->path == NULL || q->recs == NULL) {
    queue_close(q);
    return PQC_ERR_BAD_CONFIG;
  }

  /* 0600: only the client's user may read or change the records. */
  q->fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  int rc = (q->fd < 0) ? PQC_ERR_IO : load_file(q);
  if (rc != PQC_OK) {
    queue_close(q);
    return rc;
  }
  *out = q;
  return PQC_OK;
}

int queue_append(struct queue *q, struct wire_record *rec) {
  if (q == NULL || rec == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* After a failed fsync, Linux may already have dropped the unwritten data
   * and cleared the error, so a retry could "succeed" without the record on
   * disk. Only a fresh queue_open(), which reads back what is really there,
   * is trustworthy. */
  if (q->failed) {
    return PQC_ERR_IO;
  }
  if (q->next_seq == UINT64_MAX) {
    return PQC_ERR_INTERNAL; /* the next seq would wrap and be reused */
  }

  /* The seq is used up before the capacity check, so a rejected record leaves
   * a gap in seq: the loss is visible, not hidden (spec section 7). */
  rec->node = q->node;
  rec->seq = q->next_seq++;
  if (q->len == q->max_pending) {
    q->overflow++;
    return PQC_ERR_QUEUE_FULL;
  }

  uint8_t entry[QUEUE_RECORD_ENTRY_LEN];
  int rc = encode_record_entry(rec, entry);
  if (rc != PQC_OK) {
    return rc;
  }
  if (fileio_write_at(q->fd, entry, sizeof(entry), q->end) != PQC_OK ||
      fsync(q->fd) != 0) {
    q->failed = 1;
    return PQC_ERR_IO;
  }
  /* The ring changes only after the record is on disk, so memory never
   * claims a record that a crash could lose. */
  q->end += QUEUE_RECORD_ENTRY_LEN;
  ring_push(q, rec);
  return PQC_OK;
}

int queue_ack(struct queue *q, uint64_t seq, int *was_pending) {
  if (was_pending == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *was_pending = 0;
  if (q == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  if (q->failed) {
    return PQC_ERR_IO;
  }
  if (seq == 0) {
    return PQC_ERR_WIRE_BAD_FIELD;
  }
  if (seq >= q->next_seq) {
    return PQC_ERR_QUEUE_BAD_ACK; /* an ACK for a record we never made */
  }
  size_t index = 0;
  if (!ring_find(q, seq, &index)) {
    return PQC_OK; /* already ACKed, e.g. a second ACK after a resend */
  }

  uint8_t entry[QUEUE_ACK_ENTRY_LEN];
  int rc = encode_ack_entry(q->node, seq, entry);
  if (rc != PQC_OK) {
    return rc;
  }
  /* The ACK is fsync()ed too. Losing it would only cause a resend that Pi B
   * deduplicates, but a lost ACK followed by later entries would leave damage
   * before the last entry, and the file would be refused at the next start. */
  if (fileio_write_at(q->fd, entry, sizeof(entry), q->end) != PQC_OK ||
      fsync(q->fd) != 0) {
    q->failed = 1;
    return PQC_ERR_IO;
  }
  q->end += QUEUE_ACK_ENTRY_LEN;
  ring_remove(q, index);
  q->acked_in_file++;
  *was_pending = 1;

  /* Keep the file from growing for ever: once as many records have been ACKed
   * as the queue can hold, rewrite it. */
  if (q->acked_in_file >= q->max_pending) {
    rc = compact(q);
    if (rc != PQC_OK) {
      q->failed = 1;
      return rc;
    }
  }
  return PQC_OK;
}

int queue_get(const struct queue *q, size_t index, struct wire_record *rec) {
  if (q == NULL || rec == NULL || index >= q->len) {
    return PQC_ERR_INVALID_ARG;
  }
  *rec = *slot(q, index);
  return PQC_OK;
}

int queue_get_stats(const struct queue *q, struct queue_stats *out) {
  if (q == NULL || out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  out->pending = (uint64_t)q->len;
  out->next_seq = q->next_seq;
  out->overflow = q->overflow;
  out->peak_pending = q->peak;
  out->truncated_bytes = q->truncated;
  return PQC_OK;
}

void queue_close(struct queue *q) {
  if (q == NULL) {
    return;
  }
  if (q->fd >= 0) {
    close(q->fd);
  }
  free(q->path);
  free(q->recs);
  free(q);
}
