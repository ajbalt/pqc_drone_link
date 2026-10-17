/**
 * @file test_tls_link.c
 * @brief Tests for tls_link.c: configurations A and B, T02, T03, T04, T09.
 *
 * Every scenario forks a server process and connects to it over a socketpair,
 * so no network port is needed. Usage: test_tls_link <cert-dir>
 * <other-cert-dir> where both directories come from scripts/make_demo_certs.sh
 * (the second one is a different CA, used for the "wrong CA" case).
 */

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common/errors.h"
#include "proto/wire.h"
#include "tls/tls_link.h"

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

/** @brief Directory with the CA and server certificate under test. */
static const char *cert_dir;
/** @brief Directory with a different CA (for the wrong-CA case). */
static const char *other_dir;

/** @brief What the forked server does after the handshake. */
enum serve_mode {
  SERVE_ECHO,   /**< read one RECORD, ACK it */
  SERVE_TAMPER, /**< expect the next read to fail (corrupted TLS record) */
  SERVE_SILENT, /**< say nothing for a while (client times out) */
};

/** @brief Server exit codes the parent can tell apart. */
enum {
  EXIT_SERVED = 0,        /**< handshake and the requested behaviour worked */
  EXIT_NO_CTX = 10,       /**< could not build the server context */
  EXIT_NO_HANDSHAKE = 11, /**< handshake failed (expected in failure cases) */
  EXIT_BAD_BEHAVIOUR = 12 /**< handshake worked but the exchange did not */
};

/** @brief One client/server pairing to run. */
struct scenario {
  enum tls_group client_group; /**< group the client pins */
  enum tls_group server_group; /**< group the server pins */
  const char *ca_dir;          /**< directory whose ca.crt the client trusts */
  const char *hostname; /**< name the client expects in the certificate */
  enum serve_mode mode; /**< what happens after the handshake */
};

/** @brief What a scenario produced. */
struct result {
  int client_rc;   /**< PQC_OK or the first client error */
  int server_exit; /**< server process exit code */
  char group[32];  /**< group the client saw (empty if no handshake) */
  char cipher[48]; /**< cipher suite the client saw */
  uint64_t rx;     /**< bytes the client received before any record */
  uint64_t tx;     /**< bytes the client sent before any record */
};

/** @brief Build "<dir>/<name>" into @p out; return 0 on success. */
static int join(char *out, size_t n, const char *dir, const char *name) {
  int w = snprintf(out, n, "%s/%s", dir, name);
  return (w < 0 || (size_t)w >= n) ? -1 : 0;
}

/** @brief Server side of a scenario. Runs in the child; never returns. */
static void run_server(int fd, const struct scenario *sc) {
  char cert[512], key[512];
  if (join(cert, sizeof(cert), cert_dir, "srv.crt") != 0 ||
      join(key, sizeof(key), cert_dir, "srv.key") != 0) {
    _exit(EXIT_NO_CTX);
  }
  struct tls_config cfg = {
      .group = sc->server_group, .cert_file = cert, .key_file = key};
  struct tls_ctx *ctx = NULL;
  struct tls_conn *conn = NULL;
  if (tls_ctx_new_server(&cfg, &ctx) != PQC_OK) {
    _exit(EXIT_NO_CTX);
  }
  if (tls_accept(ctx, fd, &conn) != PQC_OK) {
    _exit(EXIT_NO_HANDSHAKE);
  }

  int code = EXIT_BAD_BEHAVIOUR;
  uint8_t hdr[WIRE_HEADER_LEN];
  if (sc->mode == SERVE_ECHO) {
    uint8_t type = 0;
    uint16_t len = 0;
    uint8_t payload[WIRE_MAX_PAYLOAD];
    struct wire_record rec;
    uint8_t ack_msg[WIRE_ACK_MSG_LEN];
    size_t ack_len = 0;
    if (tls_read_exact(conn, hdr, sizeof(hdr)) == PQC_OK &&
        wire_parse_header(hdr, &type, &len) == PQC_OK &&
        type == WIRE_TYPE_RECORD &&
        tls_read_exact(conn, payload, len) == PQC_OK &&
        wire_decode_record(payload, len, &rec) == PQC_OK && rec.seq == 1) {
      /* The durable commit belongs here (rule 15); Phase 1 has no store yet. */
      struct wire_ack ack = {.node = rec.node, .seq = rec.seq};
      if (wire_encode_ack(&ack, ack_msg, sizeof(ack_msg), &ack_len) == PQC_OK &&
          tls_write_all(conn, ack_msg, ack_len) == PQC_OK) {
        code = EXIT_SERVED;
      }
    }
  } else if (sc->mode == SERVE_TAMPER) {
    /* A record that fails its integrity check is an I/O error, not "closed". */
    if (tls_read_exact(conn, hdr, sizeof(hdr)) == PQC_ERR_IO) {
      code = EXIT_SERVED;
    }
  } else {
    sleep(1);
    code = EXIT_SERVED;
  }
  tls_conn_free(conn);
  tls_ctx_free(ctx);
  _exit(code);
}

