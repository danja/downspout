---
title: Infra
order: 226
bundle: infra.vst3
kind: Audio effect
category: processor
role: Beat-locked tremolo
screenshot: /assets/plugins/infra.png
capabilities: [audio input, audio output]
summary: A stereo VCA driven by a beat-locked LFO: a whole multiple of the host beat rate within 1-20 Hz, eight waveforms, and a 0/90/180/270 degree phase for each channel.
---

## Opinion

Not yet auditioned in a host. The defaults are a gentle, fast shimmer: a 10 Hz sine (at 120 BPM) swinging the gain between 0 and -3 dB, the same on both
channels. Give the two channels different phases (0 and 180 degrees) for an auto-pan feel, raise Range for a real tremolo, or pick Sample & hold for a
stuttering, beat-locked random gate.

## Functionality

The LFO runs at **Multiple** cycles per host beat. **Auto** (0) chooses the whole multiple whose rate is nearest to 10 Hz at the current tempo, and any
setting is moved into the range that keeps the rate between 1 and 20 Hz. The LFO is locked to the transport position, so it lines up with the beat on every
pass; with the transport stopped it free-runs. Each channel reads the waveform at its own **Phase** (0, 90, 180 or 270 degrees; the default 90 degrees puts the LFO
at its peak on every beat). The VCA falls from 0 dB at the LFO peak to **-Range** at its trough.

### Parameters

| Parameter | Range | Default |
|---|---|---|
| Waveform | Sine, Triangle, Ramp up, Ramp down, Square, Soft square, Sample & hold, Smooth random | Sine |
| Multiple | 0 (Auto), 1-40 cycles per beat | Auto |
| Phase Left / Right | 0, 90, 180, 270 degrees | 90 / 90 |
| Range | 0-40 dB | 3 dB (0 to -3 dB) |

### The panel

The scope shows two cycles of the chosen waveform for each channel, so the phase offset is visible, scaled from 0 dB at the top to -Range at the
bottom. The header of the scope shows the gain each channel has right now, and the Rate panel shows the multiple and the rate in Hz in use.

### Status

Core covered by tests (70 checks): the multiple choice against a brute-force search from 20 to 600 BPM, the rate staying within 1-20 Hz, every waveform, the 0 to -3 dB
default swing and the Range across its span, the measured LFO rate, the phase selectors, every control having an effect, transport lock and block-size independence,
a stopped or tempo-less transport, and bad input. Not yet listened to or validated in a host, and the BBT-to-beat mapping is untested against a real host transport.
