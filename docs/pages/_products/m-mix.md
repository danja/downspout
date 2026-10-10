---
title: M-Mix
order: 40
bundle: m_mix.vst3
kind: MIDI effect
category: midi
role: MIDI gate
screenshot: /assets/plugins/m-mix.png
summary: MIDI gate combining probabilistic bar transitions with Euclidean block patterns.
---

## Opinion

I'd forgotten I made this. Need to remember for generative stuff with Transmission.

## Functionality

M-Mix processes incoming MIDI notes rather than audio. Notes pass only when the
current probability and Euclidean gates are open, making it useful for rhythmic
note thinning, comping variation, and transport-locked MIDI movement.

### Conductor

Set **Conductor Ch** to the channel Conductor sends on (it defaults to 16; 0 is off). CC 21 (Density) sets Open Bias, so denser sections let more notes through, and CC 22 (Energy) sets Maintain inverted, so more energetic sections change the gate more often. The CCs move the sliders as if you had, so you can still override them by hand.

### Status

Functional but awaiting review in practical application.