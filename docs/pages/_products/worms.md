---
title: ToneWorm
order: 115
bundle: worms.vst3
kind: MIDI generator
role: Tonnetz melody generator
screenshot: /assets/plugins/worms.png
summary: Paterson's Worm navigates a Tonnetz pitch lattice, generating transport-locked melodies from six directional rules with scale quantization and Conductor CC control.
---

## Opinion

Produces melodic lines with an organic quality that pure random or step-sequence generators rarely match. The Tonnetz constraint means consecutive notes are always harmonically related, even when rules are randomized.

## Functionality

ToneWorm places a worm on a toroidal Tonnetz lattice — a 2D grid where east/west moves by a perfect fifth, northeast/southwest by a major third, and northwest/southeast by a minor third. At each transport step the worm applies a rule: given its incoming direction, the rule selects one of five turns (L120, L60, Straight, R60, R120). The pitch class at the worm's lattice position becomes the output note. Six rules (one per incoming direction) are configured via button grids or randomized/mutated with one click.

### Key controls

- **Root / Register** — tonal center and octave range.
- **Step / Length** — note grid resolution (¼ to 1/32) and pattern steps (16–128) before looping.
- **Rules (r0–r5)** — turn choice per incoming direction; Randomize and Mutate actions for quick exploration.
- **Density / Velocity / Vary** — note probability, base velocity, and mutation rate at loop boundaries.
- **Quantize / Scale** — snap Tonnetz pitch classes to one of 23 scales.
- **MIDI Ch / Cond. Ch** — output channel and Conductor CC reception channel.

### Conductor integration

Set **Cond. Ch** (0 = off) to receive Conductor's section CCs: CC 21 → density, CC 22 → velocity, CC 23 → vary, CC 24 (value 127) → mutate one rule.

### Melgen integration

Route ToneWorm's MIDI output to MelGen's MIDI input in your DAW. MelGen's follow mechanism responds to the incoming pitch material automatically — no configuration changes needed.

### Status

First release. Core tested with 18 deterministic unit tests. DAW smoke-testing pending.
