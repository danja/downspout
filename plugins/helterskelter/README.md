# HelterSkelter

`helterskelter.vst3` — automatic wah pedal.

A resonant lowpass whose cutoff is played by the music itself: the input
envelope opens the filter like a touch-wah, and/or a BBT-synced ADSR cycle
sweeps it in time — two beats low followed by two beats high is Division 4
with Gate 2 and Invert on. Blend mode takes whichever source opens further.

## Using it

**Sources.** Mode selects Envelope (touch-wah), BBT ADSR (tempo wah), or Blend
(max of both). Sensitivity drives the envelope follower; Depth sets the sweep
range from Base Freq up to four octaves above it; Resonance (Q 0.5–12) sets
how vocal the peak gets.

**The BBT cycle.** Division sets the cycle length (1, 2, 4 or 8 beats); the
ADSR gate stays high for Gate Beats, then releases for the rest of the cycle.
Invert flips the gate, giving low-then-high patterns. With the transport
stopped the ADSR releases to silence and the filter sits on the envelope path.
Beats are counted as quarters (4/4 assumption — see docs/design.md).

| Parameter | Range | Default | Notes |
|-----------|-------|---------|-------|
| Mode | Env, BBT, Blend | Env | Cutoff source |
| Sensitivity | 0–100 % | 60 % | Envelope drive; CC 1 |
| Depth | 0–100 % | 70 % | Sweep range; CC 2 |
| Resonance | Q 0.5–12 | 4 | Filter peak; CC 3 |
| Base Freq | 100–2000 Hz | 400 Hz | Pedal-down cutoff |
| Division | 1/2/4/8 beats | 4 beats | ADSR cycle length |
| Gate Beats | 0.5–8 | 2 | High beats per cycle (clamped to Division) |
| Attack | 1–500 ms | 20 ms | ADSR attack |
| Decay | 5–1000 ms | 150 ms | ADSR decay to Sustain |
| Sustain | 0–100 % | 70 % | ADSR sustain level |
| Release | 5–2000 ms | 200 ms | ADSR release |
| Invert | off/on | off | Flip the BBT gate |
| Mix | 0–100 % | 100 % | Dry/wet blend; CC 4 |
| Trim | −12 to +12 dB | 0 dB | Wet-only makeup gain |
| Bypass | active/bypass | active | DSP keeps running |
| CC Sens/Depth/Res/Mix | 0–127 | 1/2/3/4 | 0 = off; Drift lane defaults |
| CC Channel | 1–16 | 1 | MIDI channel for the four CCs |

Route Drift's MIDI output to the wah's MIDI input: CC 1 → Sensitivity, CC 2 →
Depth, CC 3 → Resonance, CC 4 → Mix.

## Building and testing

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_HELTERSKELTER=ON
cmake --build build --target downspout_helterskelter_core_tests helterskelter-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_helterskelter_core_tests --output-on-failure
```

Core tests cover silence, dry passthrough at Mix 0, bypass, determinism
(including block splits), envelope-driven brightening, BBT cycle breathing,
gate inversion, stopped-transport settling, CC overrides, clamping, and state
round trip.

## Status

Core DSP, deterministic tests, the VST3 target, the panel and the catalogue
screenshot are complete. Host validation in REAPER remains.
