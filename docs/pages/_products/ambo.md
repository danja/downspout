---
title: Ambo
order: 145
bundle: ambo.vst3
kind: Audio effect
role: Ambient processor
screenshot: /assets/plugins/ambo.png
summary: Stereo ambient effect with rearrangeable time, spectral, tape, shimmer, delay, drive, and feedback stages.
---

## Opinion

It's still a good idea, but the implementation isn't great, definitely needs a few more sysles before it's likely to be useful.

## Functionality

Ambo is a stereo ambient processor built from modular time, spectral, tape,
shimmer, delay, drive, and feedback stages. The chain order can be rearranged
between Drift, Bloom, Haze, and Fracture modes.

The first version keeps the spectral and time modules portable and
dependency-free while leaving room for a deeper spectral backend later.

## Controls

The six chain blocks are vertical module controls arranged in the same
left-to-right order as the audio path. Feedback, Mix, and Output use conventional
sliders. The labelled Mix/Feedback XY pad provides an alternate performance
control, and Control-click restores defaults.

Continuous controls are smoothed to reduce crackling during rapid movement.
Changing the chain briefly crossfades through the dry signal to avoid a topology
switch click, and the host's plugin-window bypass maps to Ambo's click-smoothed
bypass parameter.

## Feedback-driven revision

Special thanks to [u/ChapelHeel66 on Reddit](https://www.reddit.com/user/ChapelHeel66/)
for testing Ambo in Studio One 7 and providing detailed usability and audio
feedback.

That feedback led to the chain blocks becoming the module controls, larger and
reachable endpoint hit areas, Control-click reset, clearer XY labelling, removal
of misleading duplicate wet/return displays, corrected activity semantics,
parameter smoothing, click-safe chain changes, and host bypass support.
