# helterskelter — design notes

Brief: automatic wah pedal. Cutoff from the envelope of incoming audio and/or
from divisions of the transport BBT through an ADSR (e.g. two beats low
followed by two beats high). Parameters controllable via MIDI CC matching
Drift's channels. Magneto look and feel.

## Structure

```
include/helterskelter_core.hpp   Parameters, EngineState, processBlock, text state
src/helterskelter_core.cpp       RBJ lowpass, envelope follower, BBT ADSR
src/dpf/HelterSkelterPlugin.cpp  state-driven wrapper, Drift CC 1-4 overrides
src/dpf/HelterSkelterUI.cpp      magneto-scheme panel (segments, switches, dropdowns)
```

The core depends on `generative_common.hpp` for `Transport` and
`absoluteQuarter` only — no MIDI is produced, so no `MidiBlock` plumbing.

## Signal path

`in → envelope follower ─┬─→ max/mode ─→ cutoff = base · 2^(mod·depth·4) → RBJ LP(Q) → trim → mix → out`
`BBT phase → gate → ADSR ─┘`

The two filters (one per channel) share one mono envelope and one ADSR
decision, so sweeps never shift the image. RBJ coefficients are smoothed at
~5 ms per sample to avoid zipper noise on fast sweeps; the first block snaps
them. Non-finite filter states clear to zero; denormals flush below 1e-12.

## Assumptions

**Beats are quarters.** The BBT cycle counts `absoluteQuarter()` directly, so
Division/Gate are exact in 4/4 and approximate elsewhere (a 6/8 beat is three
eighths, i.e. 0.75 quarters — the cycle still loops musically, just not on the
numbered beats). True meter-relative cycles are deferred.

**Gate semantics.** The gate is high for the first Gate Beats of each Division
cycle, low for the rest, retriggering on cycle wrap. Gate Beats clamps to the
cycle length (gate permanently high → sustain). Invert flips high/low, which is
how "two beats low then two beats high" is voiced: Division 4, Gate 2, Invert
on. With the transport stopped or invalid the gate releases and the BBT path
contributes nothing.

**Linear ADSR.** Attack rises linearly to 1, decay falls linearly to Sustain,
release falls linearly from the release-start level. Linear is deterministic,
cheap, and entirely adequate for a wah sweep — no exponential tail matching.

**Blend is max.** In Blend mode the larger of envelope and ADSR wins, so a
picked accent always punches through the tempo pattern and vice versa.

**Envelope scaling.** `norm = clamp(env · sens · 4)`: at default 0.6 a 0.4 peak
opens the filter fully, which suits guitar-level inputs; line-level material
backs Sensitivity off.

**Drift CCs.** CC 1–4 → Sensitivity, Depth, Resonance, Mix (the four playable
wah controls); 0 disables. Follows chipper/ghost/spliff, not any wah
tradition — the brief explicitly wants Drift correspondence.

## Visual acceptance

Panel follows the magneto color scheme (light default, toggle, amber/steel
strips, accent fills); recapture with
`scripts/capture-plugin-screenshots.sh helterskelter` after any UI change.
