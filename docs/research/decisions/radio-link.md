# Radio Link

**Summary**: Which link the drone and ground station use for experiments: a SiK
915 MHz telemetry radio, LoRa, or UDP only (with netem to simulate loss and delay).

**Sources**: *(none yet)*

**Last updated**: 2026-10-08

**Status**: Open

**Decision**: —

---

## Why it matters

The radio sets the link MTU and data rate. Those decide how many fragments an ML-KEM
public key and ciphertext need, and how long a handshake takes on air. This is the core
of the research question. Fragmentation (`fragment.c`) is due in week 2, so this
decision affects the week 2 design.

## Options

| Option | What it gives us | Open questions |
|--------|------------------|----------------|
| SiK 915 MHz | A real drone telemetry radio | Max payload per packet? Air data rate settings? Duty-cycle or legal limits? *(needs source)* |
| LoRa | Long range, very low data rate | Max payload at each spreading factor? Time on air? Duty-cycle limits? *(needs source)* |
| UDP only | Fast iteration; loss and delay controlled exactly by netem | Which real radio's MTU and rate should netem emulate, so results stay realistic? |

The options aren't exclusive: UDP + netem is already the plan for development (see the
repo README), so the real question is which radio, if any, to validate against.

## Evidence

*(none yet: ingest radio datasheets and MAVLink docs, then fill this in)*

## Recommendation

*(pending evidence)*

## Related pages

- [Research wiki index](../index.md)
