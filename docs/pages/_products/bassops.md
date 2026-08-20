---
title: Bassops
order: 35
bundle: bassops.vst3
kind: Audio effect
role: Sidechain ducker / mid-side EQ
screenshot: /assets/plugins/bassops.png
summary: Four-input sidechain ducker with a linear-phase mid/side EQ. A control signal (typically a kick-drum send) ducks the main stereo signal; the ducked result is then split into mid and side components and filtered at a shared crossover frequency before being recombined.
---

## Functionality

Bassops combines two processing stages into a single plugin:

**Sidechain ducker** — the signal on inputs 3 and 4 feeds an envelope follower
whose output is inverted and applied as a VCA over the main signal on inputs 1
and 2. Attack and release controls shape how fast the duck opens and closes.
Duck Depth sets the maximum gain reduction at full sidechain amplitude.

**Mid/side EQ** — after ducking, the stereo signal is split into its mid (sum)
and side (difference) components. A linear-phase LP filter is applied to the mid
and a complementary HP filter is applied to the side; both share a single M/S
Cutoff control. The filtered components are recombined into stereo output.
Below the cutoff, only the mid channel carries signal, keeping bass locked to
the centre. Above the cutoff, only the side channel carries signal, preserving
stereo width in the highs.

The right-hand panel shows four live meters — Input, Sidechain, Gain Reduction,
and Output — making the ducking action immediately visible.

### Parameters

| Parameter    | Range          | Default | Notes                                                  |
|--------------|----------------|---------|--------------------------------------------------------|
| Duck Depth   | 0–100 %        | 80 %    | 0 = bypass ducker; 100 = full silence on sidechain hit |
| Attack       | 1–500 ms       | 10 ms   | Envelope follower rise time                            |
| Release      | 10–2000 ms     | 100 ms  | Envelope follower fall time                            |
| M/S Cutoff   | 50–5000 Hz     | 200 Hz  | LP (mid) / HP (side) crossover; typical 80–300 Hz      |

### Routing

- **Inputs 1–2** Main stereo signal to be processed.
- **Inputs 3–4** Sidechain control signal (e.g. a dry kick-drum send).
- **Outputs 1–2** Ducked and EQ'd stereo output.

Use a **dry** kick send on inputs 3–4 rather than a processed bus. Applying
heavy compression or limiting to the sidechain signal before it arrives will
flatten the envelope follower's response and reduce ducking clarity.

### Latency

The M/S EQ uses a 128-tap Hann-windowed sinc FIR. This introduces **64 samples**
of latency, which is reported to the host so that automatic delay compensation
can keep the track in time with other channels.

### Status

Initial implementation. Core DSP — envelope follower, FIR filter pair, and M/S
matrix — is tested. The UI is functional with live metering. The FIR coefficients
are recomputed per-block on cutoff changes; rapid parameter automation may
produce brief tonal artefacts at the crossover during the transition.
