# Spliff

`spliff.vst3` — adaptive transient processor, a downspout-style clone of
Oeksound Spiff (see `docs/reference/spiff manual.html`).

Unlike a conventional transient shaper (attack/sustain gain envelope), Spliff
detects transients per frequency band and applies dynamic cuts or boosts only
where transient energy exists, leaving non-transient material intact. Input
level barely matters: Depth behaves the same regardless of gain staging.

## Using it

**Cut or boost first.** Mode selects Cut (tame clicks, mouth noise, harsh
sticks) or Boost (bring drums, piano attacks forward). Spiff manual advice
holds: cut last in the chain, boost early before compressors and saturators.

**Workflow** (from the manual): Delta on, raise Depth, tune Sensitivity and
Decay, sculpt the band splits, optionally Sharpness and Decay LF/HF, then
Delta off, re-tune Depth, Bypass to compare, Mix to taste, Trim to compensate.

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Mode | Cut, Boost | Cut | Pink = boost, blue = cut in the manual's graph |
| Depth | 0–100 % | 50 % | Amount of cut/boost; CC 1 |
| Sensitivity | 0–100 % | 50 % | Higher catches more transients; CC 2 |
| Sharpness | 0–100 % | 30 % | Per-band independence; low = natural on drums |
| Decay | 0–100 % | 25 % | Recovery time; 0 = onset only; CC 3 |
| Decay LF/HF | LF … equal … HF | equal | Which end of the spectrum decays longer |
| Split Low / High | 20 Hz–12 kHz | 250 Hz / 4 kHz | Detector band crossovers (sidechain weighting, not EQ) |
| Mix | 0–100 % | 100 % | Dry/wet blend; CC 4 |
| Trim | −12 to +12 dB | 0 dB | Wet-only makeup gain |
| Bypass | active/bypassed | active | Soft in intent; DSP keeps running |
| Delta | off/on | off | Monitor wet−dry: what is removed or added |
| CC Depth/Sens/Decay/Mix | 0–127 | 1/2/3/4 | 0 = off; Drift lane defaults |
| CC Channel | 1–16 | 1 | MIDI channel for the four CCs |

Route Drift's MIDI output to Spliff's MIDI input: CC 1 → Depth, CC 2 →
Sensitivity, CC 3 → Decay, CC 4 → Mix.

**Stereo.** The detector is mono (fixed full link — transients never shift the
image); both channels share the decision and keep their own band content. M/S
operation from the manual is not implemented in v1.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_SPLIFF=ON
cmake --build build --target downspout_spliff_core_tests spliff-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_spliff_core_tests --output-on-failure
```

Core tests cover silence, dry passthrough at Mix 0, bypass, Depth-0
transparency, cut reduction and boost lift on an onset burst, the sensitivity
gate, decay-tail behaviour and tilt direction, Delta polarity, Trim gain,
block-split determinism, clamping, and state round trip.

## Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER remains. Deliberately
deferred to follow-ups: M/S stereo modes, parametric sensitivity-curve bands,
resolution / oversample / window / phase controls, and live gain-reduction
metering in the panel.
