# Mistakes log

New, unresolved entries only. Resolved lessons are promoted to `AGENTS.md` and removed from here.

Format: what happened, root cause, prevention.

## Open

## guardian: clip-ramp denominator used the remaining run only (2026-11-26)
- **What happened:** the interpolation test would have seen ramp values beyond the ceiling for the last samples of a clipped run; the formula was corrected before the test run.
- **Root cause:** the denominator was `exitOffset + 1` (samples remaining) instead of the whole run, `run + exitOffset + 1`.
- **Prevention:** derive the ramp from total run length, then assert every intermediate ramp point (not just the first) against the endpoints.

## guardian: program release affected attack, not recovery (2026-11-26)
- **What happened:** the first draft chose the fast release coefficient from the current limiting state, so it never applied while the gain was recovering after a peak.
- **Root cause:** attack and release directions were collapsed into one coefficient; recovery starts after `target` returns to 1.
- **Prevention:** branch on `target < gain` for attack and compare internal `gain` after a short transient for release; keep the hold counter through recovery.

## guardian: core tests forgot the look-ahead delay (2026-11-26)
- **What happened:** the single-sample input-gain and instant-attack assertions failed because the default 5 ms look-ahead delayed the first output.
- **Root cause:** the old tests explicitly zeroed look-ahead; the new fixture inherited the default.
- **Prevention:** derive single-sample fixtures from the parameters under test and set every delay-producing control explicitly.

## voxmod: bypass stopped muting during the three-way blend (2026-11-25)
- **What happened:** `testBypassIsTransparent` failed after adding Conv Mix; the bypassed plugin still passed the ring and vocoder engines.
- **Root cause:** the new `legacyAmount` was derived only from `1 - convAmount`, dropping the existing `bypassed ? 0 : ...` condition when the amounts were refactored.
- **Prevention:** derive every blend branch from the bypass flag in one place, and rerun the whole plugin suite (including bypass) after changing any amount expression.

## voxmod: direction test for commutative frame multiplication (2026-11-25)
- **What happened:** a new test asserted that swapping the source and IR pairs changes the convolution output; it failed because the outputs matched.
- **Root cause:** frequency-domain frame multiplication is commutative, so cross-synthesis has no source/IR direction; the test encoded an assumption from linear convolution with a fixed IR.
- **Prevention:** derive the algebra before writing an observable-effect test; prove the routing contract with silence on one pair instead of swapping symmetric operands.

## voxmod: limiter on the whole mix squashed the vocoder (2026-11-25)
- **What happened:** after adding a final-output peak limiter, `testFormantShiftChangesOutput` failed because both shift directions were clamped to the same 0.89 peak, and a normal vocoder patch sat at the limiter permanently.
- **Root cause:** the limiter was placed after all three engines, but only the convolution feedback loop needed stabilising; the legacy vocoder is intentionally hot and was being compressed at its default level.
- **Prevention:** scope stabilisers to the engine that needs them. Keep an unchanged legacy-behaviour test (formant/mix pass-through) as the guard when adding global output stages.

## voxmod: normalisation test kept the old delta-IR expectation (2026-11-25)
- **What happened:** after normalising the frame product by `kFft * kFrame`, the delta-IR frame test failed by a factor of 4096 until its expected value was re-derived.
- **Root cause:** the old expectation baked in the previous `0.5 / kFft` scale instead of the new inverse-transform algebra: a delta IR gives the windowed source divided by the frame length.
- **Prevention:** derive test expectations from the transform definition after changing a normalisation constant; render one probe point by hand before loosening tolerances.

## voxmod: ring oscillator phase used a hard-coded 48 kHz (2026-11-26)
- **What happened:** the ring carrier shifted pitch with the host sample rate and the new sample-rate test failed at 44.1/96 kHz.
- **Root cause:** `oscillatorSample()` divided frequency by a literal 48000 instead of the block's sample rate, so its phase advance and polyBLEP width were wrong off 48 kHz.
- **Prevention:** pass the sample rate into every phase-incrementing oscillator and pin the generated frequency across 44.1/48/96 kHz with a settled zero-crossing test.

## voxmod: routing audit relied on comments and a symmetric test (2026-11-26)
- **What happened:** the DSP, port metadata and UI agreed on 1/2 source and 3/4 carrier/IR, but `carrierSource` was commented as input 1/2, Sinevox's README described Voxmod's slots backwards, and the vocoder carrier test passed even with the pairs swapped.
- **Root cause:** routing was documented in prose rather than pinned by a distinguishable signal, and stale comments survived the earlier input swap.
- **Prevention:** add frequency-discriminating tests for vocoder carrier/source roles and stereo channel pairing; keep port comments, README cross-references and UI labels in the same audit whenever routing changes.

## Template

## <plugin/area>: <short description> (YYYY-MM-DD)
- **What happened:** observable failure and how it was found.
- **Root cause:** the specific assumption or missing check.
- **Prevention:** the test, build step or design rule that prevents a repeat.
