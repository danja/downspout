---
title: Glue
order: 219
bundle: glue.vst3
kind: Audio effect
category: processor
role: Bus compressor
screenshot: /assets/plugins/glue.png
capabilities: [audio input, audio output]
summary: A stereo-linked bus compressor with a soft knee, sidechain high-pass, peak or RMS detector, program-dependent auto release, soft clip and parallel mix, showing its transfer curve and gain reduction.
---

## Opinion

Not yet auditioned in a host. Downspout had gates, a ducker and a limiter but no general compressor, so this fills
that gap: put it on a drum or mix bus with the defaults (4:1, soft knee, 60 Hz sidechain filter) and bring the
threshold down until the gain-reduction history moves with the groove. It is a redesign, not a copy of the Oxide
Studio compressor that inspired it, which has a hard knee, no sidechain filter and a soft clipper that is
discontinuous and on by default.

## Functionality

The detector takes the input, optionally high-passes it (so the bass does not pump the mix), measures peak or RMS
level in each channel and uses the louder one, so the reduction is linked and the stereo image does not move. The
gain computer is a textbook soft knee; the gain is smoothed in dB with separate attack and release.

### Parameters

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Threshold | -60 to 0 dB | -18 dB | |
| Ratio | 1.1 to 20 : 1 | 4 : 1 | |
| Knee | 0 to 24 dB | 6 dB | Soft knee centred on the threshold |
| Range | 1 to 40 dB | 40 dB | Caps the reduction |
| Attack | 0.1 to 100 ms | 10 ms | |
| Release | 20 to 2000 ms | 200 ms | Ignored while Release mode is Auto |
| Release mode | Fixed / Auto | Fixed | Auto is short after a transient and long once compression has been sustained |
| Sidechain HPF | 20 to 400 Hz (20 = off) | 60 Hz | Detector only; the audio is not filtered |
| Detector | Peak / RMS | Peak | RMS uses a 10 ms window |
| Makeup | 0 to 24 dB | 0 dB | |
| Soft clip | Off / On | Off | Linear below -6 dBFS; never exceeds 1.0 |
| Mix | 0 to 100 % | 100 % | Parallel compression |

### Auto release

In Auto mode the release time runs from about 40 ms to 800 ms depending on how long the compression has been
held. Measured: 63 % recovery in 196 ms after a 50 ms burst and 644 ms after 5 s of compression. The Release knob
is ignored while Auto is on.

### The panel

The transfer curve is the static response (before makeup); the white dot is the current input level and
reduction, and lags the curve during transients. The history strip shows reduction hanging down from 0 dB. Both
read gain reduction and level that the plugin measures from the audio.

### Status

Core covered by tests (static curve on audio, soft knee, range cap, attack and release time constants, auto
release, sidechain filter, stereo link, RMS against peak, mix, makeup, soft clip shape, meters, every control,
stability and overload at 44.1 to 192 kHz, block-size independence, denormals, and the parameter table against the
core). Not yet listened to or validated in a host. No look-ahead, so it is not a limiter, and no external sidechain.
