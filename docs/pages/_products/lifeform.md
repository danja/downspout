---
title: Lifeform
order: 180
bundle: lifeform.vst3
kind: MIDI generator
category: generative
role: Launchpad Game of Life sequencer
screenshot: /assets/plugins/lifeform.png
summary: Conway Game of Life MIDI generator where the Launchpad grid evolves one beat at a time.
---

## Opinion

This was irresistable given the grid layout of the Launchpad. Good fun but not much use in  practice.

## Functionality

Lifeform turns an 8x8 Launchpad-style grid into Conway's Game of Life. Living
cells generate melodic, multi-channel, or drum MIDI events, then the pattern
evolves on the next beat. The UI and Launchpad can both flip cells, load seeds,
randomize the board, step manually, and show LED feedback. Unhandled controller
MIDI is blocked by default, with a `Pass` switch when forwarding is intentional.

### Conductor integration

Set **Conductor Ch** (the **Cond** button at the bottom right of the panel; click to cycle Off, then 1-16)
to the channel Conductor uses (default 16). CC 21 drives density, CC 22 velocity, CC 23 mutation, and CC 24
(value 127) draws a fresh random pattern, so the board gets busier, louder and more volatile as the arrangement
builds and reseeds at section boundaries. CC 20 (Scene) is not used. The Conductor CCs still pass through when
**Pass Input** is on. See [MIDI Mapping](../../midi-mapping.md).

### Status

A fun experiment, taken to proof-of-concept. A glider pattern did once emerge on the Launchpad.