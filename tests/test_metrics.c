/**
 * @file test_metrics.c
 * @brief Tests for timer.c and logger.c.
 *
 * Adversarial cases: config strings that would corrupt the CSV, an existing
 * log file that must not be overwritten, a full event buffer, unknown
 * events, and use after close.
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "common/errors.h"
#include "metrics/logger.h"
#include "metrics/timer.h"

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

/** @brief Log file used by these tests, created in ctest's working dir. */
#define TEST_LOG "test_metrics.csv"

/** @brief One parsed CSV row. */
struct row {
  char run_id[LOG_RUN_ID_MAX]; /**< column 1 */
  char mode[LOG_MODE_MAX];     /**< column 2 */
  uint64_t timestamp_ns;       /**< column 3 */
  char endpoint[16];           /**< column 4 */
  char event[32];              /**< column 5 */
  uint32_t bytes;              /**< column 6 */
  int64_t detail;              /**< column 7 */
};

/** @brief Parse one CSV line; returns 1 if all 7 columns were read. */
static int parse_row(const char *line, struct row *r) {
  return sscanf(line,
                "%63[^,],%15[^,],%" SCNu64 ",%15[^,],%31[^,],%" SCNu32
                ",%" SCNd64,
                r->run_id, r->mode, &r->timestamp_ns, r->endpoint, r->event,
                &r->bytes, &r->detail) == 7;
}

/** @brief Wall clock never goes backwards; CPU clock advances with work. */
static void test_timer(void) {
  uint64_t t1 = 0;
  uint64_t t2 = 0;
  CHECK(timer_now_ns(&t1) == PQC_OK);
  CHECK(timer_now_ns(&t2) == PQC_OK);
  CHECK(t2 >= t1);

  uint64_t c1 = 0;
  uint64_t c2 = 0;
  CHECK(timer_cpu_ns(&c1) == PQC_OK);
  volatile uint64_t sink = 0;
  for (uint64_t i = 0; i < 10000000; i++) {
    sink += i;
  }
  CHECK(timer_cpu_ns(&c2) == PQC_OK);
  CHECK(c2 > c1);

  CHECK(timer_now_ns(NULL) == PQC_ERR_INVALID_ARG);
  CHECK(timer_cpu_ns(NULL) == PQC_ERR_INVALID_ARG);
}

/** @brief Bad config is rejected before any file is created. */
static void test_logger_bad_config(void) {
  struct log_record storage[4];
  struct logger lg;
  char long_id[LOG_RUN_ID_MAX + 1];
  memset(long_id, 'a', sizeof long_id - 1);
  long_id[sizeof long_id - 1] = '\0';

  remove(TEST_LOG);
  CHECK(logger_init(&lg, storage, 4, TEST_LOG, "r,1", "none",
                    LOG_ENDPOINT_DRONE) == PQC_ERR_BAD_CONFIG);
  CHECK(logger_init(&lg, storage, 4, TEST_LOG, "r1\n", "none",
                    LOG_ENDPOINT_DRONE) == PQC_ERR_BAD_CONFIG);
  CHECK(logger_init(&lg, storage, 4, TEST_LOG, "", "none",
                    LOG_ENDPOINT_DRONE) == PQC_ERR_BAD_CONFIG);
  CHECK(logger_init(&lg, storage, 4, TEST_LOG, long_id, "none",
                    LOG_ENDPOINT_DRONE) == PQC_ERR_BAD_CONFIG);
  CHECK(logger_init(&lg, storage, 0, TEST_LOG, "r1", "none",
                    LOG_ENDPOINT_DRONE) == PQC_ERR_INVALID_ARG);
  CHECK(logger_init(&lg, storage, 4, TEST_LOG, "r1", "none",
                    (enum log_endpoint)7) == PQC_ERR_INVALID_ARG);
  CHECK(logger_init(&lg, NULL, 4, TEST_LOG, "r1", "none", LOG_ENDPOINT_DRONE) ==
        PQC_ERR_INVALID_ARG);

  /* None of the failures above may have left a file behind. */
  FILE *fp = fopen(TEST_LOG, "r");
  CHECK(fp == NULL);
  if (fp != NULL) {
    fclose(fp);
  }
}

/** @brief Events round-trip to CSV; overflow and misuse are handled. */
static void test_logger_roundtrip(void) {
  struct log_record storage[2];
  struct logger lg;

  remove(TEST_LOG);
  CHECK(logger_init(&lg, storage, 2, TEST_LOG, "run-7", "mlkem768",
                    LOG_ENDPOINT_GROUND) == PQC_OK);

  /* An existing log must never be overwritten. */
  struct logger other;
  CHECK(logger_init(&other, storage, 2, TEST_LOG, "run-7", "mlkem768",
                    LOG_ENDPOINT_GROUND) == PQC_ERR_IO);

  CHECK(logger_event(&lg, LOG_EV_PKT_TX, 42, 7) == PQC_OK);
  CHECK(logger_event(&lg, LOG_EV_PKT_DROP, 60, PQC_ERR_REPLAY) == PQC_OK);
  /* Buffer holds 2: the third is dropped and counted, not flushed. */
  CHECK(logger_event(&lg, LOG_EV_PKT_RX, 42, 8) == PQC_ERR_BUFFER_TOO_SMALL);
  CHECK(logger_event(&lg, LOG_EV_COUNT, 0, 0) == PQC_ERR_INVALID_ARG);

  CHECK(logger_flush(&lg) == PQC_OK);
  /* Buffer is empty again after a flush. */
  CHECK(logger_event(&lg, LOG_EV_REKEY_DONE, 0, 1) == PQC_OK);
  CHECK(logger_close(&lg) == PQC_OK);

  /* Closed: further use is rejected, not a crash. */
  CHECK(logger_event(&lg, LOG_EV_PKT_TX, 1, 1) == PQC_ERR_INVALID_ARG);
  CHECK(logger_close(&lg) == PQC_ERR_INVALID_ARG);

  FILE *fp = fopen(TEST_LOG, "r");
  CHECK(fp != NULL);
  if (fp == NULL) {
    return;
  }
  char line[256];
  CHECK(fgets(line, sizeof line, fp) != NULL);
  CHECK(strcmp(line,
               "run_id,mode,timestamp_ns,endpoint,event,bytes,"
               "detail\n") == 0);

  const char *events[] = {"pkt_tx", "pkt_drop", "log_dropped", "rekey_done"};
  const uint32_t bytes[] = {42, 60, 0, 0};
  const int64_t details[] = {7, PQC_ERR_REPLAY, 1, 1};
  uint64_t last_ts = 0;
  for (size_t i = 0; i < 4; i++) {
    struct row r = {0};
    CHECK(fgets(line, sizeof line, fp) != NULL);
    CHECK(parse_row(line, &r));
    CHECK(strcmp(r.run_id, "run-7") == 0);
    CHECK(strcmp(r.mode, "mlkem768") == 0);
    CHECK(strcmp(r.endpoint, "ground") == 0);
    CHECK(strcmp(r.event, events[i]) == 0);
    CHECK(r.bytes == bytes[i]);
    CHECK(r.detail == details[i]);
    CHECK(r.timestamp_ns >= last_ts);
    last_ts = r.timestamp_ns;
  }
  CHECK(fgets(line, sizeof line, fp) == NULL); /* nothing extra */
  fclose(fp);
  remove(TEST_LOG);
}

int main(void) {
  test_timer();
  test_logger_bad_config();
  test_logger_roundtrip();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}