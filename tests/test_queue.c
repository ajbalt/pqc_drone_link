/**
 * @file test_queue.c
 * @brief Tests for the persistent client queue: seq assignment, ACKs,
 *        overflow, restart, compaction and crash recovery.
 *
 * Crashes are simulated by editing the file between queue_close() and
 * queue_open(): appending half an entry is what a power cut mid-write leaves
 * behind. Each test uses its own file in a fresh temporary directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/errors.h"
#include "common/fileio.h"
#include "queue/queue.h"

/** @brief Number of failed checks; nonzero fails the test. */
static int failures = 0;

/** @brief Record a failed check and keep going, so one run shows them all. */
#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      failures++;                                                              \
    }                                                                          \
  } while (0)

/** @brief Temporary directory for this run, from mkdtemp(). */
static char tmp_dir[64];

/** @brief Build the path of test file @p name inside tmp_dir. */
static const char *path_of(const char *name) {
  static char path[128];
  snprintf(path, sizeof(path), "%s/%s", tmp_dir, name);
  return path;
}

/** @brief A record with distinct values; node and seq are the queue's job. */
static struct wire_record sample(int16_t marker) {
  struct wire_record r = {
      .node = 200, .seq = 999, .ts_ms = 2, .pico_ms = 3, .imu = {marker, -1}};
  return r;
}

/** @brief Size of the file at @p path, or -1. */
static long file_size(const char *path) {
  struct stat st;
  return (stat(path, &st) == 0) ? (long)st.st_size : -1;
}

/** @brief Append @p len bytes to the file at @p path. */
static void append_bytes(const char *path, const void *buf, size_t len) {
  FILE *f = fopen(path, "ab");
  CHECK(f != NULL);
  if (f != NULL) {
    CHECK(fwrite(buf, 1, len, f) == len);
    fclose(f);
  }
}

/** @brief Read @p len bytes at @p off of the file at @p path into @p buf. */
static void read_bytes(const char *path, long off, void *buf, size_t len) {
  FILE *f = fopen(path, "rb");
  CHECK(f != NULL);
  if (f != NULL) {
    CHECK(fseek(f, off, SEEK_SET) == 0);
    CHECK(fread(buf, 1, len, f) == len);
    fclose(f);
  }
}

/** @brief Flip the bits of one byte at @p off in the file at @p path. */
static void flip_byte(const char *path, long off) {
  FILE *f = fopen(path, "r+b");
  CHECK(f != NULL);
  if (f == NULL) {
    return;
  }
  CHECK(fseek(f, off, SEEK_SET) == 0);
  int c = fgetc(f);
  CHECK(c != EOF);
  CHECK(fseek(f, off, SEEK_SET) == 0);
  CHECK(fputc(c ^ 0xFF, f) != EOF);
  fclose(f);
}

/** @brief Append @p n records to an open queue and check each is accepted. */
static void append_n(struct queue *q, int n) {
  for (int i = 0; i < n; i++) {
    struct wire_record r = sample((int16_t)i);
    CHECK(queue_append(q, &r) == PQC_OK);
  }
}

/** @brief ACK @p seq and check the queue said it was waiting (@p expect). */
static void ack(struct queue *q, uint64_t seq, int expect) {
  int was = -1;
  CHECK(queue_ack(q, seq, &was) == PQC_OK);
  CHECK(was == expect);
}

/** @brief Seq of the @p i-th waiting record, or 0 if there is none. */
static uint64_t seq_at(const struct queue *q, size_t i) {
  struct wire_record r;
  return (queue_get(q, i, &r) == PQC_OK) ? r.seq : 0;
}

/** @brief Counters of an open queue (all zero if the call fails). */
static struct queue_stats stats_of(const struct queue *q) {
  struct queue_stats st = {0};
  CHECK(queue_get_stats(q, &st) == PQC_OK);
  return st;
}

/** @brief The exact file the spec layout gives: header, one record (seq 1),
 *         then its ACK. */
