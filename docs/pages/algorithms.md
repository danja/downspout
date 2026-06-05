---
layout: default
title: Algorithms
description: A summary of the generation and processing methods used by the current Downspout plugins.
permalink: /algorithms/
---

# Downspout Algorithms

This note summarizes the main generation and processing methods used by the
current Downspout plugins.

## MIDI generators

- `BassGen`: transport-synced monophonic bass generation with style and scale
  vocabularies, rhythm regeneration, color-driven harmonic tension, MIDI
  follow/dodge behavior, Jazz-specific ii-V-I / turnaround targeting, and
  Fugue subject/answer bass behavior.
- `DrumGen`: transport-aware drum pattern generation with fill logic, style
  modes, genre vocabularies, pattern mutation around the current cycle, and a
  sparse Fugue pulse mode.
- `MelGen`: phrase-aware monophonic melody generation with contour, call and
  answer, structure, range, color, follow controls, and a high-structure
  Fugue-friendly subject/dominant-answer region.
- `Ground`: longer-horizon bass generation that plans phrase roles, movement,
  cadences, pedal phrases, color, and high-sequence subject/answer forms across
  sections rather than just local bars, with explicit Dub and Jazz styles and
  generated notes folded into a guarded bass register lane.
- `Cadence`: learned harmony generation from captured MIDI, with scale-aware
  voicing, chord-size modes, comp scheduling, variation, arpeggiated playback
  phrasing, Jazz ii-V-I bias, and high-color circle-of-fifths/suspended
  dominant support.
- `Counterpointer`: learns an incoming MIDI line and answers it with a
  monophonic complementary line that can lean into contrary motion, chromatic
  color, rhythmic response, and strict dominant-answer imitation.
- `Lifeform`: Conway's Game of Life drives the MIDI output, advancing the grid
  one beat at a time and turning active cells into musical events.
- `Luma`: an 8x8 Launchpad grid drives a small set of musical agents where
  cells can act as bass, chord, melody, or drum sources.

## Audio effects

- `P-Mix`: transport-aware probabilistic audio gating and blending with fades.
- `E-Mix`: Euclidean stereo gating that carves audio into repeating patterns
  using density, block, and fade controls.
- `Rift`: short-buffer capture and disruption with repeat, reverse, skip,
  smear, and pitch-slip actions.
- `PaunchLad`: a dub-style performance effect built around delay throws,
  sirens, spring splashes, dropouts, and rhythmic chops.

## MIDI effects

- `M-Mix`: MIDI gating that combines transport-locked probability decisions
  with Euclidean block patterns.
- `Gremlin Driver`: modulation and action sequencing that emits CC movement,
  action notes, and patch-randomization bursts for `Gremlin`.
- `Cadence`: input-aware MIDI harmonizing and comping that learns a cycle,
  rebuilds voicings from captured material, and can thin or break chords with
  `Spread` and `Arpeggio`.

## Instruments

- `Basilico`: a monophonic bass synth with glide, accent, filter, body, and
  drive shaping for upright, electric, dub, acid, and industrial tones.
- `DrumKit`: a triggered drum synthesizer with a fixed MIDI map, voice
  controls, and mixer-style mute strips.
- `Floozy`: an 8-voice hybrid synth combining distortion, physical-model-style
  excitation, feedback, filtering, modulation, and reverb.
- `Gremlin`: a chaotic glitch synth with sound modes, scenes, fader macros,
  performance actions, hold pads, and MIDI LED feedback.
- `Canticle`: a 12-voice tonal synth for readable keys, reed, pad, pluck, and
  glass parts driven by melody, counterpoint, and harmony generators.

## How the site treats these methods

The plugin cards on the GitHub Pages site stay short on purpose. This page is
the place to read the implementation patterns in one place when comparing
generators, effects, and instruments.
