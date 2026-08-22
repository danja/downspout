# syrinx — design notes

## Overview

syrinx is an original Downspout plugin. It implements the Mindlin-Laje avian
syrinx model — a two-dimensional nonlinear ODE — plus a vocal-tract filter chain
intended to produce convincing bird sounds. The reference production
implementation is Lyrebird (`/home/danny/github/Lyrebird`).

## Signal chain (per voice)

```
MIDI trigger
  └─ SyrinxVoice::trigger() — set ODE state, formant coefficients, calibrate air-sac ODE
       ↓
  Per sample:
    1. Envelope (linear attack, exponential release)
    2. Vibrato (sinusoidal FM)
    3. Pitch contour (linear log-sweep over first 0.5 s via bend parameter)
    4. AM gating (sinusoidal pressure modulation)
    5. Air-sac respiration ODE (2-state, Fainstein/Goller/Mindlin 2025) → ps
       ps = pressure when respiration=0 (no-op); ps physically shaped when >0
    6. Alpha: alpha = 0.05 + 0.40 * ps
    7. Beta: betaClean = clamp(1 / gammaScale², betaMin, 16) * roughFactor()
    8. Physics tracheal coupling (when tracheaCm > 0): pi(t) from y_/gamma_ of
       both sides via fractional-delay ring buffer → modifies alphaA, alphaB
    9. ODE integration (Euler, 16–64× oversampled), primary side with alphaA:
         dx = y;  dy = −α·γ² − β·γ²·x − γ²·x³ − γ·x²·y + γ²·x² − γ·x·y
   10. Source normalisation: y / gamma * kSourceGain / ampPP(alpha, beta)
   11. Air-sac source scaling: source *= √(ps/pressure) [Laje & Mindlin low-freq limit]
   12. Noise injection (turbulence, ps-gated)
   13. Optional second ODE at offset gamma with alphaB, mixed by coupling depth
   14. 180 Hz highpass (DC block / tracheal HP)
   15. f0-tracking bandpass (tract, Q from timbre)
       + optional 2·f0 harmonic band (harmonic_ controls band level only)
       + tracheal comb: 4-partial cascade (f1, 3f1, 5f1, 7f1)
   16. DC block + 12 kHz lowpass
   17. Output × envelope × level

Master: tanh soft saturation → distance reverb → gain
```

## ODE physics

The Mindlin-Laje model (Laje & Mindlin 2005):

```
ẋ = y
ẏ = −α·γ² − β·γ²·x − γ²·x³ − γ·x²·y + γ²·x² − γ·x·y
```

`gamma` is a time-scale parameter; the dimensionless mapping `f0 = gamma * PHI(alpha,
beta)` where `PHI(0.1, 1) = 0.17061` means `gamma` is chosen per syllable so that
`beta ≈ 1` puts the oscillation near the desired pitch. `gammaScale > 1` lowers beta
(via `betaClean = 1 / gammaScale²`) toward the saddle-node-on-limit-cycle regime, which
is spectrally richer.

`gammaScale` is now driven by the **Regime** parameter (`gammaScale = 1 + regime * 3`,
range 1–4), independent of the Harmonic band level. `betaMin = 0.002` (Lyrebird value)
allows access to the pulse regime; previously `betaMin = 0.15` locked to tonal.

## Air-sac respiration ODE

Two-state ODE (Fainstein, Goller & Mindlin 2025, Table S2) replacing the old cosmetic
0.8 Hz cosine on output gain:

```
ẋ = (−(1 + x²)·x − p + F₀·f(t)) / τ_x
ṗ = (−(1 + x²)·x − (1 + αP)·p + F₀·f(t)) / τ_p

where:
  x      = piston-like air-sac variable
  p      = syringeal pressure
  f(t)   = respiratory muscle command (= envelope)
  τ_x    = 0.25 s,  τ_p = 0.20 s
  F₀     = 35
  αP     = α_i = 0.05 (inspiration),  α_i / α_o = 0.125 (expiration)
```

