# spliff — design notes

INBOX brief: *"spliff will be clone of spiff following downspout conventions,
see docs/reference/spiff manual.html."*

The manual publishes semantics but almost no numeric ranges, defaults, or DSP
internals (no Hz/dB/ms values except Mix 0–100 % and qualitative guidance), so
every number below is a downspout-side choice, kept traceable to the manual
section it implements.

## Structure

```
include/spliff_core.hpp   Parameters, EngineState (incl. DesignerState), text state
include/spliff_spectrum.hpp  portable 64-band analyser API
src/spliff_core.cpp       3-band Cut/Boost + threshold-free Designer, state I/O
src/spliff_spectrum.cpp   Hann-windowed FFT and peak-hold bands
src/dpf/SpliffPlugin.cpp  state-driven wrapper, Drift CC 1-4 overrides, spectrum outputs
src/dpf/SpliffUI.cpp      PROCESS/DRIFT CC tabs, analyser, shorter control rows
```

## Manual mapping (§-references to `docs/reference/spiff manual.html`)

- **Mode cut/boost (§3.1):** `mode` 0/1. A third choice, 2, is the separate
  level-independent Designer described below; it does not alter either Spiff
  mode.
- **Depth (§3.2):** `depth` 0–1. Cut targets `−30·depth·w` dB; boost targets
  `+18·depth·w` dB. Both bite in ~0.1 ms so short transients hear the full
  setting; gain returns toward unity with the Decay-controlled release.
- **Sensitivity (§3.2):** `sensitivity` 0–1 → fast/slow-ratio threshold
  `2.0 − 1.8·sens`, normalised as `w = (ratio − thr)/1.5`.
- **Sharpness (§3.2):** `sharpness` 0–1 blends per-band weights with their mean
  — low is the manual's "natural on drums", high the "harmonic/wide" setting.
  The default is 0.7 so the split-driven band differences are audible out of
  the box.
- **Decay (§3.2):** `decay` 0–1 → recovery τ 5 ms…300 ms exponential.
  0 is onset-only; the manual's "<5 most natural" sits near the default 0.25.
  The detector's reference floor attacks in 2 ms and releases in 30 ms, short
  enough that Decay genuinely controls the tail rather than being masked by the
  reference envelope.
- **Decay LF/HF (§3.2):** `decayTilt` −1…1 scales τ per band
  (`lo ×(1−tilt)`, `hi ×(1+tilt)`); 0 is the manual's centre detent.
- **Sensitivity curve / 5 bands (§4):** reduced to **two crossover frequencies**
  (`splitLow` 250 Hz, `splitHigh` 4 kHz). The curve is sidechain weighting, not
  EQ: the low/mid/high detectors are scaled 0.35 / 1.0 / 0.45, so moving a
  crossover moves signal between differently weighted detectors and audibly
  changes the transient shape. Each crossover uses two cascaded one-poles while
  remaining complementary. Parametric bands are deferred.
- **Stereo link/balance (§3.3):** fixed at full link — one mono detector drives
  both channels, so transients never shift the image. M/S is deferred.
- **Mix/trim/delta/bypass (§3.4):** all four implemented; delta is
  `(wet − dry) × mix`, bypass passes dry while the DSP keeps running.
- **Advanced: resolution/oversample/window/phase (§3.5):** deferred. The fixed
  detector runs at ~0.2 ms attack / 10 ms–250 ms reference envelopes, the
  generic middle of the manual's options.
- **Toolbar/presets (§5):** out of scope for the DSP clone.

## Designer mode (Mode 2)

A third mode implements the requested LYNX *Strike Shaper* / SPL Transient
Designer behaviour without vendoring upstream code. It bypasses the 3-band
Spiff path entirely and uses one shared mono envelope pair:

- `fast`: 0.2 ms attack / 30 ms release — onsets.
- `slow`: 20 ms attack / 250 ms release — body and tails.
- `ratioDb = 20·log10((fast+ε)/(slow+ε))`, where `ε` is relative to the larger
  envelope (`1e-12·scale`, floored only at the denormal limit).
- `drive = tanh(|ratioDb|/9 dB)`: a smooth saturator with **no threshold** —
  every non-zero ratio moves the gain, and the result cannot exceed the knob.