static const uint8_t
    expected_record_file[QUEUE_HEADER_LEN + QUEUE_RECORD_ENTRY_LEN] = {
        'P',  'Q',  'C',  'Q',  'U', 'E', 'U', '1', /* magic */
        0,    0,    0,    0,    0,   0,   0,   1,   /* next_seq floor */
        0,    0,    0,    0,                        /* CRC-32, checked below */
        0x01, 0x00, 0x21,                           /* RECORD, length 33 */
        0x01,                                       /* node */
        0,    0,    0,    0,    0,   0,   0,   1,   /* seq */
        0,    0,    0,    0,    0,   0,   0,   2,   /* ts_ms */
        0,    0,    0,    3,                        /* pico_ms */
        0x00, 0x04, 0xFF, 0xFF, 0,   0,   0,   0,   /* ax ay az gx */
        0,    0,    0,    0,                        /* gy gz */
        0,    0,    0,    0,                        /* CRC-32, checked below */
};

/** @brief A new queue is just the header; entries have the documented shape.
 */
static void test_layout(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("layout"), 1, 10, &q) == PQC_OK);
  CHECK(file_size(path_of("layout")) == QUEUE_HEADER_LEN);
  struct wire_record r = sample(4);
  CHECK(queue_append(q, &r) == PQC_OK);
  CHECK(r.node == 1 && r.seq == 1); /* the queue overwrote 200 and 999 */
  CHECK(file_size(path_of("layout")) ==
        QUEUE_HEADER_LEN + QUEUE_RECORD_ENTRY_LEN);
  ack(q, 1, 1);
  CHECK(file_size(path_of("layout")) ==
        QUEUE_HEADER_LEN + QUEUE_RECORD_ENTRY_LEN + QUEUE_ACK_ENTRY_LEN);
  queue_close(q);

  /* Everything except the two CRCs is fixed by the spec; the CRCs must be the
   * CRC-32 of the bytes before them (a self-check, with the algorithm itself
   * pinned by test_fileio). Nothing has reopened the file, so the ACK entry
   * is still there to be checked too. */
  uint8_t got[QUEUE_HEADER_LEN + QUEUE_RECORD_ENTRY_LEN + QUEUE_ACK_ENTRY_LEN] =
      {0};
  read_bytes(path_of("layout"), 0, got, sizeof(got));
  uint8_t want[sizeof(expected_record_file)];
  memcpy(want, expected_record_file, sizeof(want));
  fileio_put_be32(want + 16, fileio_crc32(want, 16));
  fileio_put_be32(want + QUEUE_HEADER_LEN + WIRE_RECORD_MSG_LEN,
                  fileio_crc32(want + QUEUE_HEADER_LEN, WIRE_RECORD_MSG_LEN));
  CHECK(memcmp(got, want, sizeof(want)) == 0);

  const uint8_t *ack_entry = got + QUEUE_HEADER_LEN + QUEUE_RECORD_ENTRY_LEN;
  const uint8_t expected_ack[WIRE_ACK_MSG_LEN] = {
      0x02, 0x00, 0x09,                 /* ACK, length 9 */
      0x01,                             /* node */
      0,    0,    0,    0, 0, 0, 0, 1}; /* seq */
  CHECK(memcmp(ack_entry, expected_ack, sizeof(expected_ack)) == 0);
  CHECK(fileio_get_be32(ack_entry + WIRE_ACK_MSG_LEN) ==
        fileio_crc32(ack_entry, WIRE_ACK_MSG_LEN));
}

/** @brief Seqs are 1, 2, 3...; records come back oldest first and intact. */
static void test_append_and_get(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("append"), 1, 10, &q) == PQC_OK);
  struct queue_stats st = stats_of(q);
  CHECK(st.pending == 0 && st.next_seq == 1 && st.overflow == 0);
  append_n(q, 3);
  st = stats_of(q);
  CHECK(st.pending == 3 && st.next_seq == 4 && st.peak_pending == 3);
  for (size_t i = 0; i < 3; i++) {
    struct wire_record r;
    CHECK(queue_get(q, i, &r) == PQC_OK);
    CHECK(r.node == 1 && r.seq == i + 1);
    CHECK(r.ts_ms == 2 && r.pico_ms == 3 && r.imu[0] == (int16_t)i &&
          r.imu[1] == -1);
  }
  struct wire_record r;
  CHECK(queue_get(q, 3, &r) == PQC_ERR_INVALID_ARG);
  queue_close(q);
}

