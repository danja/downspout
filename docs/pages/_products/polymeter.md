---
title: Polymeter
order: 114
bundle: polymeter.vst3
kind: MIDI generator
category: generative
role: Polymetric rhythm
screenshot: /assets/plugins/polymeter.png
capabilities: [MIDI output, host transport, Euclidean rhythm, deterministic seed]
summary: Four Euclidean lanes with independent coprime lengths, rotations, ratchets, probability, accents, notes, and channels.
---

## Automaton lanes

Each lane has an Automaton rule. Zero keeps the Euclidean pattern; 1 to 255 swaps it for a
one-dimensional cellular automaton (try 30, 90, 110, 150) whose pattern evolves every cycle
of the lane. Pulses sets how crowded the starting row is. Idea from Subsequence.

## Opinion

An AI invention. Needs investigating.

## Functionality

Polymeter schedules every event at a sample offset inside the host block and
uses one MIDI bus with note/channel separation.

### Conductor integration

Polymeter has a MIDI input for Conductor CCs only. Set **Conductor ch** (0 = off, 1-16) to the channel
Conductor uses (default 16). CC 21 drives a master **Density** that multiplies every lane's Probability, CC 22 a
master **Energy** that scales every lane's velocity, CC 23 the **Seed** (re-rolling the probability choices and the
automaton rows), and CC 24 (value 127) restarts every lane from its first step at the next bar line. The two
masters are also ordinary parameters in the CONDUCTOR strip under the clock, both at 100% (no change) by default,
so a project that does not use them plays exactly as before. CC 20 (Scene) is not used. See
[MIDI Mapping](../../midi-mapping.md).