The ODE is calibrated at `trigger()` by running 50 ms with f(t)=1 to find peak pressure
`psBase_`, then normalized. `ps = max(0, p) / psBase_ * pressure` modulates the ODE
physics multiplicatively. The source is additionally scaled by `√(ps/pressure)` per the
Laje & Mindlin low-frequency approximation for labial velocity.

## Physics bilateral coupling (tracheal pi feedback)

When `tracheaCm > 0`, the two syringeal sides share tracheal pressure pi(t) (Laje &
Mindlin 2005, eqs. 62–67):

```
pi(t) = β_inj · (√ps_a · y_a/γ_a + √ps_b · y_b/γ_b) − R · pi(t − 2L/c)
```

A fractional-delay ring buffer (`kPiRingLen = 64` samples) holds the round-trip delay
`2L/c` for tube length L. pi is computed from the previous sample's y_ values (explicit
Euler weak-coupling approximation) and fed back as alpha modification:
`alphaA = alphaB = alpha − kTrachAlphaSlope * pi`. Coupling coefficient β_inj = coupling_ * 0.5.

When `tracheaCm = 0` the old simple independent-ODE linear mix is used unchanged.

## Tracheal comb (fixed formant pair)

The avian trachea is a stopped quarter-wave resonator. Its resonances fall at odd
harmonics of a fundamental frequency fixed by the bird's anatomy:

```
f_n = (2n − 1) · c / (4 · L_eff),   c ≈ 344 m/s,  n = 1, 2, …
```

These are **fixed absolute frequencies** — independent of f0. That is the key property
that gives a species spectral identity across pitch: a wren at 3 kHz and the same wren
at 6 kHz have the same tracheal resonances colouring the sound.

Measurements from the literature:
- Eastern towhee, 45 mm trachea (Nelson, Beckers & Suthers 2005): ~2.0/5.5 kHz
  (predicted: 1.91/5.73 kHz)
- Northern cardinal (Riede, Suthers & Goller 2006): ~2/5/8/12 kHz (1:3:5:7)
- White-throated sparrow, 34–38 mm trachea (Riede & Suthers 2009): ~2.2/6.6 kHz

### Cascade vs parallel

Two bandpasses in **parallel** produce two additive peaks with no zeros between them.
Two bandpasses in **cascade** produce the same peaks but with antiresonances between
and outside them. Hollow, reedy, and nasal timbres live in those antiresonances.
Lyrebird's fixed-resonance section uses cascade for exactly this reason.

syrinx implements the tracheal comb as a 4-partial cascade:
```cpp
filtered += kFormantGain * formant4_.process(
                formant3_.process(
                    formant2_.process(
                        formant1_.process(hp))));
```

`formant1Hz_` sets f1; `formant2Hz_` defaults to 3·f1; `formant3Hz_` and
`formant4Hz_` are derived as 5·f1 and 7·f1 (clamped at 8 kHz). `formantQ_`
is fixed at 6.0 (soft tissue damping, per Riede et al. 2006).

### Preset f1 values

Preset formant Hz values are derived from the quarter-wave tube law. Approximate
effective trachea lengths used:

| Preset | L_eff | f1 | f2 = 3·f1 | f3 = 5·f1 | f4 = 7·f1 |
|--------|-------|----|-----------|-----------|-----------|
| Wren | ~1.8 cm | 4800 Hz | 8000 Hz (clipped) | — | — |
| Thrush | ~3.4 cm | 2500 Hz | 7500 Hz | 8000 Hz (clipped) | — |
| Warbler | ~2.0 cm | 4300 Hz | 8000 Hz (clipped) | — | — |
| Finch | ~2.5 cm | 3500 Hz | 8000 Hz (clipped) | — | — |
| Robin | ~2.2 cm | 3900 Hz | 8000 Hz (clipped) | — | — |
| Nightjar | ~3.4 cm | 2500 Hz | 7500 Hz | 8000 Hz (clipped) | — |
| Pigeon | ~6.1 cm | 1400 Hz | 4300 Hz | 7000 Hz | 8000 Hz (clipped) |
| Hummingbird | ~1.4 cm | 6000 Hz | 8000 Hz (clipped) | — | — |
| Starling | ~3.4 cm | 2500 Hz | 7500 Hz | 8000 Hz (clipped) | — |

