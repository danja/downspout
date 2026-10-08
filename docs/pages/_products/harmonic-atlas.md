---
title: Harmonic Atlas
order: 110
bundle: harmonic_atlas.vst3
kind: MIDI generator
category: generative
role: Autonomous harmony
screenshot: /assets/plugins/harmonic-atlas.png
capabilities: [MIDI output, host transport, autonomous harmony, deterministic seed]
summary: Seeded autonomous chord and scale guidance with tonal, modal, chromatic-mediant, and neo-Riemannian-inspired movement.
---

## Gravity

The Gravity slider pulls the progression towards functional harmony: V resolves home, IV
leads to V, I moves on to IV, V or vi, and everything else drifts back to I, IV or V.
At 0 the movement is as before. Idea from Subsequence's chord graph with key gravity.

## Voice-leading

With Voice-leading above 50% (the default is 75%), each chord is voiced to move as little as
possible from the chord before it, instead of being stacked from scratch. 100% is strictest;
lower values allow near-best alternatives. At 50% or below chords are stacked as before.

## Opinion

An AI invention. I haven't played with it properly yet.

## Functionality

Harmonic Atlas generates bounded, voice-led harmony without MIDI input and can
optionally follow incoming pitch classes. Stop, loop, seek, and tempo behavior
are derived from absolute host position.
