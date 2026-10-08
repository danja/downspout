---
title: Counterpointer
order: 100
bundle: counterpointer.vst3
kind: MIDI generator/effect
category: midi
role: Counter-melody generator
screenshot: /assets/plugins/counterpointer.png
summary: Learns an incoming MIDI pattern and emits a monophonic answering line with color control.
---

## Opinion

It serves the purpose it was designed for (counterpoint) but can easily go more avant garde than usually required. I've hardly looked at it, just plug it in an go usually.

## Functionality

Counterpointer follows incoming MIDI and generates a complementary monophonic
line. Controls shape how closely it follows, how often it moves contrary to the
source, and how much rhythmic answering, chromatic color, or randomness is
introduced.

**Inertia** (default 0, which leaves the line unchanged) biases the counterline to turn
back after a leap of a fourth or more and to keep going after a step. It applies in
Counterpoint mode, not Bass Descend. Idea from [Subsequence](https://subsystem.co/subsequence).

### Status

Functional, as far as it goes, but could maybe be improved with more deterministic patterns. 