/** @brief Client side: send one RECORD and check the ACK. Returns PQC_OK or 20.
 */
static int client_echo(struct tls_conn *conn) {
  struct wire_record rec = {.node = 1, .seq = 1, .ts_ms = 1};
  uint8_t msg[WIRE_RECORD_MSG_LEN];
  size_t len = 0;
  uint8_t hdr[WIRE_HEADER_LEN];
  uint8_t payload[WIRE_MAX_PAYLOAD];
  uint8_t type = 0;
  uint16_t plen = 0;
  struct wire_ack ack;

  if (wire_encode_record(&rec, msg, sizeof(msg), &len) != PQC_OK) {
    return 20;
  }
  int rc = tls_write_all(conn, msg, len);
  if (rc != PQC_OK) {
    return rc;
  }
  rc = tls_read_exact(conn, hdr, sizeof(hdr));
  if (rc != PQC_OK) {
    return rc;
  }
  if (wire_parse_header(hdr, &type, &plen) != PQC_OK || type != WIRE_TYPE_ACK) {
    return 20;
  }
  rc = tls_read_exact(conn, payload, plen);
  if (rc != PQC_OK) {
    return rc;
  }
  if (wire_decode_ack(payload, plen, &ack) != PQC_OK || ack.seq != 1 ||
      ack.node != 1) {
    return 20;
  }
  return PQC_OK;
}

/** @brief Run one scenario: fork the server, run the client, collect both. */
static struct result run_scenario(const struct scenario *sc) {
  struct result res;
  memset(&res, 0, sizeof(res));
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0) {
    res.client_rc = PQC_ERR_IO;
    return res;
  }
  fflush(NULL);
  pid_t pid = fork();
  if (pid < 0) {
    res.client_rc = PQC_ERR_IO;
    return res;
  }
  if (pid == 0) {
    close(sv[0]);
    run_server(sv[1], sc);
  }
  close(sv[1]);

  char ca[512];
  if (join(ca, sizeof(ca), sc->ca_dir, "ca.crt") != 0) {
    res.client_rc = PQC_ERR_BAD_CONFIG;
  } else {
    struct tls_config cfg = {
        .group = sc->client_group, .ca_file = ca, .hostname = sc->hostname};
    struct tls_ctx *ctx = NULL;
    struct tls_conn *conn = NULL;
    res.client_rc = tls_ctx_new_client(&cfg, &ctx);
    if (res.client_rc == PQC_OK) {
      res.client_rc = tls_connect(ctx, sv[0], &conn);
    }
    if (res.client_rc == PQC_OK) {
      snprintf(res.group, sizeof(res.group), "%s", tls_conn_group(conn));
      snprintf(res.cipher, sizeof(res.cipher), "%s", tls_conn_cipher(conn));
      /* Read straight after the handshake: this is the handshake overhead. */
      if (tls_conn_bytes(conn, &res.rx, &res.tx) != PQC_OK) {
        res.rx = res.tx = 0;
      }
      if (sc->mode == SERVE_ECHO) {
        res.client_rc = client_echo(conn);
      } else if (sc->mode == SERVE_TAMPER) {
        /* Bypass TLS and push bytes that look like an application-data record
         * but cannot pass the AEAD check. */
        uint8_t junk[64] = {0x17, 0x03, 0x03, 0x00, 59};
        for (size_t i = 5; i < sizeof(junk); i++) {
          junk[i] = (uint8_t)(i * 7);
        }
        if (write(sv[0], junk, sizeof(junk)) != (ssize_t)sizeof(junk)) {
          res.client_rc = PQC_ERR_IO;
        }
      } else {
        struct timeval tv = {.tv_sec = 0, .tv_usec = 200000};
        uint8_t b;
        setsockopt(sv[0], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        res.client_rc = tls_read_exact(conn, &b, 1);
      }
    }
    tls_conn_free(conn);
    tls_ctx_free(ctx);
  }
  close(sv[0]);

  int status = 0;
  res.server_exit = (waitpid(pid, &status, 0) == pid && WIFEXITED(status))
                        ? WEXITSTATUS(status)
                        : 99;
  return res;
}

