---
name: drum-voice-plugin
description: Design a MIDI-triggered drum or percussion generator plugin the way thumper, snatch, tops and clatter were built (GM note maps, velocity, voice pool, choke groups, deterministic noise). Use when adding a drum voice plugin or extending a GM note map.
---

# Drum / percussion voice plugins

Existing split of the GM drum map: Thumper 35/36; Snatch 37 (side stick), 38 (selected engine), 40 (electronic); Tops 42, 44, 46, 49, 51-57, 59, 81; Clatter 39, 41, 43, 45, 47, 48, 50, 58, 60-70, 73-77, 80, 82-87. Uncovered: whistles 71/72, cuicas 78/79. Check `plugins/clatter/tests/clatter_core_tests.cpp` (coverage test) before assigning notes, and update it and the READMEs when the map changes.

## Conventions
- **Note Map** parameter: `Any note` (plays the selected voice from any note) and `GM` (only the plugin's GM notes, each mapped to its sound). **Default GM** across the drum plugins. Plugins that also have a "Selected" mode: channel 10 plays the fixed pitch, other channels use the note's pitch per hit. Append map values; never reorder (persisted ints).
- **Velocity** scales level on every trigger; test that it does.
- **Deterministic noise**: a tiny xorshift32 in `<plugin>_rng.hpp` (namespace `downspout::<plugin>`), seeded per instance/voice, never `rand()`. Same input renders identically.
- **Voices**: fixed pool (clatter: 16) with oldest-steal; skip idle voices and zero their filter state (denormals). Choke groups (open/mute conga, guiros, surdos) with a short (3 ms) fade, not a hard cut.
- **Table-driven patches** (`Patch` struct per sound) beat per-sound code. Per-patch gain is calibrated from a probe of peak/RMS at velocity 1 (peaks about 0.2-0.75); keep the probe, do not guess.
- Shared read-only DSP: `downspout/dsp/svf.hpp` for band-pass noise. Do not modify shared headers without approval.
- Envelopes with incremental multipliers; clamp decays so long settings cannot run away. Fixed per-sound pan scaled by a Width control.
- State: parameters only; no state blobs (they were removed from all four). Presets live in the UI (see `magneto-ui`).
- Defaults in core `Params`, the DPF wrapper and the UI table must agree; add a test that the fresh-instance sound is the intended one.

## Test list (see `plugin-core-tests`)
Each note/sound produces output and differs from the others; velocity scales; every control changes the render; choke groups cut the right partner (use windows after the choke time); determinism; serialization with optional new fields; coverage against the other drum plugins' note maps; voice-pool stress and steal; CPU time rendering worst-case polyphony (rerun alone if it fails under load).

## Process
Follow `new-plugin` for the file layout, lists, docs and screenshot, and add a "Drum generators" mention in `docs/summary.md`.