/** @brief ACKs remove records, in order or not; a second ACK does nothing. */
static void test_ack(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("ack"), 1, 10, &q) == PQC_OK);
  append_n(q, 4);
  ack(q, 1, 1);
  CHECK(seq_at(q, 0) == 2);
  long size = file_size(path_of("ack"));
  ack(q, 1, 0);                             /* a second ACK after a resend */
  CHECK(file_size(path_of("ack")) == size); /* and nothing is written */
  ack(q, 3, 1);                             /* out of order */
  CHECK(seq_at(q, 0) == 2 && seq_at(q, 1) == 4 && seq_at(q, 2) == 0);
  struct queue_stats st = stats_of(q);
  CHECK(st.pending == 2 && st.peak_pending == 4);

  int was = 7;
  CHECK(queue_ack(q, 5, &was) == PQC_ERR_QUEUE_BAD_ACK && was == 0);
  CHECK(queue_ack(q, 1000, &was) == PQC_ERR_QUEUE_BAD_ACK);
  CHECK(queue_ack(q, 0, &was) == PQC_ERR_WIRE_BAD_FIELD && was == 0);
  CHECK(stats_of(q).pending == 2); /* the bad ACKs changed nothing */
  queue_close(q);
}

/** @brief The ring wraps around the end of its array without losing order. */
static void test_ring_wrap(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("wrap"), 1, 4, &q) == PQC_OK);
  append_n(q, 4);
  ack(q, 1, 1);
  ack(q, 2, 1);
  append_n(q, 2); /* seq 5 and 6 reuse the first two slots */
  CHECK(seq_at(q, 0) == 3 && seq_at(q, 1) == 4 && seq_at(q, 2) == 5 &&
        seq_at(q, 3) == 6);
  ack(q, 5, 1); /* from the middle, across the wrap */
  CHECK(seq_at(q, 0) == 3 && seq_at(q, 1) == 4 && seq_at(q, 2) == 6 &&
        seq_at(q, 3) == 0);
  queue_close(q);
}

/** @brief Waiting records survive a restart, which also compacts the file. */
static void test_restart(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("restart"), 1, 10, &q) == PQC_OK);
  append_n(q, 5);
  ack(q, 1, 1);
  ack(q, 3, 1);
  queue_close(q);

  CHECK(queue_open(path_of("restart"), 1, 10, &q) == PQC_OK);
  struct queue_stats st = stats_of(q);
  CHECK(st.pending == 3 && st.next_seq == 6 && st.truncated_bytes == 0);
  CHECK(seq_at(q, 0) == 2 && seq_at(q, 1) == 4 && seq_at(q, 2) == 5);
  struct wire_record r;
  CHECK(queue_get(q, 0, &r) == PQC_OK && r.imu[0] == 1); /* data intact */
  /* Compaction dropped the ACKed records and every ACK entry. */
  CHECK(file_size(path_of("restart")) ==
        QUEUE_HEADER_LEN + 3 * QUEUE_RECORD_ENTRY_LEN);
  CHECK(access(path_of("restart.tmp"), F_OK) != 0);
  struct wire_record next = sample(0);
  CHECK(queue_append(q, &next) == PQC_OK && next.seq == 6);
  queue_close(q);
}

/** @brief A seq that was ACKed is never handed out again, even after the
 *         record that used it has been compacted away (crypto rule 15). */