/** @brief Configurations A and B work, and report the group (T02). */
static void test_configurations(void) {
  struct scenario a = {TLS_GROUP_X25519, TLS_GROUP_X25519, NULL, "relay.test",
                       SERVE_ECHO};
  a.ca_dir = cert_dir;
  struct result r = run_scenario(&a);
  CHECK(r.client_rc == PQC_OK);
  CHECK(r.server_exit == EXIT_SERVED);
  CHECK(strcasecmp(r.group, "X25519") == 0); /* OpenSSL says "x25519" */
  CHECK(strcmp(r.cipher, TLS_CIPHER_SUITE) == 0);
  uint64_t a_rx = r.rx, a_tx = r.tx;
  CHECK(a_rx > 0 && a_tx > 0);

  struct scenario b = a;
  b.client_group = TLS_GROUP_HYBRID;
  b.server_group = TLS_GROUP_HYBRID;
  r = run_scenario(&b);
  CHECK(r.client_rc == PQC_OK);
  CHECK(r.server_exit == EXIT_SERVED);
  CHECK(strcmp(r.group, "X25519MLKEM768") == 0);
  CHECK(strcmp(r.cipher, TLS_CIPHER_SUITE) == 0);
  /* The hybrid handshake carries an ML-KEM-768 public key (1184 B) one way and
   * a ciphertext (1088 B) the other, so it must be well over 1000 B larger in
   * each direction than the classical one. */
  CHECK(r.tx > a_tx + 1000);
  CHECK(r.rx > a_rx + 1000);
}

/** @brief A group mismatch fails on both sides, in both directions (T04). */
static void test_no_downgrade(void) {
  struct scenario s = {TLS_GROUP_HYBRID, TLS_GROUP_X25519, NULL, "relay.test",
                       SERVE_ECHO};
  s.ca_dir = cert_dir;
  struct result r = run_scenario(&s);
  CHECK(r.client_rc == PQC_ERR_TLS_HANDSHAKE);
  CHECK(r.server_exit == EXIT_NO_HANDSHAKE);
  CHECK(r.group[0] == '\0');

  s.client_group = TLS_GROUP_X25519;
  s.server_group = TLS_GROUP_HYBRID;
  r = run_scenario(&s);
  CHECK(r.client_rc == PQC_ERR_TLS_HANDSHAKE);
  CHECK(r.server_exit == EXIT_NO_HANDSHAKE);
}

/** @brief A wrong hostname or an untrusted CA fails closed (T03). */
static void test_identity(void) {
  struct scenario s = {TLS_GROUP_HYBRID, TLS_GROUP_HYBRID, NULL, "wrong.test",
                       SERVE_ECHO};
  s.ca_dir = cert_dir;
  struct result r = run_scenario(&s);
  CHECK(r.client_rc == PQC_ERR_HS_AUTH);
  CHECK(r.server_exit == EXIT_NO_HANDSHAKE);

  s.hostname = "relay.test";
  s.ca_dir = other_dir; /* a CA that did not sign the server certificate */
  r = run_scenario(&s);
  CHECK(r.client_rc == PQC_ERR_HS_AUTH);
  CHECK(r.server_exit == EXIT_NO_HANDSHAKE);
}

/** @brief Corrupted ciphertext is rejected, not delivered (T09). */
static void test_tamper(void) {
  struct scenario s = {TLS_GROUP_HYBRID, TLS_GROUP_HYBRID, NULL, "relay.test",
                       SERVE_TAMPER};
  s.ca_dir = cert_dir;
  struct result r = run_scenario(&s);
  CHECK(r.client_rc == PQC_OK);
  CHECK(r.server_exit == EXIT_SERVED);
}

/** @brief A silent peer produces PQC_ERR_TIMEOUT, not a hang. */
static void test_timeout(void) {
  struct scenario s = {TLS_GROUP_X25519, TLS_GROUP_X25519, NULL, "relay.test",
                       SERVE_SILENT};
  s.ca_dir = cert_dir;
  struct result r = run_scenario(&s);
  CHECK(r.client_rc == PQC_ERR_TIMEOUT);
}

