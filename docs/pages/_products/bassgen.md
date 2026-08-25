---
title: BassGen
order: 10
bundle: bassgen.vst3
kind: MIDI generator
role: Bassline generator
screenshot: /assets/plugins/bassgen.png
summary: Transport-synced bassline generator with style, scale, Moroder/Fugue/Jazz genres, variation, color, and MIDI follow/dodge controls.
---

## Opinion

Not bad at all. Excels at very repetitive patterns (Conductor &  Ground are useful adjuncts).

## Functionality

BassGen creates monophonic bass parts that follow host transport and saved
pattern state. It can regenerate or mutate rhythmic material, choose from
scale/style controls, add harmonic color, and use incoming MIDI context to
follow or avoid drum and bass hits. Input matching can use an exact note, a
whole channel, or any note, with sensitivity for guided companion parts from
sources such as Ground. Its Jazz mode outlines ii-V-I-turnaround
movement with chord-tone targeting, dominant color, chromatic approaches, and
simple enclosures. Its Fugue mode uses a tonic subject, dominant answer,
episode, and tonic pedal/cadence shape for Bach-like continuo movement.
Moroder mode makes short, bright, repetitive subdivision-pulse bass lines with
slider-shaped holes and color turns.

### Conductor integration

Set **Conductor Ch** (0 = off, 1–16) to receive Conductor's section CCs on
the matching channel. Conductor CC 21 drives density, CC 22 drives accent,
CC 23 drives variation rate, and CC 24 (value 127) triggers a new pattern.
Route Conductor's MIDI output to the BassGen track and match the channel
(Conductor default output channel is 16). See [MIDI Mapping](../../midi-mapping.md).

### Status

This works quite nicely for short, repetitive phrases. Its tracking of input midi still needs more work.
