# Guardian design

## Signal chain

Input gain → DC blocker → look-ahead delay → limiter (attack + release) →
clipper (tanh waveshaper) → hard ceiling clamp → output.

Bypass short-circuits the entire chain and copies input to output with zero
processing.

## Input gain

Applied as a linear multiplier before the delay buffer. Allows deliberate
overdrive into the limiter (+24 dB max) or headroom reservation (−24 dB min).

## Limiter

Uses a peak detector with configurable attack and release smoothing:
- **Attack = 0 ms**: gain reduction is immediate (legacy behaviour).
- **Attack > 0 ms**: `attack_coeff = 1 − exp(−1 / (sr × attackMs × 0.001))`;
  gain slews toward the required attenuation over the chosen time, letting brief
  transients pass before full clamp engages.
- Release uses the same one-pole IIR model as before.

Look-ahead delay is allocated per sample-rate change. The reported latency
equals `lookaheadMs × sr / 1000` frames.

## Clipper

Applied after the limiter, before the hard ceiling clamp.

`shape = 0`: no clipping — signal passes through the limiter unchanged.

`shape > 0`: `y = tanh(x × k) / tanh(k)` where `k = 1 + shape × 19`
(range 1–20). At shape=1, tanh with k=20 approximates a hard clip but with a
very narrow soft knee. Output is then scaled back to the ceiling so that a
fully-limited signal at the ceiling remains at the ceiling.

## Transfer curve display

The UI draws a per-pixel transfer function curve — X=input dB, Y=output dB —
computed directly from parameters and status outputs. No audio ring buffer is
needed. The current true-peak level is overlaid as a yellow vertical marker.

## Diagnostics

The UI presents conventional gain-reduction and true-peak bars, overload and
silence lamps, recovered-fault count, selected look-ahead, and a dedicated
latched-diagnostics reset action.