**Prior presets (500–2800 Hz) were wrong**: those values fell inside the f0-tracking
band, making them redundant with the tract filter and providing no cross-pitch spectral
identity.

## Noise / spectral flatness

Real birdsong has median spectral flatness ~0.044 (Lyrebird harvest, 39 976 syllables).
At `noise = 0` syrinx produces flatness ~0.0005 — three orders of magnitude too pure.

`kNoiseGain = 6.0` was chosen empirically; Lyrebird uses `2 × SOURCE_GAIN = 0.70`.
The noise is pressure-gated (only flows above kNoisePsFloor = 0.05) and injected at the
source before the tract, so turbulence is shaped by the vocal-tract filters.

All presets now have at least `noise = 0.03` to break the unphysical flatness floor.

## Lyrebird constant verification

All syrinx physics constants are now confirmed against Lyrebird source
(`pipeline/src/lyrebird_pipeline/twovoice.py`, `syrinx-processor.js`):

| Constant | syrinx value | Lyrebird source | Reference |
|---|---|---|---|
| kTrachAlphaSlope | 0.40 | `ALPHA_SLOPE = 0.40`; `alpha - 0.40 * pi[i]` | twovoice.py:28, syrinx-processor.js:117 |
| kTrachRefl | 0.90 | `DEFAULT_REFL = 0.9` | twovoice.py:87 — "Table I's gamma_refl" |
| kRespTauX | 0.25 s | `tau_x=0.25` (TAU_X_MAX) | respiration.py:133 |
| kRespTauP | 0.20 s | `tau_p=0.2` (TAU_P_MAX) | respiration.py:133 |
| kRespF0 | 35.0 | `forcing=35.0` | respiration.py:133 |
| kRespAlphaI | 0.05 | `alpha_i=0.05` (ALPHA_I_MIN) | respiration.py:133 |
| kRespAlphaR | 0.125 | `alpha_rel=0.125` (ALPHA_REL_MAX) | respiration.py:133 |
| betaMin | 0.002 | `--beta-min 0.002` shipped table | 08-implementation.md §8.1a |

**TracheaCm for bilateral coupling** — Lyrebird's inventory uses `tracheaCm = 2.0`
for **all species** regardless of tracheal length (from `DEFAULT_TRACHEA_CM = 2.0`,
twovoice.py:86, "Table I's L"). The formant comb f1 encodes the acoustic resonance;
the bilateral coupling delay encodes the syringeal chamber path length, which is
anatomically shorter and more consistent across species (~2 cm, from Laje & Mindlin
2005 Table I). These are distinct physical quantities.

**betaInj mapping** — Lyrebird fits betaInj per syllable in range 0.02–0.20
(DEFAULT_BETA_INJ = 0.05). syrinx maps `coupling_` (0–1) to `betaInj = coupling_ * 0.5`,
so coupling = 0.10 → betaInj = 0.05 (Lyrebird default), coupling = 0.40 → betaInj = 0.20
(Lyrebird maximum in fitted data).

## What is not yet done (known gaps vs Lyrebird)

- **Physics-based preset calibration** — current presets are hand-tuned. Lyrebird
  measures tracheal f1 per species from MFCC means and inverts the ODE from real
  recordings to fit alpha/beta contours. A scripted calibration pipeline against real
  xeno-canto audio would close this gap properly.
- **Presets with respiration > 0 may need retuning** — Nightjar (0.25) and Pigeon (0.40)
  had respiration applied as a cosmetic output-gain cosine; the new air-sac ODE changes
  onset shape and should be retuned by ear.
- **TracheaCm defaults set to 2.0 for coupling presets** — Nightjar, Pigeon, Starling now
  use TracheaCm = 2.0 (Lyrebird DEFAULT_TRACHEA_CM, Laje & Mindlin 2005 Table I). These
  presets enable physics pi(t) coupling by default; ear-testing against the old simple-mix
  behavior is still needed.
