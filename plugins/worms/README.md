# ToneWorm

Transport-synchronized MIDI generator using Paterson's Worm navigation on a Tonnetz pitch lattice.

## Status

Phase 1 (portable core) complete. Phase 2 (DPF wrapper) complete. Phase 3 (UI) complete. Pending: integration into release scripts, screenshot capture.

## Concept

The **Tonnetz** is a hexagonal lattice of pitch classes where:
- Moving East: +perfect fifth (+7 semitones)
- Moving NE: +major third (+4 semitones)
- Moving NW: +minor third (+3 semitones, derived)

A **Paterson's Worm** navigates this lattice by following a rule table: given the incoming direction, the rule selects one of five turns (L120 / L60 / Fwd / R60 / R120 relative to the incoming direction). The worm's path becomes the melody.

## Controls

| Control | Description |
|---------|-------------|
| Root | Root pitch class of the lattice (C–B) |
| Register | Octave range (−2 to +2 relative to C4) |
| Step | Note grid: ¼, ⅛, 1/16, 1/32 |
| Length | Pattern steps: 16, 32, 64, or 128 |
| Density | Note vs rest probability |
| Velocity | Base MIDI velocity |
| Vary | Rule mutation rate at loop boundaries |
| Seed | Deterministic RNG seed |
| Cond. Ch | Conductor MIDI channel (0=off) |
| Rule 0–5 | Turn table: L120/L60/Fwd/R60/R120 per incoming direction |
| Quantize | Snap pitch classes to a standard scale |
| Scale | Scale for quantization |
| MIDI Ch | Output MIDI channel |
| Randomize | Randomize all six rules |
| Mutate | Mutate one random rule |

## Conductor Integration

When Cond. Ch is set, receives:
- CC 21 → Density
- CC 22 → Velocity
- CC 23 → Vary
- CC 24 (value 127) → Mutate

## Melgen Integration

Route ToneWorm MIDI output → Melgen MIDI input. Melgen's follow mechanism
will pull its melody toward ToneWorm's pitch classes.
