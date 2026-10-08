---
title: Canticle
order: 155
bundle: canticle.vst3
kind: Instrument
category: instrument
role: Polyphonic tonal synth
screenshot: /assets/plugins/canticle.png
summary: 12-voice keys, reed, pad, pluck, and glass instrument for melody, counterpoint, and chord generators.
---

## Opinion

Claude says "stable middle voice" to describe this thing, which is accurate. Very useful when an extra layer is needed in something, but the sounds are rather boring on their own.

## Functionality

Canticle is a stable middle voice for the Downspout ensemble. It gives
`melgen`, `counterpointer`, and `cadence` a clear polyphonic destination when
the part should read as keys, reed, pad, pluck, or glass rather than bass,
percussion, glitch, or hybrid physical synthesis.

**Pitch bend** is applied per MIDI channel (2 semitones by default, set per channel with
RPN 0) and voices are keyed by channel and note, so it follows the one-note-per-channel
output of [Retune](/downspout/plugins/retune/).

### Status

This is a very basic voice, perfectly usable but not very interesting. A bit more finesse is needed in the DSP.