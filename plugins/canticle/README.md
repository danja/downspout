# Canticle

Canticle is a small polyphonic tonal instrument for the middle of a Downspout
ensemble. It is intended for `melgen`, `counterpointer`, and especially
`cadence` when a part needs clear keys, reed, pad, pluck, or glass tones rather
than bass weight, percussion, glitch, or Floozy's complex hybrid synthesis.

## Build

Canticle is enabled by default with `DOWNSPOUT_BUILD_CANTICLE=ON` and builds as
`canticle.vst3` when DPF is available.

```bash
cmake -S ../.. -B ../../build -DDOWNSPOUT_BUILD_CANTICLE=ON
cmake --build ../../build --target canticle-vst3 downspout_canticle_core_tests
ctest --test-dir ../../build --output-on-failure -R canticle
```

## Implementation

The portable core handles MIDI note allocation, 12-voice polyphony, release
tails, voice stealing, model-specific tone profiles, stereo spread, bounded
output, and simple host parameter persistence through the DPF wrapper.
