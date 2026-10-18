/**
 * @file store.c
 * @brief Append-only record store with an in-memory dedup index.
 */

#include "store/store.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/errors.h"

/** @brief First bytes of every store file. The last byte is the format
 *         version, so a future layout change cannot be misread as this one. */
static const uint8_t store_magic[STORE_HEADER_LEN] = {'P', 'Q', 'C', 'S',
                                                      'T', 'O', 'R', '1'};

/** @brief One dedup index entry: the unique key of a stored record. */
struct store_key {
  uint64_t seq; /**< record seq */
  uint8_t node; /**< record node */
};

/** @brief An open store: the file and the index rebuilt from it. */
struct store {
  int fd;                 /**< the store file, open read-write */
  struct store_key *keys; /**< sorted by node, then seq */
  size_t count;           /**< keys in use = records in the file */
  size_t max_records;     /**< capacity of @p keys */
  off_t end;              /**< file offset where the next entry goes */
  uint64_t truncated;     /**< bytes cut off by store_open() */
  int failed;             /**< 1 after a failed write or fsync */
};

/** @brief CRC-32 (IEEE 802.3, the one zlib uses), one bit at a time.
 *  @note It catches torn and damaged writes, not deliberate changes: anyone
 *        who can write this file can also fix up the CRC. */
static uint32_t crc32_ieee(const uint8_t *p, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

/** @brief Write @p v at @p p, most significant byte first. */
static void put_be32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = (uint8_t)(v >> (24 - 8 * i));
  }
}

/** @brief Read a big-endian 32-bit value from @p p. */
static uint32_t get_be32(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

/** @brief Read up to @p len bytes at @p off; returns bytes read (fewer only
 *         at end of file) or -1. */
static ssize_t read_at(int fd, uint8_t *buf, size_t len, off_t off) {
  size_t done = 0;
  while (done < len) {
    ssize_t n = pread(fd, buf + done, len - done, off + (off_t)done);
    if (n < 0) {
      if (errno == EINTR) {
        continue; /* a signal interrupted the call; nothing was lost */
      }
      return -1;
    }
    if (n == 0) {
      break; /* end of file */
    }
    done += (size_t)n;
  }
  return (ssize_t)done;
}

/** @brief Write all @p len bytes at @p off; 0 on success, -1 on failure. */
static int write_at(int fd, const uint8_t *buf, size_t len, off_t off) {
  size_t done = 0;
  while (done < len) {
    ssize_t n = pwrite(fd, buf + done, len - done, off + (off_t)done);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      return -1;
    }
    done += (size_t)n;
  }
  return 0;
}

/** @brief fsync() the directory holding @p path.
 *
 * A new file's name lives in its directory. Without this, a crash right after
 * creating the store can leave a synced file that no directory points to. */
static int sync_parent_dir(const char *path) {
  char dir[STORE_PATH_MAX];
  size_t len = strlen(path);
  if (len >= sizeof(dir)) {
    return PQC_ERR_INVALID_ARG;
  }
  memcpy(dir, path, len + 1);
  char *slash = strrchr(dir, '/');
  if (slash == NULL) {
    memcpy(dir, ".", 2); /* "store.dat" lives in the current directory */
  } else if (slash == dir) {
    dir[1] = '\0'; /* "/store.dat" lives in "/" */
  } else {
    *slash = '\0';
  }
  int dfd = open(dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dfd < 0) {
    return PQC_ERR_IO;
  }
  int rc = (fsync(dfd) == 0) ? PQC_OK : PQC_ERR_IO;
  close(dfd);
  return rc;
}

/** @brief Compare an index key with node+seq: <0, 0 or >0. */
static int key_cmp(const struct store_key *k, uint8_t node, uint64_t seq) {
  if (k->node != node) {
    return (k->node < node) ? -1 : 1;
  }
  if (k->seq != seq) {
    return (k->seq < seq) ? -1 : 1;
  }
  return 0;
}

/** @brief Binary search for node+seq. Returns 1 if found. Sets @p pos to
 *         where the key is, or to where it would have to be inserted. */
static int key_find(const struct store *s, uint8_t node, uint64_t seq,
                    size_t *pos) {
  size_t lo = 0;
  size_t hi = s->count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2; /* (lo + hi) / 2 could overflow */
    int c = key_cmp(&s->keys[mid], node, seq);
    if (c == 0) {
      *pos = mid;
      return 1;
    }
    if (c < 0) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  *pos = lo;
  return 0;
}

