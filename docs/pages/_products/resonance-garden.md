---
title: Resonance Garden
order: 117
bundle: resonance_garden.vst3
kind: Audio effect
category: processor
role: MIDI-tuned resonator
screenshot: /assets/plugins/resonance-garden.png
capabilities: [audio input, audio output, MIDI input, resonator bank]
summary: Up to twelve bounded damped resonators tuned by held MIDI or an internal scale chosen from 23 scales.
---

## Opinion

An AI invention. Needs investigating.

## Functionality

Resonance Garden turns arbitrary audio into pitched evolving material with
damping, inharmonicity, freeze, bounded feedback, and deterministic voice
replacement.

The bank holds up to twelve resonators (**Voice limit**). With no MIDI it sustains on an
internal scale: pick one of 23 scales (BassGen's list) from the **Internal scale** dropdown
and a root with **Internal root**; voice 1 is the root and the rest climb the scale. A held
note takes over a voice, and a released voice returns to the scale once its tail has died
away.