- `targetDb = attackDb·drive` when `ratioDb >= 0`, else `sustainDb·drive`.
- `attackDb ∈ [−15, 15]`, `sustainDb ∈ [−24, 24]`, both defaulting to 0 dB.
- Gain is smoothed over 0.5 ms; Mix, Trim, Bypass and Delta still apply.

The gain decision is the **dB difference of two envelopes**, never their
absolute magnitude. Multiplying the input by any scale multiplies both
envelopes by the same factor, so `ratioDb` and the resulting gain are unchanged.
This is the defining level-independent behaviour and is tested at 1.0 and
1e-10 amplitude. The relative epsilon keeps that property right down to the
numerical floor; the `fast`/`slow` flush is only a denormal guard.

State keys `attack_db` and `sustain_db` are appended, and `mode` now clamps to
0–2. CC 1–3 still override Depth/Sensitivity/Decay in Cut/Boost; Designer uses
its panel Attack/Sustain (CC 4 Mix still applies). The UI swaps its nine Spiff
rows for the two Designer rows and keeps all parameter indices stable.

## Assumptions

**Two-pole crossover split.** Each crossover is a cascade of two one-poles:
`lo = LP₂(low)`, `hi = x − LP₂(high)`, `mid = LP₂(high) − LP₂(low)`.
Recombination is still algebraically exact, so Depth 0 is bit-near transparent
(asserted in tests). The steeper split plus the fixed 0.35 / 1.0 / 0.45 detector
weighting makes Split Low and Split High audibly effective. Phase distortion
between bands is accepted — Spiff's own filter topology is proprietary and the
manual gives no latency/phase targets to match.

**Detector envelopes.** Fast attack 0.2 ms with per-band release τ; slow
reference 2 ms attack / 30 ms release. The very first sample of a transient
passes before the gain reacts (no lookahead in v1 — same audibility trade the
manual's `window` control governs).

**Gain smoothing.** Gains move away from unity in ~0.1 ms for both Cut and
Boost, then return with the Decay-controlled τ. Previously Boost used the slow
release to rise, which made it almost inaudible on short transients.

**Denormals.** Gains snap to exactly 1.0 below 1e-7 so long quiet tails cost
nothing; outputs are `isfinite`-guarded per sample.

**Drift CCs.** CC 1–4 → Depth, Sensitivity, Decay, Mix (the four the manual's
workflow touches first); 0 disables. Follows chipper, not Spiff (which has no
MIDI at all) — the INBOX brief explicitly wants Drift correspondence.

## Analyser and layout

The 720×680 panel has two tabs. **PROCESS** puts a 64-band log-frequency
analyser in the upper half (20 Hz–20 kHz, Hann FFT, 30 Hz refresh, fast attack
and 40 dB/s fall). A segmented switch selects **Input**, **Output** or
**Delta** (`wet − dry`, captured before the Delta monitoring/bypass mix). The
lower half holds the mode selector, Bypass/Delta switches and the main controls
at half their former track length in two short columns. **DRIFT CC** owns all
five routing dropdowns and the explanatory copy.

The source is the state key `analyser_source` (0/1/2, default 1 = Output), not
an automatable parameter, so switching views never writes host automation.
The 19 original control parameter indices are unchanged; 64 read-only output
parameters (`spectrum_01`…`spectrum_64`) carry the band levels in dB. The
analyser is a Spliff-local copy of Octet's portable implementation so no shared
framework code changed.

## Visual acceptance

Panel follows the magneto color scheme: light test-equipment panel by default
with a LIGHT/DARK header toggle, theme tokens throughout, an amber PROCESS
strip, a cold-steel DRIFT CC strip and accent fills for sliders, segments,
switches and dropdown selections. The analyser accent changes with the source
(steel for Input, amber for Output, red for Delta).

The original redesigned panel capture was accepted and copied to
`docs/pages/assets/plugins/spliff.png` (720×680, verified with `identify`).
The demo spectrum was then raised from a −72 dB floor to a −22 dB programme
curve so the 64 bars are visible, and the PARAMETERS footer that overlapped the
last slider track was removed. A fresh 720×680 capture of that fix is in
`/tmp/opencode/spliff-shots/spliff.png`; pixel checks show populated bars and a
clear last slider row, but this agent cannot view PNG pixels, so catalog
acceptance is pending. Recapture with
`scripts/capture-plugin-screenshots.sh spliff` after any further UI change. The
four-octave reduction graph from the manual remains deferred — the analyser is
a spectrum display, not a live transfer-function plot.
