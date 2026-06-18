---
title: Rift
order: 60
bundle: rift.vst3
kind: Audio effect
role: Buffer disruption
screenshot: /assets/plugins/rift.png
summary: Transport-locked stereo buffer effect with live/sample modes, WAV loading, chop/stutter repeats, reverse, skip, smear, and pitch-slip actions.
---

## Functionality

Rift records short pieces of source audio and reorders or damages them with
transport-aware actions. The source can be live input, a beat-mapped WAV loop,
or both. It is intended for playable glitch, breakbeat disruption, recovery,
scatter, and held-buffer performance gestures.

`Chop` shortens the repeated fragment inside each grid block, so loaded breaks
and live loops can move from block repeats into tighter stutters without
changing the main transport grid.

The sample path maps the loaded file across the declared beat length, defaulting
to four beats, before feeding it into the same rolling-buffer processor as live
input. Current file support is focused on RIFF/WAVE PCM and 32-bit float WAV.
