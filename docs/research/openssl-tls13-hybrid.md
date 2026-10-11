# OpenSSL TLS 1.3 and the Hybrid Group

**Summary**: What OpenSSL 3.5.9 does for TLS 1.3 key exchange, how to pin the group for
configuration A (X25519) and B (`X25519MLKEM768`), which C calls we need, and what the
built-in tools can and cannot benchmark. Read from the 3.5.9 source tree and checked on a
development machine ([results.md](../results.md)); **not yet run on a Pi**.

**Sources**: [openssl-3-5-9](sources.md#openssl-3-5-9), [fips-203](sources.md#fips-203), [debian-openssl-tracker](sources.md#debian-openssl-tracker)

**Last updated**: 2026-10-11

---

## Version and build

- Latest 3.5 release at reading time is **3.5.9** (29 Sep 2026), a security patch release
  whose most severe fixed issue is rated High (`NEWS.md`). Pin 3.5.9 or later. Newer
  branches exist (3.6.x, 4.0.x); the advisor's scope says "3.5+", so stay on 3.5 unless
  told otherwise.
- ML-KEM, ML-DSA and SLH-DSA were added in 3.5, including the TLS hybrid groups
  `X25519MLKEM768`, `SecP256r1MLKEM768` and `SecP384r1MLKEM1024` (`CHANGES.md`).
- Raspberry Pi OS based on Debian 13 should already ship OpenSSL 3.5, so a source build may not be
  needed: see [certificates and time](certificates-and-time.md). Otherwise, install into its own prefix so the system OpenSSL is untouched:
  `./Configure --prefix=/opt/openssl-3.5.9 --openssldir=...` and link with an rpath
  (`INSTALL.md`). `no-ml-kem` would disable ML-KEM, and `no-docs` and `no-tests` should shorten
  the build. Build time on a Pi 4 is **not known**.
- The OpenSSL ML-KEM code is plain C ported from BoringSSL (`crypto/ml_kem/ml_kem.c`, about
  2,400 lines), with constant-time helpers and no assembly in that file. It is built into the
  default and FIPS providers. Treat Pi timings accordingly: there is no ARM-specific
  optimization.
- The tag's signature was not verified. Record the commit hash above, and check it against the
  release signature or hash on openssl.org before relying on it.

## Default groups change the experiment

The default TLS group list since 3.5 is
`?*X25519MLKEM768 / ?*X25519:?secp256r1 / ?X448:?secp384r1:?secp521r1 / ?ffdhe2048:?ffdhe3072`
(`CHANGES.md`, `ssl/t1_lib.c`). A default client therefore offers hybrid first and sends two
key shares (hybrid and X25519). **An unconfigured client already negotiates hybrid**, so
configuration A is only classical if the group is set explicitly.

The default list's first predicted key share makes the ClientHello larger, and the manual notes
that it "may no longer fit in a single TCP segment" (`doc/man3/SSL_CTX_set1_curves.pod`).

## Group-list syntax

From `SSL_CTX_set1_curves.pod`:

| Syntax | Meaning |
|--------|---------|
| `A:B` | Colon-separated groups in preference order; the first one gets the client's only key share |
| `*A` | Client sends a key share for `A` (up to 4); servers ignore `*` |
| `A/B` | Tuple separator. Servers choose within a tuple first and may send a HelloRetryRequest to reach a more preferred tuple; clients ignore tuple boundaries |
| `?A` | Ignore `A` if this build doesn't know it. Without `?`, an unknown name makes the list fail to parse |
| `-A` | Remove `A` from the list built so far |
| `DEFAULT` | The built-in default list |

Group names are case-insensitive from 3.5. List what a build supports with
`openssl list -tls1_3 -tls-groups`.

## Proposed settings for the two configurations *(proposed)*

| Setting | Configuration A | Configuration B |
|---------|-----------------|-----------------|
| Group list | `X25519` | `X25519MLKEM768` |
| Set on | client **and** server | client **and** server |
| `?` prefix | no | **no**, so a build without ML-KEM fails loudly instead of silently dropping the group |
| `DEFAULT` | never | never |
| TLS version | min = max = 1.3 | min = max = 1.3 |
| Tickets | `SSL_CTX_set_num_tickets(ctx, 0)` | same |
| Cipher suite | one suite, identical in both, set with `SSL_CTX_set_ciphersuites` | same |

For T04, the manual's server-side selection algorithm returns failure when the client's supported
groups and the server's list share nothing. On a development machine, a client offering only
`X25519MLKEM768` against a server listing only `X25519` failed with TLS alert 40
(`handshake_failure`), and so did the reverse; a client left on the default group list connected
with whatever the server offered ([results.md](../results.md)). Still to repeat on the Pis.

## C calls we need

All documented in the 3.5.9 manual pages:

| Need | Call | Page |
|------|------|------|
| Set groups from a string | `SSL_CTX_set1_groups_list` | `SSL_CTX_set1_curves.pod` |
| Negotiated group (NID) | `SSL_get_negotiated_group` | same |
| Group name for logs | `SSL_get0_group_name` | `SSL_get0_group_name.pod` |
| Groups a client offered (server side) | `SSL_get1_groups`, `SSL_get0_iana_groups` | `SSL_CTX_set1_curves.pod` |
| Pin the version | `SSL_CTX_set_min_proto_version`, `SSL_CTX_set_max_proto_version` | demo, `SSL_CTX_set_min_proto_version.pod` |
| Fix the TLS 1.3 suite | `SSL_CTX_set_ciphersuites` | `SSL_CTX_set_cipher_list.pod` |
| Verify peer | `SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL)` | `demos/guide/tls-client-block.c` |
| Check hostname | `SSL_set1_host`, then `SSL_get_verify_result` | same |
| No resumption | `SSL_CTX_set_num_tickets`, `SSL_OP_NO_TICKET` | `SSL_CTX_set_num_tickets.pod` |

The TLS 1.3 default suites are `TLS_AES_256_GCM_SHA384`, `TLS_CHACHA20_POLY1305_SHA256` and
`TLS_AES_128_GCM_SHA256`, in that order (`include/openssl/ssl.h.in`).

## Sizes

Measured on a development machine with ECDSA P-256 and tickets off ([results.md](../results.md)):

| Message | Config A (X25519) | Config B (`X25519MLKEM768`) |
|---------|-------------------|-----------------------------|
| ClientHello | 212 B | 1,388 B |
| ServerHello | 122 B | 1,210 B |

The ServerHello grows by exactly the ML-KEM-768 ciphertext (1,088 B, FIPS 203, see [ML-KEM](ml-kem.md)).
The ClientHello grows by 1,176 B, 8 B less than the 1,184 B encapsulation key; the reason is not yet
known. Key shares by arithmetic (estimate, X25519 is 32 B in `providers/implementations/keymgmt/mlx_kmgmt.c`):
client 1,184 + 32 = 1,216 B, server 1,088 + 32 = 1,120 B. The group's IANA ID is 4588 (0x11EC)
(`ssl/t1_trce.c`). The Pure ML-KEM groups `MLKEM512`, `MLKEM768` and `MLKEM1024` also appear in
`openssl list -tls1_3 -tls-groups`; they are a TLS handshake, which is **not** the optional Config C
primitive benchmark.

## Demos we can start from

`demos/guide/tls-client-block.c` and `tls-server-block.c` are complete C programs. The client
demo requires TLS 1.2 or later and uses the system trust store, so we change both. **Do not use
the keys in `demos/`** (`rootkey.pem`, `serverkey.pem`, `key.pem`): they are public.

## What the built-in tools can and cannot do

| Tool | Useful for | Limit |
|------|-----------|-------|
| `openssl s_server` / `s_client` | The guide's day-one check; `-groups` is accepted as an `SSL_CONF` option | Not a benchmark |
| `openssl s_time` | `-new` makes a fresh session per connection; `-time`, `-cafile`, `-ciphersuites` | **No group option**, so it cannot force A versus B |
| `openssl speed` | Algorithm speed | No ML-KEM entry found in `apps/speed.c` |

So the T10 handshake benchmark and the optional Config C measurement both need our own small C
programs. Config C uses the `EVP_PKEY` ML-KEM interface (`doc/man7/EVP_KEM-ML-KEM.pod`,
`EVP_PKEY-ML-KEM.pod`) and must not be reported as a TLS session.

## Not covered here

- Behaviour and timing on a real Pi 4 (build time if needed, handshake time, memory).
- Measurement methods (CPU, memory, bytes on the wire) beyond the byte counts above.

Covered elsewhere: [certificates and the Pi clock](certificates-and-time.md) and
[TCP under loss](tcp-under-loss.md).

## Related pages

- [ML-KEM](ml-kem.md)
- [Certificates and the Pi clock](certificates-and-time.md)
- [TCP under loss](tcp-under-loss.md)
- [Hybrid mode](decisions/hybrid-mode.md)
- [Handshake authentication](decisions/handshake-auth.md)
- [Project scope](../scope.md) and [telemetry format](../telemetry_format.md)
