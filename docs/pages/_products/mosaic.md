---
title: Mosaic
order: 116
bundle: mosaic.vst3
kind: Sampler instrument
role: Generative sample variation
screenshot: /assets/plugins/mosaic.png
capabilities: [audio output, MIDI input, host transport, WAV sample state]
summary: Four-slot bounded WAV sampler with seeded slices, pitch variation, reverse, stereo spread, and autonomous triggering.
---

## Opinion

an AI invention. Needs investigating.

## Functionality

Mosaic loads samples outside the audio callback and uses sixteen fixed playback
voices. Invalid or missing files produce silence and a visible status value.
