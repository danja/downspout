# Glue: design notes

Source read: `~/github/oxide-studio/crates/oxide-fx/src/glue_compressor.rs` (MIT/Apache-2.0).
Unlike the ladder, nothing in it was worth porting as it stands, so Glue is a new design that
keeps the useful ideas: gain smoothing in the dB domain with separate attack and release, a
range cap, a soft clipper, parallel mix and a gain-reduction readout.

## What the source does (from reading the code; not run)

- A hard-knee, feed-forward peak compressor: `max(|L|, |R|)` converted with `20 log10`, a static
  gain computer `(1 - 1/R)(T - level)`, then one-pole smoothing in dB, attack when more reduction is
  wanted and release otherwise. The header's "exact SSL 4000 G-Bus" and "log sidechain detector"
  are just that.
- **No sidechain filter**, although the README lists "sidechain high-pass filtering".
- **The soft clipper is discontinuous.** For |x| <= 2/3 it returns x; just above 2/3 it returns
  `(3 - (2 - 3|x|)^2) / 3`, which is about 1.0 (0.667 jumps to 0.997). It is **on by default** and
  runs on the wet signal even when nothing is being compressed, so the default settings distort
  anything above about 0.67.
- It processes only the first 1024 frames of a buffer (`frame_count.min(1024)`); the rest is left alone.
- Auto release is a heuristic on a smoothed `|target - gain|` and is not tied to how long the
  compression has lasted.
- Ratio is a free float; the "2:1, 4:1, 10:1" and discrete attack/release lists are not enforced.

## What Glue does

- **Detector:** per-channel sidechain high-pass (Butterworth, 20-400 Hz, off at 20), then peak or RMS
  (10 ms mean square), then `max` of the two channels, so the reduction is linked.
- **Gain computer:** the textbook soft knee (Giannoulis et al.), capped by Range. `staticGainDb()` is the
  single definition: the audio uses it and the panel plots it.
- **Ballistics:** one-pole smoothing of the gain in dB, attack when the target is lower than the current
  gain, release when higher.
- **Auto release:** the release time is `40 + 760 * clamp(depth / 8 dB)` ms, where `depth` is a 400 ms
  average of the reduction the detector *demands* (the target). It must follow the target, not the smoothed
  gain: the first version followed the gain, which stays deep while it releases, so a slow release held the
  depth up and a short burst recovered almost as slowly as sustained compression (635 ms against 800 ms). Now a
  50 ms burst recovers (63 %) in 196 ms and 5 s of compression in 644 ms.
- **Soft clip:** `y = x` for |x| <= 0.5, else `0.5 + 0.5 tanh((|x| - 0.5) / 0.5)`. Continuous in value and slope,
  bounded by 1, off by default, and it does nothing to signals under -6 dBFS.
- Makeup and Mix are interpolated across each block; the gain follows the ballistics. Tiny recursive state is
  flushed once per block (denormals). A non-finite input sample is treated as silence.

## Measured (the tests)

- Steady-state gain on a sine matches the static curve to within 0.6 dB at -40, -25, -10 and -2 dB.
- Soft knee: 1.125 dB of reduction at the threshold for a 12 dB knee, exactly the formula; the curve is
  continuous and never rises with level; the audio agrees inside the knee.
- Attack and release: a step reaches 63 % in one time constant (10 ms attack within 1.5 ms; 200 ms release
  within 20 %).
- A 40 Hz tone through a 150 Hz sidechain filter is compressed over 6 dB less than 1 kHz; with the filter off
  both are compressed alike.
- Stereo link: a 20 dB channel imbalance is unchanged by compression, and the quiet channel is reduced as much
  as the loud one.
- RMS against peak on a sine: about 2.25 dB less reduction (a sine's RMS is 3 dB under its peak, times the slope).
- Random parameters and 3.0 overload at 44.1, 96 and 192 kHz: no NaN or inf, bounded. NaN input does not poison the
  output.
- CPU, measured on a busy machine (so an upper bound): about 0.4-0.55 % of one core for stereo at 48 kHz (-O2),
  with no extra cost through a long silent decay.

## Added later (October 2026)

- **External sidechain** (inputs 3-4, chosen by *Detector listens to*): the detector, with its high-pass and Peak or RMS, listens to the sidechain and the
  reduction is applied to inputs 1-2. If the host connects nothing the detector falls back to the input. Tests: a loud sidechain compresses a quiet input by
  the static curve for the sidechain's level (within 0.7 dB), a silent sidechain compresses nothing, internal mode ignores the sidechain, the high-pass acts on
  it, and the level meter reads it.
- **The level meter is the block peak** of the detector level, not the last sample's (which on a sine is anywhere between 0 and -6 dB and made the panel's dot
  flicker).
- **Drift CC 1-4** on a channel (default 1; 0 ignores MIDI) drive Threshold, Ratio (logarithmic), Makeup and Mix until the panel or the host moves the control.
  The mapping, channel match and override logic are core functions with tests; the panel does not follow the CC. Threshold overrides need an explicit "active"
  flag because the value is negative. `sidechain` and `cc_channel` are appended after the two meters (indices 14 and 15) so no index moved. 157 checks in all.

## Not done

- **Look-ahead** (0-10 ms, off by default, parameter 16) delays the audio path by whole samples while the detector sees the input undelayed, so the gain has been falling for that long when a peak arrives. The delay is reported to the host with `setLatency` when the control, the sample rate or activation changes. Changing it while playing moves the read point, which clicks once. Because the gain still follows the one-pole attack it is not peak-safe limiting; use Guardian after it.
- Not listened to; no host validation (REAPER).
- The 10 ms RMS window and the auto-release table (40-800 ms, 8 dB depth) are my choices, not measured against
  any hardware.
