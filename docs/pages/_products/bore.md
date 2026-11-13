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
summary: "An eight-voice lip-reed instrument: nonlinear reed flow excites a pitch-tuned bore with controllable turbulence and upper-mode capture."
---

## Functionality

Bore couples nonlinear reed flow to a damped, pitch-tuned waveguide. Pressure
changes reed drive and loop sustain, Embouchure moves the reed resonance relative
to the bore, Bell changes feedback damping, and Breath adds filtered noise. Reed
Instability adds turbulent-flow noise and slow lip flutter. Register Capture
couples more of the nonlinear reed airflow into the bore; high pressure and
tighter embouchure can let an upper mode catch without directly transposing the
played note. It is a compact physical sound-design model, not a calibrated
trumpet simulation. There are eight MIDI voices, an Attack/Release envelope,
velocity sensitivity and a channel filter.

CC 1-6 control Pressure, Embouchure, Bell, Breath, Reed Instability and Register
Capture; CC 7 controls Level.
The core tests and VST3 build pass. Listening and host save/reopen validation
remain pending.
