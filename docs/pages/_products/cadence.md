---
title: Cadence
order: 90
bundle: cadence.vst3
kind: MIDI effect
category: midi
role: Harmony and comping
screenshot: /assets/plugins/cadence.png
summary: Transport-aware MIDI harmonizer and comping generator with learned harmony, color, extended voicings, suspended dominants, and arpeggiated phrasing.
---

## Opinion

It is hard to tell ahead of time if this will work well on a given bassline/melody, but when it does it is good, surprisingly musical.

## Functionality

Cadence listens to incoming MIDI and uses the learned material to produce
transport-synced harmony and comping output. The color control biases learned
progressions toward stronger Jazz cadence roles and dominant tension, while
major/minor scales can lean into circle-of-fifths support and suspended
dominants. The extended chord mode can voice 9ths, 11ths, and 13ths. Spread
opens the voicing range, and Arpeggio moves playback from solid chords toward
broken fragments or rotating single-note chord tones.

### Conductor integration

Set **Conductor Ch** (0 = off, 1-16) to the channel Conductor uses (default 16). CC 21 drives Complexity,
CC 22 Movement, CC 23 Variation, and CC 24 (value 127) relearns, so each section can start from fresh
harmony. CC 20 (Scene) is not used. See [MIDI Mapping](../../midi-mapping.md).

### Status

This basically works as intended though a little more work is needed in fine-tuning the user interface, it could be more intuitive.