# TODO

## Transmission headless findings (2026-07-30)

- [x] Fix Ambo feedback-network DC accumulation. In Transmission's
  `patches/crystal-healing-vibratones.ttl` at 48 kHz/1024 frames, Orchid still
  produced AC RMS 0.0153 at second 14, while Ambo had decayed to AC RMS 0.0021
  and accumulated a DC offset of 0.2066. Add DC blocking in the relevant
  feedback/delay/shimmer paths, ensure feedback state remains finite and
  bounded, and add a deterministic 30-second regression asserting low output
  mean and preserved AC energy.

- [x] Investigate Canticle worst-case processing spikes with multiple
  instances. The same deterministic graph measured maximum block times of
  27.56 ms for the chord instance and 18.93 ms for the motion instance at a
  21.33 ms host deadline. Add a repeatable processing benchmark, identify
  voice/model-dependent spikes, and optimize without adding allocation or
  locking to the processing path.

- [x] Make ArpGen Scale mode continuously schedule notes while transport is
  playing. With Lydian seventh shape, up/down order, and no required input, the
  headless run emitted no MIDI during seconds 11-14 before resuming at second
  15. Add deterministic tests across bar and capture-slice boundaries and
  verify that Scale mode does not depend on captured chord material.

Reproduce from the Transmission repository with:

```sh
npm run probe:project -- patches/crystal-healing-vibratones.ttl \
  --seconds 30 --block-size 1024 --sample-rate 48000
```
