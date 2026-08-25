---
title: Arpgen
order: 95
bundle: arpgen.vst3
kind: MIDI effect
role: Transport arpeggiator
screenshot: /assets/plugins/arpgen.png
summary: Meter-aware chord-capture and scale-derived arpeggiator with four traversal orders and triplet grids.
---

## Opinion

Basically working but barely tested, will no doubt need a few revisions.

## Functionality

Arpgen turns incoming MIDI into transport-locked single-note patterns. Chord
mode captures a new source chord from a configurable fraction of each bar.
Scale mode uses held notes as register anchors for in-key runs, triads, or
sevenths. Both modes support one to four octaves, velocity following, gate
control, straight and triplet rates, and endpoint-safe alternating patterns.

### Status

The portable core, deterministic tests, VST3 wrapper, and custom UI are in
place. Host validation remains ongoing.
