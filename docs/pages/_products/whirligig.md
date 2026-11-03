---
title: Whirligig
order: 238
bundle: whirligig.vst3
kind: Audio effect
category: processor
group: space
role: Rotary speaker
screenshot: /assets/plugins/whirligig.png
capabilities: [audio input, audio output, MIDI input]
summary: A rotary speaker with a drum and a horn rotor, two microphones, inertia between Slow and Fast, and an optional mode that locks the rotation to the host's tempo and bar position.
---

## Opinion

Not yet auditioned in a host. Put it after Wheelhouse (or any organ or guitar) and switch Speed between Slow and Fast; the horn changes quickly and the drum
follows seconds later, as on the real thing. Switch Sync to Beat and the rotors turn in whole beats, so the swirl lands on the bar.

## Functionality

The input is summed to mono and split at the **Crossover** into a low band for the **drum** and a high band for the **horn**. Each rotor has a Slow and a Fast
rate and an **Accel** time for moving between them (or **Stop**). Two microphones, **Mic Angle** apart, hear each rotor as **Swell** (amplitude) and
**Doppler** (pitch). **Mix** blends with the delay-matched dry signal. CC 1 (Fast) and CC 2 (Stop) on the CC channel work a Leslie switch.

**Sync: Beat** turns the rates into beats per turn (8 down to 1/8) and pulls each rotor into phase with the host's BBT position, so loops and relocations
land on the same angle every time. **Phase** moves where the turn starts. The panel shows the rotors turning and *locked to the transport* when both are in phase.

### Status

Core covered by tests (104 checks): flat response, levels, AM depth and lobes, Doppler against the formula, speed changes, microphones, controllers,
beat-rate and lock convergence, block-size independence. Voicing is by ear. Not yet listened to or validated in a host.
