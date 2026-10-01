---
title: Keyframe
order: 204
bundle: keyframe.vst3
kind: Audio effect
category: processor
role: Extrema-sampling time stretch
screenshot: /assets/plugins/keyframe.png
capabilities: [audio input, audio output, MIDI CC input]
summary: Time stretch after the DAFx26 extrema-sampling method — the input reduces to timestamped local extrema, and the spacing between them sets the overlap-add crossfade length, so splices shorten into transients and lengthen through sustained material with no transient detector anywhere.
---

## Opinion

The leash diagram is the whole instrument. Turn Time down and watch the two
playheads separate until the drift bar crosses the leash, and a splice fires to
pull them together. Long gaps in the keyframes mean long crossfades; a burst of
dense keyframes and the next splice arrives almost immediately. Nothing is
detecting the transient — the spacing is doing it.

## Functionality

Analysis reduces the input to timestamped local extrema with a bandlimited
B-spline derivative, a difference threshold and a subsample crossing estimate,
giving a sparse buffer where the distance between points encodes local
information density. Playback runs three playheads over it: a reference marking
where playback should be, an audio playhead at the pitch rate, and a temporary
playhead that crossfades in when the two main playheads drift more than K
keyframes apart. The crossfade spans exactly K keyframes, so its length follows
the signal.

Time and pitch are independent rates and the pitch ratio is Pitch divided by
Time. Time stops at 1.00x, because a live input cannot be read faster than it
arrives. Hold freezes the reference so a passage sustains indefinitely.

The paper analyses one signal; this analyses the mid channel so both channels
share one sparse time base and the stereo image survives a splice.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Time | 0.25–1.00x | 1.00x | Rate, not stretch factor. CC 1 |
| Pitch | 0.25–4.00x | 1.00x | Read rate, independent of Time. CC 2 |
| Splice | 2–64 keyframes | 16 | Leash length K; macro over splice duration |
| Threshold | −96 to −24 dB | −60 dB | Extremum floor; the paper's value |
| Max Splice | 20–500 ms | 250 ms | Ceiling on one crossfade |
| Hold | Off / On | Off | Freeze the reference for an indefinite sustain |
| Mix, Width, Output | 0–100 % | 60 / 60 / 75 % | Dry carries the latency. CC 7 on Output |

### Status

Core DSP, deterministic tests, the VST3 target and the panel are complete. Host
validation in REAPER remains.