/** @brief Insert node+seq at @p pos. The caller has checked that the key is
 *         absent and that there is room. */
static void key_insert(struct store *s, size_t pos, uint8_t node,
                       uint64_t seq) {
  /* Records normally arrive in seq order, so pos == count and nothing moves.
   * A resend after a lost ACK can arrive late; then the tail shifts by one. */
  memmove(&s->keys[pos + 1], &s->keys[pos],
          (s->count - pos) * sizeof(s->keys[0]));
  s->keys[pos].node = node;
  s->keys[pos].seq = seq;
  s->count++;
}

/** @brief Build the on-disk entry for @p rec: RECORD message, then CRC. */
static int encode_entry(const struct wire_record *rec,
                        uint8_t entry[STORE_ENTRY_LEN]) {
  /* Reusing the wire encoder means the file and the network share one
   * layout, and its checks (such as seq 0) apply to the file too. */
  size_t len = 0;
  int rc = wire_encode_record(rec, entry, STORE_ENTRY_LEN, &len);
  if (rc != PQC_OK) {
    return rc;
  }
  if (len != WIRE_RECORD_MSG_LEN) {
    return PQC_ERR_INTERNAL;
  }
  put_be32(entry + WIRE_RECORD_MSG_LEN, crc32_ieee(entry, WIRE_RECORD_MSG_LEN));
  return PQC_OK;
}

/** @brief Check an entry's CRC and decode its record. */
static int decode_entry(const uint8_t entry[STORE_ENTRY_LEN],
                        struct wire_record *rec) {
  if (get_be32(entry + WIRE_RECORD_MSG_LEN) !=
      crc32_ieee(entry, WIRE_RECORD_MSG_LEN)) {
    return PQC_ERR_STORE_CORRUPT;
  }
  uint8_t type = 0;
  uint16_t len = 0;
  if (wire_parse_header(entry, &type, &len) != PQC_OK ||
      type != WIRE_TYPE_RECORD ||
      wire_decode_record(entry + WIRE_HEADER_LEN, len, rec) != PQC_OK) {
    return PQC_ERR_STORE_CORRUPT;
  }
  return PQC_OK;
}

/** @brief Check the header of a file of @p size bytes.
 *
 * Sets @p fresh to 1 if the file is empty or holds only the start of the
 * header: a crash during creation, before any record could be ACKed. Anything
 * else that is not our magic is someone else's file, and is not touched. */
static int check_header(int fd, off_t size, int *fresh) {
  uint8_t hdr[STORE_HEADER_LEN];
  size_t want = (size < STORE_HEADER_LEN) ? (size_t)size : STORE_HEADER_LEN;
  if (read_at(fd, hdr, want, 0) != (ssize_t)want) {
    return PQC_ERR_IO;
  }
  if (memcmp(hdr, store_magic, want) != 0) {
    return PQC_ERR_STORE_CORRUPT;
  }
  *fresh = (want < STORE_HEADER_LEN);
  return PQC_OK;
}

/** @brief Write the header to an empty or torn file and make it durable. */
static int write_header(int fd, const char *path) {
  if (ftruncate(fd, 0) != 0 ||
      write_at(fd, store_magic, STORE_HEADER_LEN, 0) != 0 || fsync(fd) != 0) {
    return PQC_ERR_IO;
  }
  return sync_parent_dir(path);
}

/** @brief Read every entry into the index; cut off a torn last entry. */
static int load_entries(struct store *s, off_t size) {
  off_t off = STORE_HEADER_LEN;
  while (off < size) {
    uint8_t entry[STORE_ENTRY_LEN];
    ssize_t n = read_at(s->fd, entry, sizeof(entry), off);
    if (n < 0) {
      return PQC_ERR_IO;
    }
    struct wire_record rec;
    if ((size_t)n < sizeof(entry) || decode_entry(entry, &rec) != PQC_OK) {
      /* Appends only ever tear the last entry. Damage anywhere else would
       * mean losing records that were ACKed, so refuse to start. */
      if (off + (off_t)STORE_ENTRY_LEN < size) {
        return PQC_ERR_STORE_CORRUPT;
      }
      break;
    }
    size_t pos = 0;
    if (key_find(s, rec.node, rec.seq, &pos)) {
      return PQC_ERR_STORE_CORRUPT; /* store_insert() never writes a copy */
    }
    if (s->count == s->max_records) {
      return PQC_ERR_BAD_CONFIG;
    }
    key_insert(s, pos, rec.node, rec.seq);
    off += STORE_ENTRY_LEN;
  }

  s->end = off;
  if (off < size) {
    /* Safe to drop: the ACK is sent only after fsync() of a whole entry, so
     * a torn entry was never ACKed and the client still has it. */
    s->truncated = (uint64_t)(size - off);
    if (ftruncate(s->fd, off) != 0 || fsync(s->fd) != 0) {
      return PQC_ERR_IO;
    }
  }
  return PQC_OK;
}

