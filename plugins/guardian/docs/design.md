# Guardian design

## Signal chain

Input gain → DC blocker → **clip interpolation** → look-ahead delay →
limiter (attack + program-dependent release) → clipper (tanh waveshaper) →
hard ceiling clamp → output.

Bypass short-circuits the entire chain and copies input to output with zero
processing. Interpolation is on by default and can be switched off.

## Input gain and auto drive

Manual input gain is ±24 dB and is applied before the input stage. With
**Auto drive** enabled, Guardian adds a slowly adjusted trim (clamped so the
combined gain stays inside ±24 dB) that aims for **Target reduction**. The
follower moves at most 8 dB/s when there is too much reduction and 3 dB/s when
there is too little; it is inactive during silence and resets on activation or
sample-rate change. The manual parameter value is never rewritten, so the host
still automates the user's setting.

## Clip interpolation

This stage runs after input gain and DC removal, before the limiter. A sample is
"clipped" when `|x| > ceiling`, where `ceiling` is the current `ceiling_db`
setting. Every other sample leaves the stage unchanged. When a run of clipped
samples ends, the whole run is replaced by a straight line between the clean
sample that entered it, `entry`, and the first clean sample after it, `exit`:

```
y[j] = entry + (exit - entry) * (j + 1) / (runLength + 1)
```

Because both endpoints are inside the ceiling, the rebuilt samples are too, so
the stage cannot introduce a new ceiling violation. The stage needs the first
clean sample after the run, so it delays its output by a fixed 5 ms window
(`interpFrames()`), independent of the user's **Look-ahead** setting. When the
host reports latency it reports `lookaheadFrames + interpFrames` while
interpolation is on. A clipped run longer than the window is restarted at the
ceiling so the ramp never reaches backwards into samples already emitted.

The limiter and clipper still process the stage's output; "untouched" means
untouched **by the interpolater**. Samples below the ceiling are not bit-for-bit
identical at the plugin output when the limiter, DC blocker or true-peak guard
changes gain.

## Limiter

A peak detector with configurable attack and release smoothing drives a shared
stereo gain. `attack = 0` means instant reduction; otherwise
`attack_coeff = 1 − exp(−1 / (sr × attackMs × 0.001))`. Release uses the same
one-pole IIR model. With **Program release** on, a short limiting run (under
3 ms) recovers at one quarter of the chosen release time (never below 10 ms);
long runs keep the chosen release, which avoids both pumping and slow pumping
recovery.

Look-ahead delay is allocated per sample-rate change. Its reported latency is
`lookaheadMs × sr / 1000` frames, plus the interpolation window above.

## True-peak guard

With the guard on, the detector also measures four-phase Catmull-Rom
interpolation of the most recent four samples (t = 0, 0.25, 0.5, 0.75) and uses
the largest magnitude. This catches inter-sample overshoot that the raw sample
peaks miss. It is an estimate from the sample history, not an oversampled
render of the final limiter output.

## Clipper

Applied after the limiter, before the hard ceiling clamp.

`shape = 0`: no saturation — the limiter output passes through unchanged.

`shape > 0`: `y = tanh(x × k) / tanh(k)` where `k = 1 + shape × 9` (range 1–10),
mapped into the ceiling. At shape=1 this approximates a hard clip with a narrow
soft knee. The unconditional final clamp guarantees the output never exceeds
the ceiling, even with slow attack.

## Transfer curve display

The UI draws a per-pixel transfer function curve — X=input dB, Y=output dB —
from the current input gain, ceiling and clipper shape. It models the
instantaneous limiter and clipper, not attack/release smoothing or the
interpolater. The live **Protection density** output shows the percentage of
samples in the current block touched by interpolation, limiting or clamping;
this is the one output parameter, needed because DPF's VST3 bridge has no
DSP-to-UI callback.

The former Safety Monitor read-only outputs (gain reduction, fault count,
overload, silence and peak) were removed: they were host-visible parameters
with no useful control function. Non-finite inputs are still sanitised, but
without a fault counter or latched status.
