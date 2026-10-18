/**
 * @file test_store.c
 * @brief Tests for the durable record store: dedup, restart, capacity and
 *        crash recovery.
 *
 * Crashes are simulated by editing the file between store_close() and
 * store_open(): appending half an entry is what a power cut mid-write leaves
 * behind. Each test uses its own file in a fresh temporary directory.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "common/errors.h"
#include "store/store.h"

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

/** @brief A record with the given seq and distinct values elsewhere. */
static struct wire_record rec_seq(uint64_t seq) {
  struct wire_record r = {
      .node = 1, .seq = seq, .ts_ms = 2, .pico_ms = 3, .imu = {4, -1}};
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

/** @brief Open @p name, insert seqs 1..n, close. */
static void make_store(const char *name, uint64_t n) {
  struct store *s = NULL;
  CHECK(store_open(path_of(name), 100, &s) == PQC_OK);
  for (uint64_t seq = 1; seq <= n; seq++) {
    struct wire_record r = rec_seq(seq);
    int is_new = 0;
    CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  }
  store_close(s);
}

/** @brief Open @p name and return its record count (UINT64_MAX on error). */
static uint64_t count_of(const char *name) {
  struct store *s = NULL;
  uint64_t n = UINT64_MAX;
  if (store_open(path_of(name), 100, &s) == PQC_OK) {
    CHECK(store_count(s, &n) == PQC_OK);
  }
  store_close(s);
  return n;
}

/** @brief The exact file the spec layout gives for one record. */
static const uint8_t expected_file[STORE_HEADER_LEN + STORE_ENTRY_LEN] = {
    'P',  'Q',  'C',  'S',  'T', 'O', 'R', '1', /* header */
    0x01, 0x00, 0x21,                           /* RECORD, length 33 */
    0x01,                                       /* node */
    0,    0,    0,    0,    0,   0,   0,   1,   /* seq */
    0,    0,    0,    0,    0,   0,   0,   2,   /* ts_ms */
    0,    0,    0,    3,                        /* pico_ms */
    0x00, 0x04, 0xFF, 0xFF, 0,   0,   0,   0,   /* ax ay az gx */
    0,    0,    0,    0,                        /* gy gz */
    0x72, 0x76, 0x50, 0x19, /* CRC-32 of the 36 bytes above (zlib.crc32) */
};

/** @brief A new store is just the header; one insert adds one exact entry. */
static void test_layout(void) {
  struct store *s = NULL;
  CHECK(store_open(path_of("layout"), 10, &s) == PQC_OK);
  CHECK(file_size(path_of("layout")) == STORE_HEADER_LEN);
  struct wire_record r = rec_seq(1);
  int is_new = 0;
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  store_close(s);

  uint8_t got[sizeof(expected_file) + 1] = {0};
  FILE *f = fopen(path_of("layout"), "rb");
  CHECK(f != NULL);
  if (f != NULL) {
    CHECK(fread(got, 1, sizeof(got), f) == sizeof(expected_file));
    fclose(f);
  }
  CHECK(memcmp(got, expected_file, sizeof(expected_file)) == 0);
}

/** @brief The same node+seq is stored and counted once (T05). */
static void test_duplicate(void) {
  struct store *s = NULL;
  CHECK(store_open(path_of("dup"), 10, &s) == PQC_OK);
  struct wire_record r = rec_seq(7);
  int is_new = -1;
  uint64_t n = 0;
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  long size_after_one = file_size(path_of("dup"));

  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 0);
  CHECK(store_count(s, &n) == PQC_OK && n == 1);
  CHECK(file_size(path_of("dup")) == size_after_one); /* nothing written */

  /* Same seq from a different node is a different record. */
  r.node = 2;
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  CHECK(store_count(s, &n) == PQC_OK && n == 2);
  store_close(s);
}

/** @brief Late and out-of-order seqs are new; repeats of them are not. */
static void test_out_of_order(void) {
  struct store *s = NULL;
  CHECK(store_open(path_of("order"), 10, &s) == PQC_OK);
  const uint64_t seqs[] = {5, 3, 4, 1, 9};
  int is_new = 0;
  for (size_t i = 0; i < sizeof(seqs) / sizeof(seqs[0]); i++) {
    struct wire_record r = rec_seq(seqs[i]);
    CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  }
  for (size_t i = 0; i < sizeof(seqs) / sizeof(seqs[0]); i++) {
    struct wire_record r = rec_seq(seqs[i]);
    CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 0);
  }
  struct wire_record r = rec_seq(2);
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  uint64_t n = 0;
  CHECK(store_count(s, &n) == PQC_OK && n == 6);
  store_close(s);
}

