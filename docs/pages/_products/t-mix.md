---
title: T-Mix
order: 45
bundle: t_mix.vst3
kind: Audio mixer
role: Eight-channel mixer
screenshot: /assets/plugins/t-mix.png
capabilities: [eight mono audio inputs, stereo output, MIDI CC input, producer gain status]
summary: Eight mono strips with manual controls, click-smoothed producer overlays, channel-isolated CC 19 ownership, and MIDI-through.
---

## Functionality

T-Mix combines eight separately routable mono inputs into a stereo output.
Every strip provides a pre-fader meter, level fader, constant-power pan, Mute,
and Solo, followed by a stereo master fader. MIDI CC 20-27 add transient
producer gain over channels 1-8 without overwriting that saved manual balance.
CC 19 lifecycle, optional gate enforcement, channel filtering, and MIDI-through
let one Producer Control Bus continue into downstream effects.

### Status

The complete Mixgen-to-T-Mix-to-Loopdelay-to-Lightverb portable contract has
deterministic coverage. Broader host routing tests remain useful.
