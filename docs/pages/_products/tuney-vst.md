---
title: Tuney VST
order: 158
bundle: tuney_vst.vst3
kind: Instrument / MIDI generator
role: Text-to-music instrument
screenshot: /assets/plugins/tuney-vst.png
summary: Focused typing and stored text become microtonal synthesized audio and MIDI through configurable character maps, scales, tunings, and human timing.
---

## Functionality

Tuney VST ports the musical core of Tom Ritchford's MIT-licensed Tuney 0.3.39.
It records focused Unicode typing or accepts pasted text, maps characters into a
configurable note range, and replays the result using deterministic millisecond
timing. Its internal synth supports computed, ratio, and table tunings while its
MIDI output emits ordinary note messages for routing to another instrument.

The plugin intentionally does not install a global keyboard listener. Its
Python CLI, recording, speech, device, and desktop file-management features are
outside the VST3 boundary.
