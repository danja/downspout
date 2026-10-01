# Treatment — design notes

## What it models

A wall-mounted acoustic treatment panel: a facing sheet of some surface mass,
a cavity containing a porous fill, and an air gap between panel and wall.

```
  wall
   │
   │  air gap          g
   │
 ┌─┴─┐
 │   │  fill cavity    d      ← porous, flow resistance R
 └─┬─┘
   │  facing          mass m
   │
   ▼
 sound
```

## Derivation

Air in a gap of depth `D` between a moving surface and a rigid wall behaves as
a spring with stiffness per unit area `k = rho0 * c^2 / D`. Putting the facing
mass `m` on that spring gives the mass-air-mass resonance:

```
w0 = c * sqrt(rho0 / (m * D))
```

The facing and the gap are in series acoustically — the same air volume is
compressed either way — so `D = cavity + gap`. Adding either one lowers the
resonance, which is why a panel mounted further off the wall reaches lower
frequencies.

The fill's flow resistance, spread over the cavity depth, is the specific
resistance per unit area:

```
r = R * cavity
```

Surface impedance of the panel at normal incidence:

```
Z(w) = r + j(w * m - rho0 * c^2 / (w * D))
```

and the absorption coefficient follows from the standard backed-absorber
relation:

```
alpha(w) = 4 * rho0 * c * r / (|Z(w) + rho0 * c|^2)
```

At `w = w0` the imaginary part of `Z` vanishes, so `alpha` peaks there:

```
alpha_peak = 4 * r * rho0 * c / (r + rho0 * c)^2
```

That expression is maximal when `r = rho0 * c ≈ 413 Pa·s/m²`, giving
`alpha = 1`: total absorption at that frequency. Either side of it, `alpha`
falls off symmetrically. **This is the panel's optimum, and it is the reason
Flow Resist is not a "more damping" control.** Too open a fill lets the sound
through; too dense a fill reflects it back.

With the default 100 mm cavity, the match sits at `R ≈ 4130 Rayl/m`. The default
`R = 8000` gives `r = 800`, `alpha_peak ≈ 0.90`, and `Q ≈ 0.72` — a broad,
deep dip around 173 Hz, which is what a bass trap actually does.

### Damping and the fill's broadband behaviour

The panel resonance's Q follows from the same impedance:

```
Q = w0 * m / (r + rho0 * c)
```

A well-matched fill (large `r`) has *lower* Q than a badly matched one, so a
good panel absorbs a *wide* band and a poor one is narrow as well as shallow.
Q is clamped to `[0.7, 12]` so the parameter space stays usable.

Above the resonance the panel stops absorbing and a backed porous layer takes
over. The corner is where viscous and thermal penetration balance in the fill:

```
f_diffusion = R / (2 * pi * rho0 * cavity)
```

The shelf uses `kFillShare = 0.55` of the peak absorption rather than all of it,
because a backed fill never reaches `alpha = 1` at its own corner.

## Mapping to DSP

The audio path is the complement of absorption — what the panel does not absorb
passes through — realised as two biquads per channel:

1. **Peaking EQ** at `w0` with the model's own Q, gain `20*log10(1 - alpha)`.
   This is the panel's dip.
2. **High shelf** at `f_diffusion`, gain `-6 * kFillShare * alpha`. This is the
   fill's broadband contribution.

Both stages are cuts, so the plugin cannot boost the signal. That is asserted in
the tests rather than assumed, by evaluating the cascade magnitude response
across the parameter space.

`Amount` scales `alpha` itself rather than crossfading dry against wet. That
keeps Amount 0 exactly transparent (a bit-transparent filter, not a mix) and
keeps the response monotonic as you sweep it.

### Two coefficient bugs worth recording

Both were caught by the tests, and both produce a filter that looks plausible
while being badly wrong:

- The RBJ high shelf needs the `amp` factor on the numerator. Without it the
  shelf reaches half its requested gain at Nyquist.
- The RBJ high shelf flips the sign of the `(amp - 1) * cos` term between the
  `b` and `a` groups. With the same sign on both sides the shelf *boosts* the
  bottom end by up to +13 dB and cuts the top: exactly inverted.

`worstCascadeDb` in the tests sweeps cavity, gap, mass and resistivity over the
whole declared range and fails on any peak above 0 dB, so neither can come back.

## Portability

- `include/treatment_params.hpp` — parameters, ranges, defaults, the physical
  constants, and the CC map. No DSP.
- `include/treatment_core_types.hpp` — `Parameters`, `PanelState`, engine state.
- `src/treatment_engine.cpp` — the analytic model, clamping, Randomise, the two
  biquads, and text (de)serialisation.
- `src/dpf/` — the DPF wrapper and the NanoVG panel.

`analysePanel()` is pure and public, so the tests and the panel read exactly the
numbers the DSP uses rather than a second copy of the maths.

## Mapping to LV2 concepts

Treatment is a new plugin, not a port, so there is no LV2 precedent to map.
The VST3-specific decisions:

- **Parameters, not just state.** The panel property parameters are real
  automatable parameters, with the text state kept alongside for a stable
  save format. `Randomise` is a `kParameterIsTrigger` parameter so it is
  automatable and the *processor* decides what a new panel is — the UI never
  invents geometry.
- **MIDI in on a single port.** VST3 has one MIDI input, so the CC channel is a
  parameter rather than a port, matching `helterskelter` and `magneto`.
- **Output status parameters.** Resonance, peak absorption and diffusion corner
  are exposed read-only so the panel can show the processor's derived state
  rather than re-deriving it.
- **No transport.** Treatment is not transport-aware, so `WANT_TIMEPOS` is off.

## Serialisation contract

`version=1`, one `key=value` per line, newline-separated:

```
version=1
cavity=...
gap=...
mass=...
resist=...
amount=...
bypass=...
seed=...
```

Unknown keys are rejected (`std::nullopt`) so a malformed save cannot silently
load as a default panel. Older versions that omit a key get the struct default,
so adding a parameter later stays backward compatible.

## Test coverage

`tests/treatment_core_tests.cpp`, 48 assertions:

- **The model** — resonance matches the textbook formula; resonance falls with
  heavier facing and deeper cavity; `f ∝ 1/sqrt(D)` verified numerically; peak
  absorption reaches ~1 at the impedance match and falls off symmetrically
  either side; diffusion corner rises with resistivity and falls with depth; Q
  stays inside its clamp across the whole parameter space.
- **DSP** — silence in, silence out; Amount 0 and Bypass are dry; the panel
  attenuates at its resonance; Amount is monotonic; the fill absorbs above its
  diffusion corner; impedance match controls notch depth; the cascade never
  boosts, including across 60 randomised panels; determinism; block-split
  determinism; non-finite input stays contained.
- **Randomise** — deterministic from the seed, differs between seeds, every
  drawn value in range across 200 draws, Amount and Bypass preserved, a second
  press differs.
- **Robustness** — clamping of out-of-range and NaN parameters, and that broken
  parameters still produce a finite panel and finite audio.
- **State** — round-trip of all seven values, and garbage rejected.

## Outstanding

- Host validation in REAPER is pending.
- The panel has not been reviewed against `AGENTS.md`'s screenshot criteria; a
  catalog screenshot needs capturing first.
