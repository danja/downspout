# spliff — design notes

INBOX brief: *"spliff will be clone of spiff following downspout conventions,
see docs/reference/spiff manual.html."*

The manual publishes semantics but almost no numeric ranges, defaults, or DSP
internals (no Hz/dB/ms values except Mix 0–100 % and qualitative guidance), so
every number below is a downspout-side choice, kept traceable to the manual
section it implements.

## Structure

```
include/spliff_core.hpp   Parameters, EngineState, processBlock, text state
src/spliff_core.cpp       3-band split, mono detector, per-band dynamic gain
src/dpf/SpliffPlugin.cpp  state-driven wrapper, Drift CC 1-4 overrides
src/dpf/SpliffUI.cpp      two-column NanoVG panel (parameters + CC routing)
```

## Manual mapping (§-references to `docs/reference/spiff manual.html`)

- **Mode cut/boost (§3.1):** `mode`. No other clone choice matters more.
- **Depth (§3.2):** `depth` 0–1. Cut target `1 − depth·w`; boost target
  `1 + 3·depth·w` (up to ~+12 dB).
- **Sensitivity (§3.2):** `sensitivity` 0–1 → fast/slow-ratio threshold
  `2.0 − 1.8·sens`, normalised as `w = (ratio − thr)/1.5`.
- **Sharpness (§3.2):** `sharpness` 0–1 blends per-band weights with their mean
  — low is the manual's "natural on drums", high the "harmonic/wide" setting.
- **Decay (§3.2):** `decay` 0–1 → recovery τ 5 ms…300 ms exponential.
  0 is onset-only; the manual's "<5 most natural" sits near the default 0.25.
- **Decay LF/HF (§3.2):** `decayTilt` −1…1 scales τ per band
  (`lo ×(1−tilt)`, `hi ×(1+tilt)`); 0 is the manual's centre detent.
- **Sensitivity curve / 5 bands (§4):** reduced to **two crossover frequencies**
  (`splitLow` 250 Hz, `splitHigh` 4 kHz). The curve is sidechain weighting, not
  EQ, and two splits give the same three-region steering (ignore lows, focus
  mids, ignore highs) without a node editor. Parametric bands are deferred.
- **Stereo link/balance (§3.3):** fixed at full link — one mono detector drives
  both channels, so transients never shift the image. M/S is deferred.
- **Mix/trim/delta/bypass (§3.4):** all four implemented; delta is
  `(wet − dry) × mix`, bypass passes dry while the DSP keeps running.
- **Advanced: resolution/oversample/window/phase (§3.5):** deferred. The fixed
  detector runs at ~0.2 ms attack / 10 ms–250 ms reference envelopes, the
  generic middle of the manual's options.
- **Toolbar/presets (§5):** out of scope for the DSP clone.

## Assumptions

**One-pole crossover split.** `lo = LP(low)`, `hi = x − LP(high)`,
`mid = LP(high) − LP(low)`; recombination is algebraically exact, so Depth 0
is bit-near transparent (asserted in tests). Phase distortion between bands is
accepted — Spiff's own filter topology is proprietary and the manual gives no
latency/phase targets to match.

**Detector envelopes.** Fast attack 0.2 ms with per-band release τ; slow
reference 10 ms / 250 ms. The very first sample of a transient passes before
the gain reacts (no lookahead in v1 — same audibility trade the manual's
`window` control governs).

**Gain smoothing asymmetry.** Gains clamp down in ~1 ms and release with τ, so
cuts bite on time and recover musically; boost uses the same path.

**Denormals.** Gains snap to exactly 1.0 below 1e-7 so long quiet tails cost
nothing; outputs are `isfinite`-guarded per sample.

**Drift CCs.** CC 1–4 → Depth, Sensitivity, Decay, Mix (the four the manual's
workflow touches first); 0 disables. Follows chipper, not Spiff (which has no
MIDI at all) — the INBOX brief explicitly wants Drift correspondence.

## Visual acceptance

Panel follows the chipper two-column layout with shared look-and-feel tokens.
The catalogue screenshot is captured and reviewed (mode label renamed from
"Mode Cut/Bst" after first review); recapture with
`scripts/capture-plugin-screenshots.sh spliff` after any UI change. A live
reduction curve (the manual's centrepiece graph) is deferred —
the UI is state-editors only in v1.
