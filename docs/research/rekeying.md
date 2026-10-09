# Rekeying

**Summary**: Every reason the standards give for replacing session keys mid-mission,
and what each implies for `rekey.c` and the rekey-time measurements.

**Sources**: [sp-800-232](sources.md#sp-800-232), [sp-800-227](sources.md#sp-800-227), [sp-800-57pt1](sources.md#sp-800-57pt1), [sp-800-133r3-ipd](sources.md#sp-800-133r3-ipd)

**Last updated**: 2026-10-08

---

## Rekey triggers

| Trigger | Basis | Strength |
|---------|-------|----------|
| Packet counter would overflow | Our nonce design: the counter never resets without a rekey (CONTRIBUTING.md crypto rule 4); a repeated nonce is forbidden (R3, [sp-800-232 §4.3](sources.md#sp-800-232)) | Required |
| 2^54 bytes processed under one key (including nonces, both directions of use) | R6/R7, [sp-800-232 §4.3](sources.md#sp-800-232) | Required (**shall**) |
| Decryption-failure limit reached: 2^96 for full 128-bit tags, 1 for tags under 64 bits | R5/R7, [sp-800-232 §4.3](sources.md#sp-800-232) | Recommended (**should**) |
| Cryptoperiod: about a day to a week for link-encryption keys | [sp-800-57pt1 Table 1](sources.md#sp-800-57pt1) (rough guidance) | Recommended; never reached in a mission |
| Scheduled interval | The experiment config (`rekey interval`) | Our choice |

NIST's cryptoperiod guidance is far longer than a mission, so **the rekey interval is a
free experimental variable**, chosen to limit exposure rather than to meet a requirement
(see [Key management](key-management.md#cryptoperiods)).

At drone telemetry rates, the first two triggers will never be reached in a mission
(**estimate**: 2^64 packets, or 2^54 bytes). The failure trigger only matters in
practice if tags are truncated. In experiments, rekeys will come from the configured
interval.

Whether to add a `decrypt_fail`-count trigger in `rekey.c` is an
[open question](open-questions.md).

## What a rekey must do

- **Run a fresh key exchange.** Ephemeral ML-KEM key pairs are single-use (RS6,
  [sp-800-227 §4.2](sources.md#sp-800-227)), so a rekey cannot reuse the first
  handshake's key pair. KeyGen, Encaps and Decaps all run again, and their cost belongs
  in the measured rekey time.
- **New keys, new epoch.** New per-direction keys are derived with the new epoch in the
  FixedInfo context (see [Key derivation](key-derivation.md)). The packet counter
  restarts only together with the new key.
- **Never derive new keys from old ones.** "Key update" (computing the new key from the
  old) is disallowed; replacement keys must be independent, from fresh key
  establishment ([sp-800-57pt1 §8.2.3](sources.md#sp-800-57pt1),
  [sp-800-133r3-ipd §5.5](sources.md#sp-800-133r3-ipd)). Epoch n+1 keys must not depend
  on epoch n keys. This should become an explicit project rule.
- **Authentication cost repeats.** With signatures, every rekey pays the signature bytes
  again, and ML-DSA's variable signing time widens the spread of rekey times. With a
  PSK it costs nothing extra (see [ML-DSA](ml-dsa.md#what-it-would-cost-our-handshake)).
- **Destroy old keys** once no packet under the old epoch can still be accepted
  (CONTRIBUTING.md crypto rule 5).

## Related pages

- [Ascon](ascon.md)
- [KEM usage](kem-usage.md)
- [Key derivation](key-derivation.md)
- [Key management](key-management.md)
