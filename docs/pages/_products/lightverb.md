---
title: Lightverb
order: 49
bundle: lightverb.vst3
kind: Audio effect
role: Low-CPU stereo reverb
screenshot: /assets/plugins/lightverb.png
capabilities: [stereo audio, MIDI CC input, zero latency, auxiliary send]
summary: Fixed-cost stereo reverb with Producer Control Bus lifecycle, channel isolation, and MIDI-through.
---

## Opinion

The UI needs some more work, but its sound is very pleasing, in a cold dark way. The producer part needs attention (across all the related plugins).

## Functionality

Lightverb provides inexpensive space after T-Mix or Loopdelay, or on a 100% wet
auxiliary return. Producer MIDI can take over Wet mix with CC 32 and Space with
CC 33 without altering the saved manual settings.
CC 19 ownership and an optional channel filter allow several independent mix
chains to share the same project safely.
