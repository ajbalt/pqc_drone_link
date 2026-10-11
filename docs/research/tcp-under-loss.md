# TCP Under Loss

**Summary**: What TCP does when packets are lost, which timers matter for our TLS handshake and for
detecting a dead Wi-Fi link, and how `tc netem` emulates loss. It sets expectations for tests T06,
T08 and T10 before we run them.

**Sources**: [rfc-6298](sources.md#rfc-6298), [linux-ip-sysctl](sources.md#linux-ip-sysctl), [tc-netem-man](sources.md#tc-netem-man), [openssl-3-5-9](sources.md#openssl-3-5-9)

**Last updated**: 2026-10-11

---

## Why it matters

Our link is TCP, so the application never sees a lost packet; it sees **delay**. At 10% and 30%
emulated loss, a handshake finishes late rather than failing, and the delay is set by TCP timers, not
by cryptography. The hybrid handshake is bigger and so is exposed to a little more loss, but the
dominant effect is the same for A and B.

## Timers (spec and kernel values)

| Item | Value | Type and source |
|------|-------|-----------------|
| Initial retransmission timeout (RTO) | 1 s ("SHOULD") | spec, [rfc-6298 §2.1](sources.md#rfc-6298) |
| Backoff | RTO doubles on each timer expiry ("MUST") | spec, [rfc-6298 §5.5](sources.md#rfc-6298) |
| Maximum RTO | may be capped, but the cap must be at least 60 s | spec, [rfc-6298 §2.5](sources.md#rfc-6298) |
| After a lost SYN | RTO is set back to 3 s when data starts | spec, [rfc-6298 §5.7](sources.md#rfc-6298) |
| `tcp_syn_retries` | default 6; documented as 67 s to the last retransmission with `tcp_syn_linear_timeouts` = 4, final timeout after 131 s | reported, [linux-ip-sysctl](sources.md#linux-ip-sysctl) (latest kernel docs; the Pi's 6.12 kernel may differ) |
| `tcp_retries2` | default 15; "a hypothetical timeout of 924.6 seconds" as a lower bound | reported, same |
| Keepalive | `tcp_keepalive_time` 2 h, `tcp_keepalive_intvl` 75 s, `tcp_keepalive_probes` 9 | reported, same |

Consequences:

- **A lost SYN costs at least 1 s** of waiting before the retry, far longer than a handshake on an
  unloaded LAN.
- **A dead connection is not noticed for about 15 minutes** by default (`tcp_retries2`). After a
  60 s Wi-Fi outage Pi A would still think the socket is fine. Our application-level ACK timeout
  (proposed 2 s) must make Pi A close the socket and reconnect instead of waiting for TCP; keepalive
  or a user timeout on the socket helps. *(The Linux `TCP_USER_TIMEOUT` socket option is not
  sourced here; look it up in `tcp(7)` before relying on it.)*

## Expected effect on a handshake *(estimate)*

Arithmetic with stated assumptions: each packet is lost independently with probability *p*; only the
packets of the exchange below count; ACK-only packets and retransmissions are ignored.

| | Config A | Config B |
|---|---------|----------|
| Packets in the exchange | SYN, SYN-ACK, ClientHello, server flight (1 segment), client Finished = 5 | same, but the server flight is 2 segments (about 2,040 B against a 1,448 B segment, [results.md](../results.md)) = 6 |
| Probability none is lost at *p* = 10% | 0.9^5 = 59% | 0.9^6 = 53% |
| Probability none is lost at *p* = 30% | 0.7^5 = 17% | 0.7^6 = 12% |

At 30% loss, roughly 8 or 9 handshakes in 10 need at least one retransmission, each costing at least
an RTO. So handshake latency at 30% loss is expected to be **seconds**, and the difference between A and B
a small share of it. Assumptions to revisit: segment size (1,448 B assumes a 1,500 B MTU and TCP
timestamps), whether loss applies in both directions, and the real RTO after the first RTT sample (the
Linux minimum RTO is not sourced here).

This also changes how we report T10: at 30% loss the 100-successful-handshake rule needs many
attempts, and failures (timeouts) must be counted separately, as the advisor required.

## ClientHello size

The hybrid ClientHello measured 1,388 B ([results.md](../results.md)). Add a 5 B TLS record header and
the whole message is 1,393 B, under a 1,448 B segment, so it should fit in one packet on a 1,500 B MTU. The
OpenSSL manual warns it "may no longer fit in a single TCP segment"
([openssl-3-5-9](sources.md#openssl-3-5-9)); a longer certificate chain or extra extensions on the server
side do not change the ClientHello, but a client that sends two key shares (the OpenSSL default) would be
larger.

## Emulating loss with `tc netem`

From the manual ([tc-netem-man](sources.md#tc-netem-man)):

| Model | Meaning |
|-------|---------|
| `loss random PERCENT` | "Each packet loss is independent" |
| `loss gemodel` | Gilbert-Elliott burst model with four parameters: probability of entering the bad state, probability of leaving it, loss probability in the bad state, loss probability in the good state |

- Real Wi-Fi loss is bursty, so independent loss is the simple case. State which model each run uses
  and record the parameters.
- The manual's examples apply netem to packets "going out on" a device, so loss in both directions
  probably needs netem on **both** Pis' Wi-Fi interfaces. *(Not confirmed from the manual; test it.)*
- `tc` is installed on our dev machine (`/usr/sbin/tc`); netem needs root and the `sch_netem` kernel
  module, and whether the Pi OS kernel includes it has to be checked.
- Open: netem on the Pis or loss shaped at the AP (advisor question Q10).

## Related pages

- [OpenSSL TLS 1.3 and the hybrid group](openssl-tls13-hybrid.md)
- [Certificates and the Pi clock](certificates-and-time.md)
- [Telemetry format](../telemetry_format.md)
