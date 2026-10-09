# ExpressVPN "Post-Quantum WireGuard" Guide

**Summary**: Source summary of a 2025 ExpressVPN white paper on adding post-quantum
protection to WireGuard. **Low-trust vendor source**: not peer-reviewed, no citations,
and it contains a factual error. Cite it only for what one vendor says it deployed.

**Sources**: [expressvpn-pq-wireguard-guide](sources.md#expressvpn-pq-wireguard-guide)

**Last updated**: 2026-10-08

---

> **Trust level: low.** A vendor white paper with a marketing tone. It has no
> references, no venue or DOI, and no method behind its performance numbers. Never cite
> it for security reasoning or protocol details. It is **not** the academic
> "Post-Quantum WireGuard" paper (Hülsing et al., IEEE S&P 2021), and doesn't mention
> it.

## What it is

- **Authors:** Peter Membrey and Timo Beyel, dated 5 August 2025, writing as ExpressVPN.
  It also promotes the company's own Lightway protocol
  ([expressvpn-pq-wireguard-guide §1, §3.1](sources.md#expressvpn-pq-wireguard-guide)).
- **The design:** the client registers a random 256-bit pre-shared key over a hybrid
  ML-KEM + X25519 TLS 1.3 connection, and that PSK is then used in *unmodified*
  WireGuard ([expressvpn-pq-wireguard-guide §1, §2.3, §5.1](sources.md#expressvpn-pq-wireguard-guide)).
- **What it doesn't do:** it doesn't change the WireGuard handshake, put a KEM inside
  it, or post-quantum-authenticate it. Authentication stays with WireGuard's classical
  Curve25519 static keys.

## Problems

- **A security claim is wrong.** It calls a 256-bit random PSK "information-theoretic"
  security, which it isn't; the security is computational
  ([expressvpn-pq-wireguard-guide §6.2](sources.md#expressvpn-pq-wireguard-guide)).
- **Unsourced claims:** qubit counts, competitors' designs, and performance (13–15 ms of
  added TLS time, with no hardware or method)
  ([expressvpn-pq-wireguard-guide §2.1, §3.1, §7](sources.md#expressvpn-pq-wireguard-guide)).
- **Missing topics:** rekey timers, nonces and the replay window are not covered.

## What it can support

- That a large VPN vendor deploys "PSK delivered over a PQ channel" as its post-quantum
  measure.
- Generic good practice consistent with our rules:
  - fail closed on any parse or validation error, and never degrade
  - validate keys and PSK format on input
  - generate the PSK from a CSPRNG

  ([expressvpn-pq-wireguard-guide §5.2, §5.4](sources.md#expressvpn-pq-wireguard-guide))

For WireGuard's actual timers, replay window and KEM-based handshake, ingest the
WireGuard whitepaper (Donenfeld) and Hülsing et al. 2021 instead.

## Extraction notes

Extracted with docling; clean.

## Related pages

- [Handshake authentication](decisions/handshake-auth.md)
