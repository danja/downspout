# Markov

`markov.vst3` is a Markov-chain melody generator (MIDI in and out, transport-synced).

The melody is a random walk over the twelve pitch classes, counted from the Root note.
A 12 x 12 transition matrix gives, for the note just played, how likely each note is to
follow. The matrix is drawn on screen: brightness is each move's real chance (after the
scale, anything learned and Chaos), the digit is the weight you set (0-8), and clicking a
cell changes it. Eight styles fill the matrix; the plugin can also learn it from a melodic
line on its MIDI input.

Full description, controls and the learning workflow: `docs/pages/_products/markov.md`
(published as the Markov page). Reference entry: `docs/pages/plugins-summary.md`.

## Shape

- `include/markov_core.hpp`, `src/markov_core.cpp`: the portable core. The model (base matrix
  plus learned first- and second-order counts), styles, the effective matrix, the walk, playback,
  MIDI handling (learning and the Conductor CCs) and the state text format. No framework code.
- `src/dpf/MarkovPlugin.cpp`: thin DPF wrapper. Settings are parameters; the model is the
  `model` state, and the editor sends matrix edits through a separate `edit` state so it cannot
  overwrite what is being learned.
- `src/dpf/MarkovUI.cpp`, `src/dpf/MagnetoKit.hpp`: the editor, in the Magneto look. The kit is a
  plugin-local copy shared in content with retune and sprout (moving it to a shared header needs
  approval).
- `tests/markov_core_tests.cpp`: deterministic tests: scales against `docs/scales.md`, the walk and
  its statistics, order 2, learning, locate and block-size independence, the Conductor CCs and
  restart, and the state format.

## Decisions worth knowing

- **Determinism.** Each phrase restarts the walk from the tonic, seeded by (Seed, a re-roll
  counter, phrase number), so note `k` of phrase `p` depends only on those and the model. The
  walk costs O(k) per step; phrases are at most 8 bars.
- **Chaos** is the exponent `2^(1 - 2 chaos)` on the weights, so 50% is the identity.
- **Scale mask before sampling.** Out-of-scale notes get weight 0; a row left empty falls back to
  every allowed note.
- **Order 2** uses learned second-order counts for the (two back, one back) pair when they hold at
  least two observations, and the order-1 row otherwise. With nothing learned, Order 2 equals Order 1.
- **Clear learned** is a counter parameter (any change clears), so the editor can trigger it
  reliably; the first value seen after load is adopted without clearing.
- **Scale tables** are Sprout's, with an independent copy of the semitone sets in the tests.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_MARKOV=ON
cmake --build build --target downspout_markov_core_tests markov-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_markov_core_tests --output-on-failure
```

## Status

Core, tests, wrapper and panel are complete. Host validation in REAPER is pending.