/** @brief Records and dedup survive a restart (Pi B reboot, L8). */
static void test_restart(void) {
  make_store("restart", 3);
  struct store *s = NULL;
  CHECK(store_open(path_of("restart"), 100, &s) == PQC_OK);
  uint64_t n = 0, cut = 99;
  CHECK(store_count(s, &n) == PQC_OK && n == 3);
  CHECK(store_truncated_bytes(s, &cut) == PQC_OK && cut == 0);

  struct wire_record r = rec_seq(2);
  int is_new = -1;
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 0);
  r = rec_seq(4);
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  store_close(s);
  CHECK(count_of("restart") == 4);
}

/** @brief A full store rejects new records but still recognises repeats. */
static void test_full(void) {
  struct store *s = NULL;
  CHECK(store_open(path_of("full"), 2, &s) == PQC_OK);
  int is_new = 0;
  struct wire_record r = rec_seq(1);
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  r = rec_seq(2);
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);

  r = rec_seq(3);
  is_new = -1;
  CHECK(store_insert(s, &r, &is_new) == PQC_ERR_STORE_FULL && is_new == 0);
  /* A resend after a lost ACK must still get its ACK. */
  r = rec_seq(1);
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 0);
  store_close(s);

  /* Reopening with less room than the file needs is a config error. */
  CHECK(store_open(path_of("full"), 1, &s) == PQC_ERR_BAD_CONFIG);
  CHECK(s == NULL);
}

/** @brief Half an entry at the end (power cut mid-write) is cut off. */
static void test_torn_tail(void) {
  make_store("torn", 2);
  const uint8_t junk[17] = {0x01, 0x00, 0x21};
  append_bytes(path_of("torn"), junk, sizeof(junk));

  struct store *s = NULL;
  CHECK(store_open(path_of("torn"), 100, &s) == PQC_OK);
  uint64_t n = 0, cut = 0;
  CHECK(store_count(s, &n) == PQC_OK && n == 2);
  CHECK(store_truncated_bytes(s, &cut) == PQC_OK && cut == sizeof(junk));
  CHECK(file_size(path_of("torn")) == STORE_HEADER_LEN + 2 * STORE_ENTRY_LEN);
  /* The next record goes where the torn one was, and reads back cleanly. */
  struct wire_record r = rec_seq(3);
  int is_new = 0;
  CHECK(store_insert(s, &r, &is_new) == PQC_OK && is_new == 1);
  store_close(s);
  CHECK(count_of("torn") == 3);
}

/** @brief A damaged last entry is cut off; a damaged earlier one is fatal. */
static void test_bad_crc(void) {
  make_store("crc_last", 3);
  flip_byte(path_of("crc_last"), STORE_HEADER_LEN + 2 * STORE_ENTRY_LEN + 20);
  CHECK(count_of("crc_last") == 2);

  make_store("crc_mid", 3);
  long size = file_size(path_of("crc_mid"));
  flip_byte(path_of("crc_mid"), STORE_HEADER_LEN + STORE_ENTRY_LEN + 20);
  struct store *s = NULL;
  CHECK(store_open(path_of("crc_mid"), 100, &s) == PQC_ERR_STORE_CORRUPT);
  CHECK(s == NULL);
  CHECK(file_size(path_of("crc_mid")) == size); /* evidence left intact */
}

/** @brief A valid entry written twice cannot come from store_insert(). */
static void test_repeated_entry(void) {
  make_store("repeat", 1);
  uint8_t entry[STORE_ENTRY_LEN];
  FILE *f = fopen(path_of("repeat"), "rb");
  CHECK(f != NULL);
  if (f != NULL) {
    CHECK(fseek(f, STORE_HEADER_LEN, SEEK_SET) == 0);
    CHECK(fread(entry, 1, sizeof(entry), f) == sizeof(entry));
    fclose(f);
  }
  append_bytes(path_of("repeat"), entry, sizeof(entry));
  struct store *s = NULL;
  CHECK(store_open(path_of("repeat"), 100, &s) == PQC_ERR_STORE_CORRUPT);
}

