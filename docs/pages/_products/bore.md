---
title: Bore
order: 214
bundle: bore.vst3
kind: Instrument
category: instrument
group: synths
role: Lip-reed waveguide instrument
screenshot: /assets/plugins/bore.png
capabilities: [audio output, MIDI note and CC input]
summary: "An eight-voice lip-reed instrument: MIDI pitch tunes the feedback bore; Pressure, Embouchure, Bell and Breath shape the sound."
---

## Functionality

Bore uses a driven lip-like oscillator to excite a pitch-tuned feedback delay.
Pressure changes reed drive and loop sustain, Embouchure moves the lip oscillator
relative to the bore, Bell changes feedback damping, and Breath adds filtered
noise. It is a compact waveguide sound-design model, not a calibrated trumpet
simulation. There are eight MIDI voices, an Attack/Release envelope, velocity
sensitivity and a channel filter.

CC 1-4 control Pressure, Embouchure, Bell and Breath; CC 7 controls Level.
The core tests and VST3 build pass. Listening and host save/reopen validation
remain pending.
