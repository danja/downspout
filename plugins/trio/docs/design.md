# Trio: design notes

Source read: `~/github/oxide-studio/crates/oxide-fx/src/multiband_dynamics.rs` (MIT/Apache-2.0).
The structure (LR4 split, per-band down and up thresholds, gain smoothing in dB) is kept; the
details were redesigned.

## What the source does (from reading the code; not run)

- **One envelope per band, shared by both channels, stepped twice per sample.** `self.low_band.process_sample(low_l, ...)`
  and then `process_sample(low_r, ...)` call the same `BandDynamics`, which owns a single `envelope_db`. So the right
  channel's gain is computed from an envelope the left channel just moved, each channel's level steers the other's
  gain (the stereo image shifts), and attack and release run at twice the rate (set times are halved).
- **An instantaneous detector.** `20 log10 |x|` of each sample, so on a sine the gain computer sees a rectified wave and
  the envelope settles short of the true peak.
- **The three bands do not sum flat.** `LP1 + HP1 (LP2 + HP2)`: the low band is not passed through the all-pass of the
  second crossover. Computed with the analog prototype at 250 and 2500 Hz the sum is out by at most 0.11 dB near
  326 Hz; the error grows as the crossovers move closer. (Measured with a throwaway probe, not kept.)
- Processes only the first 1024 frames of a buffer; crossovers fixed at 250 and 2500 Hz; the upward lift is capped at
  18 dB with no way to change it.

## What Trio does

- **Crossover:** two 4th-order Linkwitz-Riley splits, with the low band run through the high crossover's all-pass
  (LP + HP), so the sum is an exact all-pass: the response is flat (tests: within 0.06 dB at 250/2500, 500/650, 40/12000,
  1000/1250 Hz and at 44.1, 48 and 96 kHz). Double precision, adjustable, kept at least 1.25 x apart.
- **One detector per band, linked across channels:** `max(|L|, |R|)` of the band, then a peak-hold with a decay of
  about two periods of the band's lowest frequency (low band 8 / crossLow ms, mid 2 / crossLow, high 2 / crossHigh,
  clamped to 0.3-60 ms), so a sine reads as its true peak. The gain applies to both channels.
- **Gain computer:** `bandTargetDb()`: downward `-(L - Td)(1 - 1/Rd)` above the down threshold, upward
  `+min((Tu - L)(1 - 1/Ru), limit)` below the up threshold, nothing between. The up threshold is held at or below the down
  threshold. One definition, used by the audio and by the tests.
- **Ballistics:** one-pole smoothing in dB, attack when the target is lower than the envelope, release otherwise, both
  scaled by Time. Stepped once per sample.
- **Mix:** the dry path is the untouched input; Input drives only the bands. Amount 0 is bit-exact dry. Input, Output and
  Amount are interpolated across each block. A band that is off passes straight through (no trim) and drops its envelope.
- Meters: per-band gain (the envelope) and peak level, measured from the audio.

## Measured (the tests, 303 checks)

- Steady-state gain follows the formula in each band: -15 dB for a tone 10 dB peak at threshold -30 dB, 4:1 (low, mid, high),
  and +8.4 dB for the upward lift (tone at -50 dB, threshold -36 dB, 2.5:1), within 0.8 dB. The mid band is tested with wide
  crossovers: an LR4 split leaks about -40 dB into the neighbours, which moves a heavily reduced band by about 1 dB.
- Bands are independent: a heavy mid-band compressor leaves a 60 Hz and a 12 kHz tone alone (within 0.3 dB).
- Stereo link: a 40 dB channel imbalance is unchanged by the dynamics and the quiet channel is reduced as much as the loud one.
  (This is the test that the source would fail.)
- Ballistics: a 20 ms attack reaches 63 % in 17-23 ms, a 200 ms release in 160-240 ms, and Time x 2 doubles the attack. A
  twice-per-sample envelope would give half those times.
- Random parameters and a 3.0 overload at 44.1, 96 and 192 kHz: no NaN or inf, output bounded; NaN input is treated as silence.

## Not done

- Not listened to; no host validation (REAPER). The OTT-style defaults and the detector decay times are my choices.
- No look-ahead and no per-band listen of the unprocessed band.
- **Expander**: Up Ratio runs 0.2-10. Below 1 the lift slope `1 - 1/ratio` goes negative, so the same curve expands downward under the up threshold (0.5 = 2:1, 0.2 = 5:1), capped at 60 dB. Defaults were softened from the OTT set (4:1 from -24/-20/-18, 2.5:1 lift) to 2:1 from -16/-14/-12 with a 1.5:1 lift from -40/-38/-36.
- **Knee** (0-24 dB, default 0 = the source's hard knee) is a quadratic blend centred on each threshold, the same curve as Glue's: at the threshold it already applies `slope x knee / 8`, and it meets the straight line at `knee / 2` past it. The lift side uses the same curve around the up threshold (still held at or below the down threshold).
- **Solo** (Off/Low/Mid/High) outputs only that band after its dynamics and trim, times Output, with the dry path and Amount ignored, so you hear what that band is doing.
- **Sidechain** selects the sidechain input (3-4) for the detectors. It is split by its own copy of the crossover filters into the same three bands (so the crossovers move it too) and each band's detector reads its band. The Input trim does not scale it. With the sidechain selected but nothing connected, the detectors fall back to the input. Appended after the meters with Knee and Solo: parameters 38-40.
- No per-band meters of the input level beyond the panel's text.

## Added later (October 2026)

- **Drift CC 1-4** on a channel (default 1; 0 ignores MIDI) drive Amount, Time (logarithmic 0.1-10), Input (-12 to +24 dB) and Upward limit until the panel or the host
  moves the control; the panel does not follow the CC. The mapping, channel match and override logic are core functions with tests (321 checks in all); Input needs an
  explicit "active" flag because it can be negative. `cc_channel` is appended after the six meters (index 37) so no index moved.
- Upward lift raises the noise floor by up to the Upward limit; there is no gate.
