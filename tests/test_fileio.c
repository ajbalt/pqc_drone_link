/**
 * @file test_fileio.c
 * @brief Tests for the shared file helpers: CRC-32, big-endian integers,
 *        positioned I/O and directory sync.
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common/errors.h"
#include "common/fileio.h"

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

/** @brief CRC-32 matches the standard check value and zlib's behaviour. */
static void test_crc32(void) {
  const uint8_t digits[] = "123456789";
  CHECK(fileio_crc32(digits, 9) == 0xCBF43926u); /* the published check value */
  CHECK(fileio_crc32(NULL, 0) == 0);
  uint8_t a[4] = {1, 2, 3, 4};
  uint32_t before = fileio_crc32(a, sizeof(a));
  a[2] ^= 0x01; /* one flipped bit must change the CRC */
  CHECK(fileio_crc32(a, sizeof(a)) != before);
}

/** @brief Integers are written most significant byte first. */
static void test_big_endian(void) {
  uint8_t b[8];
  fileio_put_be32(b, 0x01020304u);
  CHECK(b[0] == 1 && b[1] == 2 && b[2] == 3 && b[3] == 4);
  CHECK(fileio_get_be32(b) == 0x01020304u);
  fileio_put_be64(b, 0x0102030405060708ull);
  for (int i = 0; i < 8; i++) {
    CHECK(b[i] == i + 1);
  }
  CHECK(fileio_get_be64(b) == 0x0102030405060708ull);
  fileio_put_be64(b, UINT64_MAX);
  CHECK(fileio_get_be64(b) == UINT64_MAX);
}

/** @brief Write at an offset, read it back, and read past the end. */
static void test_read_write(const char *dir) {
  char path[128];
  snprintf(path, sizeof(path), "%s/rw", dir);
  int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  CHECK(fd >= 0);
  if (fd < 0) {
    return;
  }
  const uint8_t data[6] = {9, 8, 7, 6, 5, 4};
  CHECK(fileio_write_at(fd, data, sizeof(data), 10) == PQC_OK);

  uint8_t back[6] = {0};
  size_t got = 99;
  CHECK(fileio_read_at(fd, back, sizeof(back), 10, &got) == PQC_OK);
  CHECK(got == 6 && memcmp(back, data, 6) == 0);

  /* Asking for more than the file holds is not an error: got says how much. */
  uint8_t big[20] = {0};
  CHECK(fileio_read_at(fd, big, sizeof(big), 10, &got) == PQC_OK);
  CHECK(got == 6);
  CHECK(fileio_read_at(fd, big, sizeof(big), 500, &got) == PQC_OK);
  CHECK(got == 0);

  CHECK(fileio_read_at(fd, NULL, 4, 0, &got) == PQC_ERR_INVALID_ARG);
  CHECK(fileio_read_at(fd, back, 4, 0, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(fileio_write_at(fd, NULL, 4, 0) == PQC_ERR_INVALID_ARG);
  close(fd);

  /* A closed descriptor must come back as an I/O error, not a crash. */
  CHECK(fileio_write_at(fd, data, sizeof(data), 0) == PQC_ERR_IO);
  CHECK(fileio_read_at(fd, back, sizeof(back), 0, &got) == PQC_ERR_IO);
  unlink(path);
}

/** @brief Directory sync works for real paths and refuses bad ones. */
static void test_sync_dir(const char *dir) {
  char path[128];
  snprintf(path, sizeof(path), "%s/some_file", dir);
  CHECK(fileio_sync_parent_dir(path) == PQC_OK);
  CHECK(fileio_sync_parent_dir("some_file_in_cwd") == PQC_OK);
  CHECK(fileio_sync_parent_dir("/no/such/dir/file") == PQC_ERR_IO);
  CHECK(fileio_sync_parent_dir(NULL) == PQC_ERR_INVALID_ARG);

  char *long_path = malloc(FILEIO_PATH_MAX + 2);
  CHECK(long_path != NULL);
  if (long_path != NULL) {
    memset(long_path, 'a', FILEIO_PATH_MAX + 1);
    long_path[FILEIO_PATH_MAX + 1] = '\0';
    CHECK(fileio_sync_parent_dir(long_path) == PQC_ERR_INVALID_ARG);
    free(long_path);
  }
}

/** @brief Run every test; exit status is nonzero if any check failed. */
int main(void) {
  char dir[64];
  snprintf(dir, sizeof(dir), "/tmp/test_fileio_XXXXXX");
  if (mkdtemp(dir) == NULL) {
    fprintf(stderr, "cannot create a temporary directory\n");
    return 1;
  }
  test_crc32();
  test_big_endian();
  test_read_write(dir);
  test_sync_dir(dir);
  rmdir(dir);

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
