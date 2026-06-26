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
accent, model-specific filter/drive behavior, tempo-aware wobble modulation,
and bounded output. The wobble path is split into small plugin-local modules:
`basilico_modulation` owns free/tempo phase and shape generation, while
`basilico_flanger` owns the stereo phase/flange target.

The `Wobble` controls can run free or follow host tempo divisions. The shared
wobble signal can start at a 0-360 degree cycle offset and can drive amplitude,
filter cutoff pitch, and stereo phase/flange motion. `Squelch` is an acid-style
macro over filter envelope, resonance, and drive. The DPF layer is
intentionally thin and exposes one MIDI input with stereo audio output.