/** @brief Bad arguments and bad files are rejected without a crash. */
static void test_bad_input(void) {
  char ca[512], cert[512], key[512];
  CHECK(join(ca, sizeof(ca), cert_dir, "ca.crt") == 0);
  CHECK(join(cert, sizeof(cert), cert_dir, "srv.crt") == 0);
  CHECK(join(key, sizeof(key), cert_dir, "srv.key") == 0);

  CHECK(tls_group_name(TLS_GROUP_X25519) != NULL);
  CHECK(tls_group_name((enum tls_group)99) == NULL);

  struct tls_ctx *ctx = (struct tls_ctx *)1;
  struct tls_config good = {.group = TLS_GROUP_X25519,
                            .ca_file = ca,
                            .hostname = "relay.test",
                            .cert_file = cert,
                            .key_file = key};
  CHECK(tls_ctx_new_client(NULL, &ctx) == PQC_ERR_INVALID_ARG);
  CHECK(ctx == NULL);
  CHECK(tls_ctx_new_client(&good, NULL) == PQC_ERR_INVALID_ARG);
  CHECK(tls_ctx_new_server(NULL, &ctx) == PQC_ERR_INVALID_ARG);

  struct tls_config bad = good;
  bad.group = (enum tls_group)99;
  CHECK(tls_ctx_new_client(&bad, &ctx) == PQC_ERR_INVALID_ARG);
  bad = good;
  bad.hostname = "";
  CHECK(tls_ctx_new_client(&bad, &ctx) == PQC_ERR_INVALID_ARG);
  bad.hostname = NULL;
  CHECK(tls_ctx_new_client(&bad, &ctx) == PQC_ERR_INVALID_ARG);
  char longname[400];
  memset(longname, 'a', sizeof(longname) - 1);
  longname[sizeof(longname) - 1] = '\0';
  bad = good;
  bad.hostname = longname;
  CHECK(tls_ctx_new_client(&bad, &ctx) == PQC_ERR_INVALID_ARG);
  bad = good;
  bad.ca_file = "/nonexistent/ca.crt";
  CHECK(tls_ctx_new_client(&bad, &ctx) == PQC_ERR_BAD_CONFIG);
  CHECK(ctx == NULL);
  bad = good;
  bad.key_file = "/nonexistent/srv.key";
  CHECK(tls_ctx_new_server(&bad, &ctx) == PQC_ERR_BAD_CONFIG);

  /* A server context must not be usable as a client, and the reverse. */
  struct tls_ctx *srv = NULL;
  struct tls_ctx *cli = NULL;
  struct tls_conn *conn = (struct tls_conn *)1;
  CHECK(tls_ctx_new_server(&good, &srv) == PQC_OK);
  CHECK(tls_ctx_new_client(&good, &cli) == PQC_OK);
  CHECK(tls_connect(srv, 5, &conn) == PQC_ERR_INVALID_ARG);
  CHECK(conn == NULL);
  CHECK(tls_accept(cli, 5, &conn) == PQC_ERR_INVALID_ARG);
  CHECK(tls_connect(NULL, 5, &conn) == PQC_ERR_INVALID_ARG);
  CHECK(tls_connect(cli, -1, &conn) == PQC_ERR_INVALID_ARG);
  CHECK(tls_connect(cli, 5, NULL) == PQC_ERR_INVALID_ARG);

  uint8_t b[4];
  CHECK(tls_write_all(NULL, b, sizeof(b)) == PQC_ERR_INVALID_ARG);
  CHECK(tls_read_exact(NULL, b, sizeof(b)) == PQC_ERR_INVALID_ARG);
  uint64_t n = 0;
  CHECK(tls_conn_bytes(NULL, &n, &n) == PQC_ERR_INVALID_ARG);
  CHECK(tls_conn_group(NULL) == NULL);
  CHECK(tls_conn_cipher(NULL) == NULL);
  tls_conn_free(NULL);
  tls_ctx_free(NULL);
  tls_ctx_free(srv);
  tls_ctx_free(cli);

  char msg[128];
  tls_last_error(msg, sizeof(msg));
  CHECK(msg[0] != '\0');
  tls_last_error(NULL, 0);
}

/** @brief Run every test and report. */
int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <cert-dir> <other-cert-dir>\n", argv[0]);
    return 2;
  }
  cert_dir = argv[1];
  other_dir = argv[2];
  signal(SIGPIPE, SIG_IGN);

  test_bad_input();
  test_configurations();
  test_no_downgrade();
  test_identity();
  test_tamper();
  test_timeout();

  if (failures != 0) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return 1;
  }
  printf("all checks passed\n");
  return 0;
}
