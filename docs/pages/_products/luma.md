---
title: Luma
order: 160
bundle: luma.vst3
kind: MIDI generator
category: generative
role: Launchpad performance generator
screenshot: /assets/plugins/luma.png
summary: Launchpad-oriented performance generator where lit pads become bass, chord, melody, and drum agents.
---

## Opinion

Yet another attempt to make something interesting out of the Launchpad. It works quite nicely, but isn't very compelling, needs review.

## Functionality

Luma turns an 8x8 Launchpad-style grid into a set of lightweight musical
agents. Lower rows create stable bass and chord movement, upper rows add melody
fragments and drum sparks, and the plugin sends LED feedback back to the
controller when the host routes its MIDI output to the Launchpad. Unhandled
input MIDI is blocked by default, with a `Pass` switch for deliberate forwarding.

### Conductor integration

The **Cond** button (click to cycle Off, then channels 1-16) sets the channel Luma listens to for
Conductor (default 16). CC 21 drives Density, CC 22 Energy, and CC 24 (value 127) scatters a fresh
pattern. CC 20 (Scene) and CC 23 (Mutation) are not used, since Luma has no mutation control. The
Conductor CCs still pass through when Pass is on. Off by default; the setting is saved
(`conductor_ch`). See [MIDI Mapping](../../midi-mapping.md).

### Status

Basically functional but so far of limited practical use.