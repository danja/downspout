---
title: Loopdelay
order: 48
bundle: loopdelay.vst3
kind: Audio effect
role: Transport-synced delay and capture looper
screenshot: /assets/plugins/loopdelay.png
capabilities: [stereo audio, MIDI CC input, host transport, capture loop]
summary: Producer-ready stereo delay and looper with BBT time, CC 19 ownership, channel isolation, and MIDI-through.
---
## Opinion

I've hardly looked at this.

## Functionality

Loopdelay is intended to follow T-Mix and precede Guardian. Use it as a
filtered ping-pong delay or capture a transport-sized loop and continuously
overdub it. Fixed CC 30 controls time and CC 31 controls feedback without
overwriting the saved manual settings.
It participates in Producer Control Bus lifecycle and forwards the control
stream to Lightverb or another downstream effect.
