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
