---
title: Guardian
order: 119
bundle: guardian.vst3
kind: Audio effect
category: processor
group: dynamics
role: Output safety
screenshot: /assets/plugins/guardian.png
capabilities: [audio input, audio output, bypass, input gain, clip interpolation, auto drive, attack/release, variable clipper, reported latency, protection density meter, input-peak marker]
summary: Input-stage clip interpolation, look-ahead limiting with attack/release, 4x inter-sample peak guard, optional auto drive to a target reduction, variable clipping, DC removal and bypass.
---

## Opinion

Still testing, but the input interpolater makes hot mixes noticeably smoother
than flat-topping the ceiling. Use the protection-density meter to dial input
gain: a few percent means you are just catching peaks; a sustained high reading
means the limiter is doing the mix.

## Functionality

Guardian is intended for the end of autonomous graphs. With **Clip
interpolation** on (the default), runs of input samples above the ceiling are
rebuilt as a straight line between the last clean sample before the run and the
first clean sample after it; every other sample is untouched by that stage.
The stage adds a fixed 5 ms delay, reported to the host together with the
user's look-ahead.

The limiter then catches what remains, with attack, release and an optional
program-dependent faster recovery. **Auto drive** slowly trims the input to
hold a chosen peak reduction, and the 4x true-peak guard watches for
inter-sample overshoot. **Clipper shape** sweeps from limiter-only to near-hard
clip, with a transfer-curve graph. Bypass and a live protection-density readout
complete the panel. No MIDI. Place last.
