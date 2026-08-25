---
title: Bassops
order: 35
bundle: bassops.vst3
kind: Audio effect
role: Sidechain ducker / stereo synthesiser
screenshot: /assets/plugins/bassops.png
summary: Four-input sidechain ducker with optional harmonic stereo synthesis. A control signal (typically a kick-drum send) ducks the main stereo signal; the ducked result is summed to mono and split by a complementary LP/HP FIR pair. Distorting the HP path before decoding generates stereo width above the crossover from harmonic content.
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
to mono. A complementary LP/HP linear-phase FIR pair then splits that mono signal
at a shared M/S Cutoff frequency:

- The LP output carries the clean bass centre component below the cutoff.
- An optional waveshaper distorts the mono signal before the HP convolution.
  The distorted HP output becomes the stereo-difference component after M/S decoding,
  creating stereo width above the cutoff from the added harmonics.

At Side Shape 0 % the waveshaper is bypassed; LP and HP reconstruct a phase-coherent
mono signal so both output channels are identical. Increasing Side Shape progressively
drives the HP path into saturation and hard-clipping, producing increasing stereo
width above the cutoff. The bass content below the cutoff is always clean.

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
