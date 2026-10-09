# MAVLink

**Summary**: The standard drone telemetry and command protocol whose traffic our link
carries: what MAVLink 2's own message signing does (and doesn't do), and what is still
needed to model realistic telemetry.

**Sources**: [mavlink2-guide](sources.md#mavlink2-guide)

**Last updated**: 2026-10-08

---

## What MAVLink 2 adds

MAVLink 2 extends MAVLink 1 with 24-bit message IDs (more than 256 message types),
optional extension fields on existing messages, and **message signing**
([mavlink2-guide](sources.md#mavlink2-guide)). Generated C headers can still send and
receive MAVLink 1. Extension fields are dropped when a message is sent as MAVLink 1.

## Message signing

Its existing security mechanism ([mavlink2-guide](sources.md#mavlink2-guide)):

- **Key:** a 32-byte secret, set up by the ground station with a `SETUP_SIGNING`
  message. The guide suggests a SHA-256 hash of a passphrase. In effect it is a
  **pre-shared key**, with no key exchange at all.
- **Anti-replay timestamp:** 64 bits, in units of 10 µs since 1 January 2015. It
  increments with every message sent, is saved regularly to persistent storage, and on
  start-up is taken as the larger of the clock and the stored value.
- **Replay check:** per (link ID, system, component), an incoming timestamp must be
  greater than the last one seen. A new stream is accepted if it is at most one minute
  behind.
- **Link ID:** stops a packet recorded on one channel from being replayed on another.
- **Unsigned packets:** an application callback may accept them, for example
  `RADIO_STATUS` from radios that don't sign (see
  [SiK radios](sik-radios.md#settings-that-distort-measurements)).

**What it does not give:** confidentiality (signing only authenticates), forward
secrecy, or any post-quantum key establishment. The guide does not name the signature
algorithm or its size *(needs source: MAVLink signing specification)*.

**Relevance to us:**
- MAVLink signing is the baseline security that drones already have, so it is useful
  context for the write-up.
- Its timestamp and link-ID scheme is a reference design for our replay window
  (`session.c`). Ours uses sequence numbers within a key epoch instead of a persistent
  clock, which avoids storing state across reboots.

## Still needed for telemetry modelling

The guide covers the protocol's mechanics, not the traffic. For `telemetry_gen.c` and
the "telemetry rates and sizes" open decision, we still need:
- message sizes, including MAVLink 2 header and signature overhead
- typical stream rates, such as HEARTBEAT, ATTITUDE and GLOBAL_POSITION_INT

Both are *(needs source: the MAVLink common message set and mavlink.io)*.

## Related pages

- [SiK radios](sik-radios.md)
- [MAVLink 2 guide summary](src-mavlink2-guide.md)
