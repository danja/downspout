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
  Fugue-friendly subject/dominant-answer region. An optional Inertia control
  applies Narmour-style implication: after a leap the next move tends to turn
  back without overshooting, after a step it tends to continue.
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
  color, rhythmic response, and strict dominant-answer imitation. Its Inertia
  control adds a scoring term that favours turning back after a leap and
  continuing after a step (Counterpoint mode only).
- `Polymeter`: four lanes of different lengths on one transport, each lane
  either a Euclidean pattern or a one-dimensional cellular automaton. The
  automaton (Wolfram rule 1-255, wrapping edges) starts from a seeded row whose
  density is Pulses/Length and advances one generation per cycle of the lane,
  returning after 64 generations. All choices are pure functions of the step
  position and seed, so block size and loops do not change the output.
- `Harmonic Atlas`: seeded chord progressions from four movement tables.
  Gravity replaces chords with functional targets chosen from the previous
  chord (V goes home, IV goes to V, I moves to IV, V or vi, anything else
  drifts to I, IV or V). Voice-leading above 50% voices each chord by choosing
  the inversion and octave whose sorted voices move least from the previous
  chord, itself voiced six chords back so that any position renders the same.
- `Sprout`: an L-system sequencer. Seven grammars (Plant, Koch, Dragon,
  Sierpinski, Cantor, Levy, Tree) are rewritten for N generations, capped at
  4096 symbols, and read as a turtle: F/G play a note, f rests, + and - move the
  pitch by scale degrees, brackets branch and return, other letters only grow.
  Pitch folds back at a set range, branch depth lowers velocity, and Grow adds
  one generation every N bars, restarting the pattern at each change.
- `Lifeform`: Conway's Game of Life drives the MIDI output, advancing the grid
  one beat at a time and turning active cells into musical events.
- `Luma`: an 8x8 Launchpad grid drives a small set of musical agents where
  cells can act as bass, chord, melody, or drum sources.

## Audio effects

- `P-Mix`: transport-aware probabilistic audio gating and blending with fades.
- `E-Mix`: Euclidean stereo gating that carves audio into repeating patterns
  using density, block, and fade controls.
- `Rift`: short-buffer capture and disruption with chop/stutter repeats,
  reverse, skip, smear, and pitch-slip actions.
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

- `Retune`: Scala microtuning. A `.scl` file (cents or ratio lines) gives the
  scale; each note is rounded to the nearest 12-tone key and corrected by a
  14-bit pitch bend sent on its own output channel (2-16) ahead of the note-on,
  with oldest-note stealing. CC and program change are broadcast to the pool,
  polyphonic aftertouch follows its note, incoming pitch bend and channel
  aftertouch are dropped. The receiving synth must bend per channel.

## Instruments

Canticle, Moka, Floozy, Syrinx and Pratt apply pitch bend per MIDI channel
(range 2 semitones by default, set per channel by RPN 0) and key their voices by
channel and note, so one-note-per-channel input such as Retune's bends each note
on its own.

- `Basilico`: a monophonic bass synth with glide, accent, body, drive,
  tempo-aware wobble modulation for amplitude/filter/phase movement, and acid
  squelch across upright, electric, dub, acid, and industrial tones.
- `DrumKit`: a triggered drum synthesizer with a fixed MIDI map, voice
  controls, and mixer-style mute strips.
- `Floozy`: an 8-voice hybrid synth combining distortion, physical-model-style
  excitation, feedback, filtering, modulation, and reverb.
- `Gremlin`: a chaotic glitch synth with sound modes, scenes, fader macros,
  performance actions, hold pads, and MIDI LED feedback.
- `Canticle`: a 12-voice tonal synth for readable keys, reed, pad, pluck, and
  glass parts driven by melody, counterpoint, and harmony generators.
- `Plank`: eight strings, one per Launchpad grid column. Each column's eight
  rows are one octave of the current scale. Two 32-bit phase oscillators are
  summed as a polyBLEP saw pair with the second inverted; a morph axis
  crossfades that into a band-limited wavetable pair read at a quarter-cycle
  offset, and the result feeds a two-pole resonant filter whose coefficient is
  modulated by a second envelope.

- `Pratt`: Pratt polynomials `f_1 = 1`, `f_2 = x`, `f_p = 1 + f_{p-1}` (odd prime
  p), `f_n = prod f_p^v`, so `f_n(2) = n` and `f_mn = f_m f_n`. The filter
  `H_n(s) = n / f_n(2 + s/w0)` is all-pole, unity at DC and strictly stable.
  Cascading two filters multiplies the indices, `H_m H_n = H_mn`. Voices sum
  partial k with weight `k^-roll * H_n(i xi k)` for `n = (note + 1) x base`
  into a band-limited table; the filter mode factors the roots into biquads and
  crossfades old and new cascades when index or cutoff changes. Based on the
  original [pratt-synth](https://github.com/githubuser1983/pratt-synth).

## How the site treats these methods

The plugin cards on the GitHub Pages site stay short on purpose. This page is
the place to read the implementation patterns in one place when comparing
generators, effects, and instruments.
