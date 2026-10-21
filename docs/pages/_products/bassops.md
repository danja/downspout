---
title: Bassops
order: 35
bundle: bassops.vst3
kind: Audio effect
category: processor
role: Sidechain ducker / stereo synthesiser
screenshot: /assets/plugins/bassops.png
summary: Four-input sidechain ducker with optional harmonic stereo synthesis. A control signal (typically a kick-drum send) ducks the main stereo signal; the ducked result is summed to mono and the harmonics added by a waveshaper, high-passed at the cutoff, are decoded as a side signal that generates stereo width above it. The bass stays clean mono.
---

## Opinion

Still new, I haven't tested much. But it basically works, should be useful.

## Functionality

Bassops combines two processing stages into a single plugin:

**Sidechain ducker** — the signal on inputs 3 and 4 feeds an envelope follower
whose output controls a VCA over the main signal on inputs 1 and 2. Attack and
release shape how fast the duck opens and closes. Duck Depth sets the maximum
gain reduction at full sidechain amplitude.

**Mono split and stereo synthesis** — after ducking, the stereo signal is summed
to mono (a mono track that only feeds one main input is treated as dual mono, so it
is not 6 dB down). The delayed full-range mono is the centre of both output channels.

- An optional waveshaper distorts the mono signal. The harmonics it adds, high-passed
  by a linear-phase FIR at the M/S Cutoff, become the stereo-difference component
  after M/S decoding, creating stereo width above the cutoff.

At Side Shape 0 % there is no side component, so both output channels are identical
copies of the mono signal at unity gain. Increasing Side Shape progressively drives
the shaper into saturation and hard-clipping, producing increasing stereo width above
the cutoff. The bass is always the clean mono signal. The cutoff is soft at low
settings (129 taps), so it sets where the added harmonics start rather than a sharp
crossover.

A Wet control blends the processed signal against the latency-compensated dry
stereo input. This is the only route to preserve the original stereo content of
the source; the wet path uses a mono sum and does not retain the input's
stereo difference.

The right-hand panel shows four live meters — Input, Sidechain, Duck Gain, and
Output — making the ducking action immediately visible.

### Parameters

| Parameter    | Range          | Default | Notes                                                              |
|--------------|----------------|---------|---------------------------------------------------------------------|
| Duck Depth   | 0–100 %        | 80 %    | 0 = bypass ducker; 100 = full silence on sidechain hit             |
| Attack       | 1–500 ms       | 10 ms   | Envelope follower rise time                                        |
| Release      | 10–2000 ms     | 100 ms  | Envelope follower fall time                                        |
| M/S Cutoff   | 50–5000 Hz     | 200 Hz  | LP/HP crossover; typical 80–300 Hz                                 |
| Side Shape   | 0–100 %        | 0 %     | Distortion on the HP path; 0 = mono output, 100 = hard clip ×8 drive |
| Wet          | 0–100 %        | 100 %   | Wet/dry blend; dry is latency-compensated original stereo          |

### Routing

- **Inputs 1–2** Main stereo signal to be processed.
- **Inputs 3–4** Sidechain control signal (e.g. a dry kick-drum send).
- **Outputs 1–2** Ducked, processed stereo output.

Use a **dry** kick send on inputs 3–4 rather than a processed bus. Applying
heavy compression or limiting to the sidechain before it arrives will flatten
the envelope follower's response and reduce ducking clarity.

### Latency

The FIR pair uses 128 taps (Hann-windowed sinc). This introduces **64 samples**
of latency, which is reported to the host so that automatic delay compensation
keeps the track in time with other channels. The dry delay line matches this
latency so wet and dry signals are time-aligned at any wet/dry ratio.

### Status

Core DSP — envelope follower, FIR filter pair, waveshaper, and wet/dry mix — is
tested. The UI is functional with live metering. The FIR coefficients are
recomputed per-block on cutoff changes; rapid parameter automation may produce
brief tonal artefacts at the crossover during the transition.
