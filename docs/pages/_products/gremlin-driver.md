---
title: Gremlin Driver
order: 120
bundle: gremlin_driver.vst3
kind: MIDI effect
role: Modulation sequencer
screenshot: /assets/plugins/gremlin-driver.png
summary: MIDI modulation and action sequencer with expanded lane shapes intended to drive Gremlin.
---

## Opinion

It does its job, but depends entirely on Gremlin. As I improve that, I'll tweak things here to keep track.

## Functionality

Gremlin Driver sits before Gremlin in a MIDI chain. It passes notes through
while emitting macro CCs, action notes, and patch-randomization bursts that can
animate Gremlin without hand-programming every gesture. Its lanes cover cyclic,
stepped, random, pulse, reverse-ramp, and exponential modulation shapes. The
`Pass Input` switch can block incoming MIDI when only generated control output
should reach the next plugin.

### Status

In progress. It works but is waiting for Gremlin to be tidied up to make it actually useful.