/** @brief Check the open file and either initialise it or load it. */
static int load_file(struct store *s, const char *path) {
  struct stat st;
  if (fstat(s->fd, &st) != 0) {
    return PQC_ERR_IO;
  }
  if (!S_ISREG(st.st_mode)) {
    return PQC_ERR_STORE_CORRUPT; /* a device or pipe is not a store */
  }
  int fresh = 0;
  int rc = check_header(s->fd, st.st_size, &fresh);
  if (rc != PQC_OK) {
    return rc;
  }
  if (fresh) {
    s->truncated = (uint64_t)st.st_size;
    s->end = STORE_HEADER_LEN;
    return write_header(s->fd, path);
  }
  return load_entries(s, st.st_size);
}

int store_open(const char *path, size_t max_records, struct store **out) {
  if (out == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *out = NULL;
  if (path == NULL || strlen(path) >= STORE_PATH_MAX) {
    return PQC_ERR_INVALID_ARG;
  }
  if (max_records == 0 || max_records > SIZE_MAX / sizeof(struct store_key)) {
    return PQC_ERR_BAD_CONFIG;
  }

  struct store *s = calloc(1, sizeof(*s));
  if (s == NULL) {
    return PQC_ERR_BAD_CONFIG;
  }
  s->fd = -1;
  s->max_records = max_records;
  /* The whole index is allocated now, so an insert never allocates and
   * cannot fail for lack of memory after the record is on disk. */
  s->keys = calloc(max_records, sizeof(s->keys[0]));
  if (s->keys == NULL) {
    store_close(s);
    return PQC_ERR_BAD_CONFIG;
  }

  /* 0600: only the server's user may read or change the records. */
  s->fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  int rc = (s->fd < 0) ? PQC_ERR_IO : load_file(s, path);
  if (rc != PQC_OK) {
    store_close(s);
    return rc;
  }
  *out = s;
  return PQC_OK;
}

int store_insert(struct store *s, const struct wire_record *rec, int *is_new) {
  if (is_new == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *is_new = 0;
  if (s == NULL || rec == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  /* After a failed fsync, Linux may already have dropped the unwritten data
   * and cleared the error, so a retry could "succeed" without the record on
   * disk. Only a fresh store_open(), which reads back what is really there,
   * is trustworthy. */
  if (s->failed) {
    return PQC_ERR_IO;
  }

  uint8_t entry[STORE_ENTRY_LEN];
  int rc = encode_entry(rec, entry);
  if (rc != PQC_OK) {
    return rc;
  }
  size_t pos = 0;
  if (key_find(s, rec->node, rec->seq, &pos)) {
    return PQC_OK; /* duplicate: already durable, ACK it again (T05) */
  }
  if (s->count == s->max_records) {
    return PQC_ERR_STORE_FULL;
  }

  if (write_at(s->fd, entry, sizeof(entry), s->end) != 0 || fsync(s->fd) != 0) {
    s->failed = 1;
    return PQC_ERR_IO;
  }
  /* The index changes only after the record is on disk, so memory never
   * claims a record that a crash could lose. */
  s->end += STORE_ENTRY_LEN;
  key_insert(s, pos, rec->node, rec->seq);
  *is_new = 1;
  return PQC_OK;
}

int store_count(const struct store *s, uint64_t *count) {
  if (s == NULL || count == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *count = (uint64_t)s->count;
  return PQC_OK;
}

int store_truncated_bytes(const struct store *s, uint64_t *bytes) {
  if (s == NULL || bytes == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *bytes = s->truncated;
  return PQC_OK;
}

void store_close(struct store *s) {
  if (s == NULL) {
    return;
  }
  if (s->fd >= 0) {
    close(s->fd);
  }
  free(s->keys);
  free(s);
}