---
title: Prism
order: 230
bundle: prism.vst3
kind: Audio effect
category: processor
group: space
role: Banded spectral delay
screenshot: /assets/plugins/prism.png
capabilities: [audio input, audio output, host tempo sync, reported latency]
summary: Splits the sound into 1-8 frequency bands and delays each one separately on the left and right, in milliseconds or note divisions of the host tempo, with its own feedback. Per-band solo, low and high cut, four FFT sizes.
---

## Opinion

An echo that does not treat the spectrum as one thing. With the defaults the lows arrive
first and the highs trail behind by nearly half a second, so a snare or a chord smears upward
into a shimmer; flip it (long lows, short highs) and the same material blooms from the top
down. The right channel is offset from the left, so the smear also spreads across the field.

It is most at home on sustained or rhythmic material: pads, drum loops, a full mix on a send.
**Feedback** turns each band into its own repeating tail; with a few bands at different times
you get interlocking, slowly decaying patterns that a normal delay cannot make. The **delay by
frequency** plot on the panel shows the shape you have made. **Solo** a band to hear what it is
contributing.

It is a creative effect, not a precise one: delays between band centres are blended, not exact,
and repeats lose a little level at some delay times.

## Functionality

An overlapping short-time Fourier transform (square-root Hann windows, 8x overlap) runs on each
channel. Every frequency bin is read back from the spectrum stored some whole number of frames
ago; the remainder, under half a hop, is applied as a phase rotation and corrected for the level
it costs through the windows. A bin's delay and feedback come from the band values, interpolated
linearly in log frequency between band centres (31.25 Hz to 16 kHz, evenly spaced in octaves),
so there is no step at a band edge. Band values are smoothed over about 100 ms so turning a
control does not click.

The stored spectrum is the input plus the feedback times the delayed spectrum, so each repeat is
late by the full delay. Bins with no whole-frame delay get no feedback (it would be a zero-delay
loop). Solo and the rotation correction affect only what is heard, never the loop.

**Tempo sync.** Each side (left, right) has an **ms / RATE** switch. In RATE mode every band
picks a note division (1/64 to 1/1, with triplets and dotted values) of the host tempo (the BBT
tempo; 120 BPM if the host gives none), so the echoes sit on the beat at any tempo, with a
different division per band for polyrhythmic smears. A division longer than 2 s at the current
tempo is limited to 2 s and the panel says so. Tempo changes glide rather than click.

**Low Cut** and **High Cut** fade the delay and feedback out over a third of an octave and leave
everything beyond them undelayed; each is off at the end of its range. **Link L/R** makes the
right delays follow the left. **Mix** is dry against delayed; while a band is soloed only the
delayed sound is heard.

The dry path is delayed to match, and the latency (the FFT size: 11-85 ms at 48 kHz) is reported
to the host and shown on the panel. Changing **Resolution** clears the delay memory.

### Parameters

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Bands | 1-8 | 8 | Bands beyond it are kept but unused |
| Resolution | 512, 1024, 2048, 4096 | 2048 | FFT size, sets latency |
| Low Cut | 20 Hz-2 kHz | off (20 Hz) | |
| High Cut | 200 Hz-20 kHz | off (20 kHz) | |
| Mix | 0-100 % | 50 % | |
| Link L/R | Off, On | Off | Right follows left, including sync and division |
| Sync L, Sync R | ms, RATE | ms | RATE: bands use their note divisions |
| Band n L Delay | 0-2000 ms | 60 ms x (n-1) | |
| Band n R Delay | 0-2000 ms | 60 ms x (n-1) + 30 | |
| Band n L / R Division | 1/64 ... 1/1 (13) | 1/8 / 1/8D | Used while that side is RATE |
| Band n Feedback | 0-95 % | 30 % | |
| Band n Solo | Off, On | Off | |

### Origin

The technique (per-bin delay in the STFT domain with band interpolation, fractional delay by
phase rotation, feedback through the stored spectra) follows the published description of
the [Multiway](https://github.com/mustafamazi/Multiway) plugin by Mustafa Mazi. It was reimplemented from that description; no Multiway
code was used, as Multiway is AGPLv3 and Downspout is MIT. Prism has no LFOs or drawn shapes.

### Status

Core DSP, about 6 900 deterministic checks, the VST3 target and the catalogue screenshot are
complete. It has not been auditioned and has not been run in a host, so latency reporting, host
tempo handling and resolution switching under automation are unverified outside the test rig.
