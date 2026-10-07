---
title: Plank
order: 157
bundle: plank.vst3
kind: Instrument
category: instrument
role: Eight-voice synthesizer
screenshot: /assets/plugins/plank.png
summary: Eight-voice polyphonic synthesizer for the Launchpad grid, with a Plinky-derived wavetable voice and Plonk-style resonator engines.
---

## Opinion

Plinky is the best eight-voice synth I have ever owned and the grid mapping is
the obvious thing to do with it, so this one is less a sound design exercise than
a translation. The column-per-string layout is the part worth stealing: on almost
every grid instrument the polyphony is an implementation detail, and here it is
the interface.

## Functionality

Plank is an eight-voice polyphonic synthesizer played from a Launchpad's 8 x 8 grid.
Each column is one monophonic voice and each row a degree of the current scale, so the
polyphony is something you hold rather than something the plugin finds for you. Every
pad on the screen is labelled with the note it plays.

A voice is one of two things, chosen with `Engine`:

- **Plinky's wavetable voice**, re-implemented as a portable C++ core: a polyBLEP
  oscillator pair, a morph axis into seventeen band-limited wavetables, Plinky's
  resonant two-pole filter and two envelopes.
- **A struck resonator**, after the Intellijel/AAS Plonk Eurorack module: a mallet or
  noise burst excites eight resonant modes tuned like a bar, marimba, drumhead, membrane,
  plate or string. `Strike`, `Damp` and `Material` shape it; `Morph` becomes the strike's
  hardness.

Both go through the same tape delay, shimmer reverb, mid/side width and soft output stage.
Top buttons on the Launchpad step octave and morph, engage latch, raise drive and
resonance, toggle LED feedback, and panic; side buttons pick a performance scale. Selectors
in the UI open drop-down lists. See `plugins/plank/docs/layout.md` for the full surface.

The strings are tuned against each other by `Spread`. The default, `Stride`, is Plinky's own
rule: each string a set number of semitones above the last (7 by default, roughly a fifth),
snapped to the nearest note of the scale so it never leaves the key.

### Status

Playable and building, with deterministic core tests for the pitch mapping, every scale, the
resonator engines and session state. The granular sampler, arpeggiator and step sequencer
from Plinky are deliberately absent, and the wavetables are synthesised rather than Plinky's
generated data, so the timbre sweep is close to the hardware but not identical. Plonk's own
DSP is not public, so the resonator mode ratios are textbook approximations, not a copy.
Host validation in REAPER is still pending.
