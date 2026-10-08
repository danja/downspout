---
title: Floozy
order: 140
bundle: floozy.vst3
kind: Instrument
category: instrument
role: Hybrid physical/modulation synth
screenshot: /assets/plugins/floozy.png
summary: Selectable 1-8 voice (default 4) synthesizer combining distortion sources, physical-model-style excitation, feedback, filtering, modulation, and reverb.
---

## Opinion

This thing is rather eccentric, the controls not altogether intuitive. But it can produce a wide range of sounds, some of them really good.

## Functionality

Floozy is a playable stereo instrument derived from `floozy-poly`. It exposes
source algorithm, interface/body model, quantized body tuning, feedback,
filter, modulation, reverb, and output controls while keeping the voice engine
deterministic and bounded.

**Pitch bend** is applied per MIDI channel (2 semitones by default, set per channel with
RPN 0) and voices are keyed by channel and note, so it follows the one-note-per-channel
output of [Retune](/downspout/plugins/retune/).

### Status

This can make many useful sounds but it is a little hit & miss trying to find them.