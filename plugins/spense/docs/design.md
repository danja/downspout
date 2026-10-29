# Spense design notes

Port of `~/github/Dispenser` (MIT; JUCE 9 with AVX2 on x86 and NEON on ARM). Only the DSP is
ported; the JUCE processor, editor and look-and-feel are replaced by the portable core in
`include/spense_core.hpp`, a thin DPF wrapper and a MagnetoKit panel.

## Signal path

```
input -> Pre Gain -> Stack x cascade -> Post Gain -> optional clip at +-1 -> output
cascade = eight second-order all-pass stages in series
stage   = TPT state-variable filter, output = in - 2 k v1   (k = 1 / Q)
```

Each stage is the Cytomic trapezoidal SVF (`v3 = in - s2; v1 = a1 s1 + a2 v3; v2 = s2 + a2 s1 +
a3 v3`), whose all-pass is `in - 2 k bandpass`, i.e. `(s^2 - k s + 1) / (s^2 + k s + 1)`.

## What the source does that is not obvious

- **One-sample delay between stages.** Stage *i* reads stage *i-1*'s output from the previous
  sample (so the eight stages have no dependency inside a sample and run in eight SIMD lanes).
  A cascade therefore delays by 7 samples on top of its all-pass phase, and the source reports
  `stack x 7` as latency. Kept as is; the host is told the same figure.
- **`g = x (1 + x^2 / 3)`, x = pi f / sr.** A cubic stand-in for `tan(x)`. Close at the low
  frequencies that matter, and well short of `tan` near Nyquist. Kept, and the analysis uses it too.
- **Drift** moves stage *i* by `drift x table[i] x 6.37` octaves (left and right use different
  tables), through a degree-5 `2^x` polynomial. Stage frequency is clamped to 20 Hz - 0.49 sr.
- **Smoothing.** Frequency, Q, Drift and both gains glide linearly over 10 ms.

## Changes from the source

1. **Coefficients are computed once per sample, not once per cascade.** All eight cascades of the
   source computed identical coefficients from identical smoothers, 8x redundantly. Sharing them
   changes no sample (a test runs the source algorithm written out literally and compares), and
   makes stack 8 about 2.5 % of one core at 48 kHz (measured on a busy machine, so an upper bound).
2. **The first block does not fade in.** JUCE's `SmoothedValue` starts at zero, so the source ramps
   gain and frequency up from nothing over 10 ms after `prepare`. Spense snaps to the set values on
   the first block, like the other Downspout effects.
3. **Cascades that are switched off are cleared.** The source leaves a deactivated cascade's memory
   as it was, so raising Stack later plays stale state. Spense zeroes cascades above the Stack, so a
   cascade always starts clean. Lowering Stack cuts the dropped cascades' tails, as the source does.
4. **Gain bottom is exactly silence.** -100 dB (the bottom of the control) is gain 0, the
   `Decibels::decibelsToGain` behaviour the source relies on; the panel shows "-inf dB".
5. **Resonance is logarithmic** (0.1-10 Q, centred on 1) in the host parameter and on the panel; the
   source used a 0.7 skew. The range and default are unchanged.
6. **No mono bus.** Stereo in and out only (the source also offered mono); a missing right input is
   treated as the left.

Not ported: the JUCE editor, the AVX2/NEON intrinsics (the compiler vectorises the eight-stage
loop), the standalone build.

## The plot

`groupDelaySamples()` is the analytic group delay of the stack (the sum over stages and cascades),
not counting the fixed seven-sample delay per cascade:

```
nu = tan(w/2) / g          t = tan(w/2)          D = (1 - nu^2)^2 + k^2 nu^2
tau = k (1 + nu^2) (1 + t^2) / (g D)     per stage, in samples
```

At Q 0.707 `tau` is proportional to `(1 + nu^2) / (1 + nu^4)`, which peaks at `nu = 0.644`, so the
curve's maximum sits below the Frequency marker; by Q 5 it is within 3 % of it (tested). `phaseRadians()` gives
the matching phase (with the seven-sample delays) and the tests hold both against the measured
filter: phase within 0.04 rad, group delay within 4 % + 0.6 samples, across Q, stack and drift.

## Parameters and state

Seven ordinary automatable parameters (`stack`, `frequency`, `resonance`, `drift`, `pre_gain`,
`post_gain`, `clip`), saved by the host by symbol. No plugin state, no MIDI, no transport.
`activate()` clears filter memory only. The latency is `stack x 7` samples, set from the Stack
parameter and in `activate()`.

## Tests (`tests/spense_core_tests.cpp`)

The source algorithm written out literally (per-cascade coefficients) against the engine at three
sample rates and three stacks; unity gain at every frequency; measured phase and group delay against
the model; delay peak position at Q 0.707 and at higher Q; latency; every control has an effect, and
Drift's default is inert; gain bottom is silence, clip holds 0 dBFS; control changes glide; stability
at 44.1-192 kHz under random sweeps and overload; impulse response decays; block-size independence;
in-place and null-right-input processing; no denormals after a long silent decay; reset clears memory
only; lowering Stack clears the dropped cascades; no fade-in on the first block; parameter table against
the core.

## Not done

Listening and host validation (REAPER) remain. Drift and high-Stack plots were checked numerically but
only the default panel has been captured.
