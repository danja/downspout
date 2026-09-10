# Moka

Moka is a stereo MIDI synth based on modal (physical) synthesis. Each voice
is a bank of eight damped sine modes excited by a short mallet transient,
which emulates struck objects: xylophone bars, glockenspiel, woodblocks,
glass bowl, metal sheet, and a soft flip-flop-struck tube.

## Build

Moka is enabled by default with `DOWNSPOUT_BUILD_MOKA=ON` and builds as
`moka.vst3` when DPF is available.

```bash
cmake -S ../.. -B ../../build -DDOWNSPOUT_BUILD_MOKA=ON
cmake --build ../../build --target moka-vst3 downspout_moka_core_tests
ctest --test-dir ../../build --output-on-failure -R moka
```

## Implementation

The portable core (`include/moka_engine.hpp`, `src/moka_engine.cpp`) handles
MIDI note allocation with a selectable 1–12 voice cap (default 4), modal
resonators with per-mode decay, mallet/spread/position morphing, stereo
alternating-partial panning, bounded output, and deterministic rendering.

The DPF wrapper (`src/dpf/MokaPlugin.cpp`) exposes ten automatable
parameters; the UI (`src/dpf/MokaUI.cpp`) follows the shared Cold War
test-equipment look & feel with dark/light panel themes and a single
factory preset menu that shows `Custom` once any control is tweaked.

## Parameters

| # | Name | Range | Default | Notes |
|---|------|-------|---------|-------|
| 0 | Instrument | Xylophone…Tube | Xylophone | Selects the modal table (via presets or automation) |
| 1 | Decay | 0–100 % | 55 % | Overall resonance time, 0.15x–3x |
| 2 | Mallet | 0–100 % | 75 % | Beater hardness: partial brightness + click |
| 3 | Tone | 0–100 % | 65 % | Resonator lowpass |
| 4 | Spread | 0–100 % | 30 % | Inharmonic stretch, 0.6x–1.5x of table ratios |
| 5 | Position | 0–100 % | 45 % | Strike point: edge (bright) to centre (round) |
| 6 | Voices | 1–12 | 4 | Polyphony cap with oldest-voice stealing |
| 7 | Level | 0–100 % | 70 % | Output gain |
| 8 | Release | 30ms–1.5s | 97ms | Note-off ring-out, choke to long tail |
| 9 | Width | 0–100 % | 70 % | Stereo spread of the modal partials |

## Presets

Xylophone, Glockenspiel, Temple Blocks, Glass Bowl, Frost Glass,
Metal Sheet, Thunder Plate, Tube Flip-Flop, Dark Tube, Soft Xylo.
Selecting a preset writes all ten controls; tweaking anything shows
`Custom` in the preset menu.