/** @brief Someone else's file is refused and left alone; a torn header from
 *         a crash during creation is started again. */
static void test_header(void) {
  const char pem[] = "-----BEGIN CERTIFICATE-----\n";
  append_bytes(path_of("cert"), pem, sizeof(pem) - 1);
  struct store *s = NULL;
  CHECK(store_open(path_of("cert"), 100, &s) == PQC_ERR_STORE_CORRUPT);
  CHECK(file_size(path_of("cert")) == (long)sizeof(pem) - 1);

  append_bytes(path_of("short"), "hi", 2);
  CHECK(store_open(path_of("short"), 100, &s) == PQC_ERR_STORE_CORRUPT);

  append_bytes(path_of("torn_hdr"), "PQC", 3);
  CHECK(store_open(path_of("torn_hdr"), 100, &s) == PQC_OK);
  uint64_t n = 99, cut = 0;
  CHECK(store_count(s, &n) == PQC_OK && n == 0);
  CHECK(store_truncated_bytes(s, &cut) == PQC_OK && cut == 3);
  store_close(s);
  CHECK(file_size(path_of("torn_hdr")) == STORE_HEADER_LEN);

  CHECK(store_open(tmp_dir, 100, &s) == PQC_ERR_IO); /* a directory */
  CHECK(store_open(path_of("no_such_dir/x"), 100, &s) == PQC_ERR_IO);
}

/** @brief NULL pointers, bad sizes and seq 0 are rejected. */
static void test_bad_args(void) {
  struct store *s = NULL;
  CHECK(store_open(NULL, 10, &s) == PQC_ERR_INVALID_ARG && s == NULL);
  CHECK(store_open(path_of("args"), 10, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(store_open(path_of("args"), 0, &s) == PQC_ERR_BAD_CONFIG);
  CHECK(store_open(path_of("args"), SIZE_MAX, &s) == PQC_ERR_BAD_CONFIG);

  static char long_path[STORE_PATH_MAX + 1];
  memset(long_path, 'a', STORE_PATH_MAX);
  CHECK(store_open(long_path, 10, &s) == PQC_ERR_INVALID_ARG);

  CHECK(store_open(path_of("args"), 10, &s) == PQC_OK);
  struct wire_record r = rec_seq(0);
  int is_new = -1;
  uint64_t n = 99;
  CHECK(store_insert(s, &r, &is_new) == PQC_ERR_WIRE_BAD_FIELD && is_new == 0);
  CHECK(store_count(s, &n) == PQC_OK && n == 0);
  CHECK(store_insert(NULL, &r, &is_new) == PQC_ERR_INVALID_ARG);
  CHECK(store_insert(s, NULL, &is_new) == PQC_ERR_INVALID_ARG);
  CHECK(store_insert(s, &r, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(store_count(NULL, &n) == PQC_ERR_INVALID_ARG);
  CHECK(store_count(s, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(store_truncated_bytes(NULL, &n) == PQC_ERR_INVALID_ARG);
  CHECK(store_truncated_bytes(s, NULL) == PQC_ERR_INVALID_ARG);
  store_close(s);
  store_close(NULL);
}

/** @brief Delete the test files and the temporary directory. */
static void cleanup(void) {
  const char *names[] = {"layout", "dup",      "order",   "restart", "full",
                         "torn",   "crc_last", "crc_mid", "repeat",  "cert",
                         "short",  "torn_hdr", "args"};
  for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
    unlink(path_of(names[i]));
  }
  rmdir(tmp_dir);
}

/** @brief Run every test; exit status is nonzero if any check failed. */
int main(void) {
  snprintf(tmp_dir, sizeof(tmp_dir), "/tmp/test_store_XXXXXX");
  if (mkdtemp(tmp_dir) == NULL) {
    fprintf(stderr, "cannot create a temporary directory\n");
    return 1;
  }
  test_layout();
  test_duplicate();
  test_out_of_order();
  test_restart();
  test_full();
  test_torn_tail();
  test_bad_crc();
  test_repeated_entry();
  test_header();
  test_bad_args();
  cleanup();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
