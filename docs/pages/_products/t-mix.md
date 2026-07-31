---
title: T-Mix
order: 45
bundle: t_mix.vst3
kind: Audio mixer
role: Eight-channel mixer
screenshot: /assets/plugins/t-mix.png
capabilities: [eight mono audio inputs, stereo output, MIDI CC input, producer gain status]
summary: Eight mono strips with manual mix controls plus click-smoothed Mixgen producer overlays on CC 20-27.
---

## Functionality

T-Mix combines eight separately routable mono inputs into a stereo output.
Every strip provides a pre-fader meter, level fader, constant-power pan, Mute,
and Solo, followed by a stereo master fader. MIDI CC 20-27 add transient
producer gain over channels 1-8 without overwriting that saved manual balance.

### Status

The portable producer contract and Mixgen-to-T-Mix signal path have deterministic
core coverage. Broader host routing tests remain useful.
