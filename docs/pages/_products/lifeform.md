---
title: Lifeform
order: 180
bundle: lifeform.vst3
kind: MIDI generator
role: Launchpad Game of Life sequencer
screenshot: /assets/plugins/lifeform.png
summary: Conway Game of Life MIDI generator where the Launchpad grid evolves one beat at a time.
---

## Functionality

Lifeform turns an 8x8 Launchpad-style grid into Conway's Game of Life. Living
cells generate melodic, multi-channel, or drum MIDI events, then the pattern
evolves on the next beat. The UI and Launchpad can both flip cells, load seeds,
randomize the board, step manually, and show LED feedback. Unhandled controller
MIDI is blocked by default, with a `Pass` switch when forwarding is intentional.

### Status

A fun experiment, taken to proof-of-concept. A glider pattern did once emerge on the Launchpad.