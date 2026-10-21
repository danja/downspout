---
title: Flues Synth Driver
order: 121
bundle: flues_synth_driver.vst3
kind: MIDI controller
category: midi
role: Parameter-to-CC controller for the Flues synth
screenshot: /assets/plugins/flues-synth-driver.png
summary: Turns its panel controls into MIDI CCs and program changes that play the external Flues vocal and physical-model synthesizer.
---

## Functionality

Flues Synth Driver is a control surface for the external Flues synthesizer. It
holds no audio of its own: each panel parameter is sent as a MIDI CC, and a
chain places it before the synth. Controls cover the Disyn algorithm and its
three parameters, noise and DC level, interface type, formants F1-F4, the
nasal, sing, shout and fry modes, attack, release, tuning, delays, filter
feedback, gain and trajectory.

There are 31 selectable programs (MIDI program change 0-30). Each remaps nine
sliders to a different set of CCs. Only parameters that have changed are sent,
and the first block after activation resends the sliders so the synth starts in
step with the panel. Output channel and a Conductor channel are selectable.
Incoming MIDI passes through unless blocked. Panic and Randomize actions are
provided. It does not use the host transport.

The panel groups controls into Oscillator, Model, Formants, Vocal / Env,
Effects, Filter, LFO and Trajectory. Controls the selected program does not map
are dimmed.
