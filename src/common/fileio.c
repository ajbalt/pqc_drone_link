/**
 * @file fileio.c
 * @brief Positioned file I/O, directory sync, CRC-32 and big-endian helpers.
 */

#include "common/fileio.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#include "common/errors.h"

int fileio_read_at(int fd, uint8_t *buf, size_t len, off_t off, size_t *got) {
  if (buf == NULL || got == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  *got = 0;
  size_t done = 0;
  while (done < len) {
    ssize_t n = pread(fd, buf + done, len - done, off + (off_t)done);
    if (n < 0) {
      if (errno == EINTR) {
        continue; /* a signal interrupted the call; nothing was lost */
      }
      return PQC_ERR_IO;
    }
    if (n == 0) {
      break; /* end of file */
    }
    done += (size_t)n;
  }
  *got = done;
  return PQC_OK;
}

int fileio_write_at(int fd, const uint8_t *buf, size_t len, off_t off) {
  if (buf == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  size_t done = 0;
  while (done < len) {
    ssize_t n = pwrite(fd, buf + done, len - done, off + (off_t)done);
    if (n <= 0) {
      if (n < 0 && errno == EINTR) {
        continue;
      }
      return PQC_ERR_IO;
    }
    done += (size_t)n;
  }
  return PQC_OK;
}

int fileio_sync_parent_dir(const char *path) {
  if (path == NULL) {
    return PQC_ERR_INVALID_ARG;
  }
  char dir[FILEIO_PATH_MAX];
  size_t len = strlen(path);
  if (len >= sizeof(dir)) {
    return PQC_ERR_INVALID_ARG;
  }
  memcpy(dir, path, len + 1);
  char *slash = strrchr(dir, '/');
  if (slash == NULL) {
    memcpy(dir, ".", 2); /* "queue.dat" lives in the current directory */
  } else if (slash == dir) {
    dir[1] = '\0'; /* "/queue.dat" lives in "/" */
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

uint32_t fileio_crc32(const uint8_t *p, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; i++) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; bit++) {
      crc = (crc & 1u) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
    }
  }
  return crc ^ 0xFFFFFFFFu;
}

void fileio_put_be32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    p[i] = (uint8_t)(v >> (24 - 8 * i));
  }
}

uint32_t fileio_get_be32(const uint8_t *p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}

void fileio_put_be64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    p[i] = (uint8_t)(v >> (56 - 8 * i));
  }
}

uint64_t fileio_get_be64(const uint8_t *p) {
  uint64_t v = 0;
  for (int i = 0; i < 8; i++) {
    v = (v << 8) | p[i];
  }
  return v;
}
