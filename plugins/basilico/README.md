# Basilico

Basilico is a monophonic bass instrument designed to pair with Downspout's MIDI
generators, especially `bassgen` and `ground`.

It provides five bass models over one stable engine:

- `Upright` for rounded acoustic-style jazz bass;
- `Electric` for clean, playable electric bass;
- `Dub` for muted reggae/dub weight;
- `Acid` for resonant synth bass with accent and glide;
- `Industrial` for harder driven bass.

## Build

Basilico is enabled by default with `DOWNSPOUT_BUILD_BASILICO=ON` and builds as
`basilico.vst3` when DPF is available.

```bash
cmake -S ../.. -B ../../build -DDOWNSPOUT_BUILD_BASILICO=ON
cmake --build ../../build --target basilico-vst3 downspout_basilico_core_tests
ctest --test-dir ../../build --output-on-failure -R basilico
```

## Implementation

The portable core handles MIDI note priority, note-off release, glide, velocity
accent, model-specific filter/drive behavior, sine-LFO filter cutoff
modulation, and bounded output. The DPF layer is intentionally thin and exposes
one MIDI input with stereo audio output.