static void test_no_seq_reuse(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("reuse"), 1, 10, &q) == PQC_OK);
  append_n(q, 3);
  ack(q, 1, 1);
  ack(q, 2, 1);
  ack(q, 3, 1); /* nothing is waiting now */
  queue_close(q);

  CHECK(queue_open(path_of("reuse"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 0);
  CHECK(file_size(path_of("reuse")) == QUEUE_HEADER_LEN);
  struct wire_record r = sample(0);
  CHECK(queue_append(q, &r) == PQC_OK && r.seq == 4);
  queue_close(q);

  /* Same, but the last seq was ACKed while an older one still waits. */
  CHECK(queue_open(path_of("reuse2"), 1, 10, &q) == PQC_OK);
  append_n(q, 3);
  ack(q, 3, 1);
  ack(q, 2, 1);
  queue_close(q);
  CHECK(queue_open(path_of("reuse2"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 1 && seq_at(q, 0) == 1);
  r = sample(0);
  CHECK(queue_append(q, &r) == PQC_OK && r.seq == 4);
  queue_close(q);
}

/** @brief A full queue rejects new records, counts them, and leaves a gap in
 *         seq (spec section 7, test L9). */
static void test_overflow(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("full"), 1, 3, &q) == PQC_OK);
  append_n(q, 3);
  long size = file_size(path_of("full"));
  struct wire_record r = sample(0);
  CHECK(queue_append(q, &r) == PQC_ERR_QUEUE_FULL && r.seq == 4);
  r = sample(0);
  CHECK(queue_append(q, &r) == PQC_ERR_QUEUE_FULL && r.seq == 5);
  CHECK(file_size(path_of("full")) == size); /* nothing was written */
  struct queue_stats st = stats_of(q);
  CHECK(st.pending == 3 && st.overflow == 2 && st.next_seq == 6 &&
        st.peak_pending == 3);

  /* Room again: the next record gets seq 6, so 4 and 5 stay a gap. */
  ack(q, 1, 1);
  r = sample(0);
  CHECK(queue_append(q, &r) == PQC_OK && r.seq == 6);
  CHECK(seq_at(q, 0) == 2 && seq_at(q, 1) == 3 && seq_at(q, 2) == 6);
  queue_close(q);

  CHECK(queue_open(path_of("full"), 1, 3, &q) == PQC_OK);
  CHECK(seq_at(q, 2) == 6 && stats_of(q).next_seq == 7);
  queue_close(q);
}

/** @brief The file stays small however many records pass through it. */
static void test_compaction(void) {
  /* A leftover scratch file from a crash must be replaced, not trusted. */
  append_bytes(path_of("compact.tmp"), "junk", 4);
  struct queue *q = NULL;
  CHECK(queue_open(path_of("compact"), 1, 4, &q) == PQC_OK);
  for (int i = 0; i < 40; i++) {
    struct wire_record r = sample((int16_t)i);
    CHECK(queue_append(q, &r) == PQC_OK);
    ack(q, r.seq, 1);
    /* Compaction runs once 4 ACKed records are in the file, so it can never
     * hold more than 4 records and their ACKs. */
    CHECK(file_size(path_of("compact")) <=
          QUEUE_HEADER_LEN +
              4 * (QUEUE_RECORD_ENTRY_LEN + QUEUE_ACK_ENTRY_LEN));
  }
  CHECK(access(path_of("compact.tmp"), F_OK) != 0);
  queue_close(q);

  CHECK(queue_open(path_of("compact"), 1, 4, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 0 && stats_of(q).next_seq == 41);
  queue_close(q);
}

/** @brief An entry cut short by a crash is dropped; the rest is kept. */
static void test_torn_tail(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("torn"), 1, 10, &q) == PQC_OK);
  append_n(q, 2);
  queue_close(q);

  /* Half a record entry: the type byte, then zeros. */
  const uint8_t half[10] = {0x01};
  append_bytes(path_of("torn"), half, sizeof(half));
  CHECK(queue_open(path_of("torn"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 2 && stats_of(q).truncated_bytes == 10);
  CHECK(stats_of(q).next_seq == 3);
  queue_close(q);
  CHECK(file_size(path_of("torn")) ==
        QUEUE_HEADER_LEN + 2 * QUEUE_RECORD_ENTRY_LEN);

  /* Half an ACK entry. */
  CHECK(queue_open(path_of("torn"), 1, 10, &q) == PQC_OK);
  queue_close(q);
  const uint8_t half_ack[5] = {0x02, 0x00, 0x09};
  append_bytes(path_of("torn"), half_ack, sizeof(half_ack));
  CHECK(queue_open(path_of("torn"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 2 && stats_of(q).truncated_bytes == 5);
  queue_close(q);

  /* Zeros: a file that grew but whose data never reached the disk. */
  const uint8_t zeros[30] = {0};
  append_bytes(path_of("torn"), zeros, sizeof(zeros));
  CHECK(queue_open(path_of("torn"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 2 && stats_of(q).truncated_bytes == 30);
  queue_close(q);
}

/** @brief A whole last entry with a bad CRC is cut; the same damage earlier
 *         stops the program and the file is left as it was. */
static void test_bad_crc(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("crc"), 1, 10, &q) == PQC_OK);
  append_n(q, 3);
  queue_close(q);

  flip_byte(path_of("crc"), QUEUE_HEADER_LEN + 2 * QUEUE_RECORD_ENTRY_LEN + 20);
  CHECK(queue_open(path_of("crc"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).pending == 2 && stats_of(q).truncated_bytes == 40);
  queue_close(q);

  /* Damage the first of two entries. */
  flip_byte(path_of("crc"), QUEUE_HEADER_LEN + 20);
  uint8_t before[QUEUE_HEADER_LEN + 2 * QUEUE_RECORD_ENTRY_LEN];
  read_bytes(path_of("crc"), 0, before, sizeof(before));
  CHECK(queue_open(path_of("crc"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);
  CHECK(q == NULL);
  uint8_t after[sizeof(before)];
  read_bytes(path_of("crc"), 0, after, sizeof(after));
  CHECK(file_size(path_of("crc")) == (long)sizeof(before));
  CHECK(memcmp(before, after, sizeof(before)) == 0);
}

/** @brief Entries with a good CRC but a nonsensical meaning stop the program,
 *         even as the last entry. */
static void test_nonsense_entries(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("dupack"), 1, 10, &q) == PQC_OK);
  struct wire_record r = sample(0);
  CHECK(queue_append(q, &r) == PQC_OK);
  queue_close(q); /* header + record; no ACK yet */

  uint8_t record_entry[QUEUE_RECORD_ENTRY_LEN];
  read_bytes(path_of("dupack"), QUEUE_HEADER_LEN, record_entry,
             sizeof(record_entry));
  /* The same record again: seq goes backwards. */
  append_bytes(path_of("dupack"), record_entry, sizeof(record_entry));
  CHECK(queue_open(path_of("dupack"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);

  /* An ACK entry for a record that is not waiting. Build it by ACKing for
   * real in another file, then copy its ACK entry here. */
  CHECK(queue_open(path_of("srcack"), 1, 10, &q) == PQC_OK);
  r = sample(0);
  CHECK(queue_append(q, &r) == PQC_OK);
  ack(q, 1, 1);
  queue_close(q);
  uint8_t ack_entry[QUEUE_ACK_ENTRY_LEN];
  read_bytes(path_of("srcack"), QUEUE_HEADER_LEN + QUEUE_RECORD_ENTRY_LEN,
             ack_entry, sizeof(ack_entry));
  append_bytes(path_of("srcack"), ack_entry, sizeof(ack_entry));
  CHECK(queue_open(path_of("srcack"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);

  /* A file whose records belong to another node. */
  CHECK(queue_open(path_of("node"), 2, 10, &q) == PQC_OK);
  r = sample(0);
  CHECK(queue_append(q, &r) == PQC_OK && r.node == 2);
  queue_close(q);
  CHECK(queue_open(path_of("node"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);
}

/** @brief Files that are not queues are refused and never changed. */
static void test_header(void) {
  struct queue *q = NULL;

  const char foreign[] = "this is somebody else's file, not a queue";
  append_bytes(path_of("foreign"), foreign, sizeof(foreign));
  CHECK(queue_open(path_of("foreign"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);
  CHECK(file_size(path_of("foreign")) == (long)sizeof(foreign));

  /* Twenty zero bytes: the right size for a header, the wrong magic. */
  const uint8_t zeros[QUEUE_HEADER_LEN] = {0};
  append_bytes(path_of("zeros"), zeros, sizeof(zeros));
  CHECK(queue_open(path_of("zeros"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);

  /* A good header with one bit wrong. */
  CHECK(queue_open(path_of("hdrcrc"), 1, 10, &q) == PQC_OK);
  queue_close(q);
  flip_byte(path_of("hdrcrc"), QUEUE_MAGIC_LEN + 7);
  CHECK(queue_open(path_of("hdrcrc"), 1, 10, &q) == PQC_ERR_QUEUE_CORRUPT);

  /* Only the start of a header: a crash while the file was being created. */
  append_bytes(path_of("short"), "PQCQ", 4);
  CHECK(queue_open(path_of("short"), 1, 10, &q) == PQC_OK);
  CHECK(stats_of(q).truncated_bytes == 4 && stats_of(q).next_seq == 1);
  queue_close(q);
  CHECK(file_size(path_of("short")) == QUEUE_HEADER_LEN);

  /* A directory is not a queue. */
  CHECK(queue_open(tmp_dir, 1, 10, &q) != PQC_OK);
  CHECK(q == NULL);
}

/** @brief A smaller max_pending than the file needs is a config error, not
 *         silent loss. */
static void test_small_capacity(void) {
  struct queue *q = NULL;
  CHECK(queue_open(path_of("small"), 1, 10, &q) == PQC_OK);
  append_n(q, 3);
  queue_close(q);
  CHECK(queue_open(path_of("small"), 1, 2, &q) == PQC_ERR_BAD_CONFIG);
  CHECK(q == NULL);
  CHECK(file_size(path_of("small")) ==
        QUEUE_HEADER_LEN + 3 * QUEUE_RECORD_ENTRY_LEN);
  CHECK(queue_open(path_of("small"), 1, 3, &q) == PQC_OK);
  queue_close(q);
}

/** @brief NULL pointers, zero sizes and over-long paths fail cleanly. */
static void test_bad_args(void) {
  struct queue *q = (struct queue *)1;
  CHECK(queue_open(NULL, 1, 10, &q) == PQC_ERR_INVALID_ARG && q == NULL);
  CHECK(queue_open(path_of("args"), 1, 10, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(queue_open(path_of("args"), 0, 10, &q) == PQC_ERR_BAD_CONFIG);
  CHECK(queue_open(path_of("args"), 1, 0, &q) == PQC_ERR_BAD_CONFIG);
  CHECK(queue_open(path_of("args"), 1, SIZE_MAX, &q) == PQC_ERR_BAD_CONFIG);
  CHECK(queue_open("/no/such/directory/queue", 1, 10, &q) == PQC_ERR_IO);

  char *long_path = malloc(FILEIO_PATH_MAX + 2);
  CHECK(long_path != NULL);
  if (long_path != NULL) {
    memset(long_path, 'a', FILEIO_PATH_MAX + 1);
    long_path[FILEIO_PATH_MAX + 1] = '\0';
    CHECK(queue_open(long_path, 1, 10, &q) == PQC_ERR_INVALID_ARG);
    free(long_path);
  }

  CHECK(queue_open(path_of("args"), 1, 10, &q) == PQC_OK);
  struct wire_record r = sample(0);
  int was = 0;
  struct queue_stats st;
  CHECK(queue_append(NULL, &r) == PQC_ERR_INVALID_ARG);
  CHECK(queue_append(q, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(queue_ack(NULL, 1, &was) == PQC_ERR_INVALID_ARG);
  CHECK(queue_ack(q, 1, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(queue_get(NULL, 0, &r) == PQC_ERR_INVALID_ARG);
  CHECK(queue_get(q, 0, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(queue_get(q, 0, &r) == PQC_ERR_INVALID_ARG); /* nothing waiting */
  CHECK(queue_get_stats(NULL, &st) == PQC_ERR_INVALID_ARG);
  CHECK(queue_get_stats(q, NULL) == PQC_ERR_INVALID_ARG);
  queue_close(q);
  queue_close(NULL); /* must be safe */
}

/** @brief Number of steps in the random test. */
#define MODEL_STEPS 3000
/** @brief Capacity used by the random test. */
#define MODEL_CAP 20

/** @brief Next value of a small deterministic generator (no rand(): the run
 *         must be the same every time). */
static uint32_t next_rand(uint32_t *state) {
  *state = *state * 1664525u + 1013904223u;
  return *state >> 8;
}

/** @brief Random appends, ACKs (good, repeated and out of order) and
 *         restarts, checked against a plain array. */
static void test_model(void) {
  struct queue *q = NULL;
  uint64_t model[MODEL_CAP]; /* waiting seqs, oldest first */
  size_t n = 0;
  uint64_t max_accepted = 0;
  uint32_t rng = 12345;
  CHECK(queue_open(path_of("model"), 1, MODEL_CAP, &q) == PQC_OK);

  for (int step = 0; step < MODEL_STEPS && q != NULL; step++) {
    uint32_t op = next_rand(&rng) % 100;
    if (op < 50) {
      struct wire_record r = sample(0);
      int rc = queue_append(q, &r);
      if (n == MODEL_CAP) {
        CHECK(rc == PQC_ERR_QUEUE_FULL);
      } else {
        CHECK(rc == PQC_OK);
        CHECK(r.seq > max_accepted); /* never reused or going backwards */
        model[n++] = r.seq;
        max_accepted = r.seq;
      }
    } else if (op < 85 && n > 0) {
      size_t pick = next_rand(&rng) % n;
      if (next_rand(&rng) % 2 == 0) {
        pick = 0; /* ACKs mostly arrive in order */
      }
      ack(q, model[pick], 1);
      memmove(&model[pick], &model[pick + 1], (n - pick - 1) * sizeof(*model));
      n--;
    } else if (op < 95 && max_accepted > 0) {
      /* An old seq, probably already ACKed: harmless unless it is waiting. */
      uint64_t seq = 1 + next_rand(&rng) % max_accepted;
      int waiting = 0;
      for (size_t i = 0; i < n; i++) {
        waiting |= (model[i] == seq);
      }
      if (!waiting) {
        ack(q, seq, 0);
      }
    } else {
      queue_close(q);
      q = NULL;
      CHECK(queue_open(path_of("model"), 1, MODEL_CAP, &q) == PQC_OK);
      if (q == NULL) {
        break;
      }
      CHECK(stats_of(q).pending == n);
      CHECK(stats_of(q).next_seq > max_accepted);
    }
    if (q != NULL) {
      CHECK(stats_of(q).pending == n);
    }
  }

  CHECK(q != NULL);
  for (size_t i = 0; q != NULL && i < n; i++) {
    CHECK(seq_at(q, i) == model[i]);
  }
  queue_close(q);
}

/** @brief Remove the test files and directory. */
static void cleanup(void) {
  const char *names[] = {"layout", "append",     "ack",    "wrap",    "restart",
                         "reuse",  "reuse2",     "full",   "compact", "torn",
                         "crc",    "dupack",     "srcack", "node",    "foreign",
                         "zeros",  "hdrcrc",     "short",  "small",   "args",
                         "model",  "compact.tmp"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    unlink(path_of(names[i]));
  }
  rmdir(tmp_dir);
}

/** @brief Run every test; exit status is nonzero if any check failed. */
int main(void) {
  snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/test_queue_XXXXXX");
  if (mkdtemp(tmp_dir) == NULL) {
    fprintf(stderr, "cannot create a temporary directory\n");
    return 1;
  }
  test_layout();
  test_append_and_get();
  test_ack();
  test_ring_wrap();
  test_restart();
  test_no_seq_reuse();
  test_overflow();
  test_compaction();
  test_torn_tail();
  test_bad_crc();
  test_nonsense_entries();
  test_header();
  test_small_capacity();
  test_bad_args();
  test_model();
  cleanup();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
