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

Plank re-implements Plinky's voice engine as a portable C++ core: a polyBLEP
oscillator pair, a morph axis into seventeen band-limited wavetables, Plinky's
resonant two-pole filter, two envelopes, dynamic parameter smoothing, and a tape
delay, shimmer reverb, mid/side width and soft output stage.

The eight Launchpad columns are the eight strings and the eight rows are one
octave of the current scale, so a scale degree is a pad and the polyphony is
something you hold rather than something the plugin finds for you. The UI spells
out the pitch each row plays. Top buttons step octave and morph, engage latch,
raise drive and resonance, toggle LED feedback, and panic. Side buttons pick a
performance scale.

### Status

Playable and building. The granular sampler, arpeggiator and step sequencer from
Plinky are deliberately absent, and the wavetables are synthesised rather than
Planky's generated data, so the timbre sweep is close to the hardware but not
identical to it.

The eight strings are tuned apart by `Spread`, defaulting to `Scale` so the grid
is a two-octave scale surface and holding a row plays a cluster. `Unison`
restores the original single-ladder behaviour.
