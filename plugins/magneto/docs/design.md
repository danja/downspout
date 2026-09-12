# magneto — design notes

Source model: Stefano Baldan, Hélène Lachambre, Stefano Delle Monache, Patrick
Boussard, *Physically informed car engine sound synthesis for virtual and
augmented environments*, SIVE'15
([docs/reference/SIVE15_submission_4.pdf](../../../docs/reference/SIVE15_submission_4.pdf)).

The paper describes a procedural four-stroke engine: a phasor at `RPM/120` Hz
drives intake-valve, exhaust-valve, piston and ignition functions; each cylinder
is a digital waveguide whose feedback coefficients are modulated by its valves
and whose delay length is modulated by its piston; cylinders feed intake
collectors and extractors; extractors feed a straight pipe, a four-element
muffler and an outlet; a Poisson process adds backfires on overrun. Three
signals leave the model — intake, engine-block vibration, and the tailpipe.

`magneto` implements that model. This document records the parts the paper
leaves open and the choices made here, so behaviour stays traceable.

## Structure

```
include/magneto_core_types.hpp     Parameters, EngineState, TransportSnapshot
include/magneto_params.hpp         ParamSpec table, enum tables, CC map (shared DSP/UI)
include/magneto_engine.hpp         clampParameters / activate / processBlock
include/magneto_serialization.hpp  version=1 key=value text state
include/modules/DelayLine.hpp      power-of-two ring, linear and Hermite reads
include/modules/Waveguide.hpp      bidirectional pair, two-phase read/scatter/write
include/modules/CycleFunctions.hpp phasor, sine table, the four cycle functions
include/modules/Cylinder.hpp       chamber + intake runner + extractor per cylinder
include/modules/ExhaustSystem.hpp  straight pipe, four muffler elements, outlet
include/modules/Noise.hpp          LCG noise, one-pole, DC blocker, smoother
```

The core has no DPF dependency. `EngineState` is roughly 315 KB of fixed delay
memory and contains no heap allocation; it must never be a stack local, so the
DPF wrapper holds it as a plugin member and the tests build it with
`std::make_unique`.

## Assumptions where the paper is underspecified

**Ignition placement.** The paper's prose says the ignition impulse is "the
positive half of a sine wave, shifted at the beginning of the expansion phase",
but its printed formula places the pulse at `0 < x < t` — where the intake valve
is open, so the combustion impulse would vent straight through the intake, and
where the formula yields a full bipolar cycle rather than a positive half.
`fuelIgnition()` follows the prose: a positive half sine starting at `x = 0.5`,
of width `0.5 * t` so `t = 1` spans the whole power-plus-exhaust half.
`kIgnitionWindowStart` makes the alternative a one-line change.

**Chamber length from displacement.** The paper gives cylinder volume in cc but
no acoustic mapping. The chamber is modelled as a square cylinder (bore =
stroke): `V = 2*pi*r^3` with length `2r`, so `L = 1.0838 * cbrt(V)`. 500 cc
gives 0.086 m, a fundamental near 2 kHz.

**Compression.** Chamber length scales between `L` at bottom dead centre and
`L/CR` at the top, following the piston function directly, per sample.

**Valve to reflection coefficient.** The paper says only that an open valve
sends "most of the signal" to the corresponding output. `valveReflection()` maps
opening `o` to `0.95 - 1.05*o`, so a closed valve is near-rigid (+0.95,
recirculating) and a fully open one is slightly open-ended (-0.10, radiating
90%). The sign change through zero is physically right and continuous, so it
introduces no click.

**Piston-end reflection.** Never stated; `+0.95`, near-rigid, phase preserving.
The ignition impulse is injected at this end.

**Muffler element lengths.** The paper asks for delay lines set "so that every
frequency has a peak in the frequency response of at most one element". A
waveguide of one-way delay `k` resonates at multiples of `fs/(2k)`, so elements
share peaks only when their delays share factors. Element lengths are
`{0.80, 0.95, 1.15, 1.40}` times the Muffler parameter, each snapped to the
nearest prime sample count, making them pairwise coprime. The core tests assert
that across the whole length range.

**Intake and outlet "gain".** Read as output-tap level in the final mix, not as
waveguide feedback. The free-end reflection coefficients stay at the paper's
fixed values (intakes `-0.5`, extractors `0.1`), with the tailpipe at `-0.35`.

**Cycle asymmetry.** Implemented as alternating firing intervals
`(1/N)(1 ± 0.5a)`, renormalised so the cycle always closes. It reduces exactly to
`k/N` at `a = 0` and gives a 3:1 interval ratio at `a = 1` — the cross-plane V8
and chopper lope the paper mentions. Exposed as **Growl**.

**Intake collectors.** Figure 5 is ambiguous about one shared plenum versus
per-cylinder runners, but the output description says "the summed output of the
free ends of the intake collectors", plural. Implemented as one runner per
cylinder, detuned ±10% around the Intake Length parameter, which is also why
that parameter is an *average* length.

**Turbulence level.** The paper gives no control for the aspiration noise, only
that it is lowpassed white noise amplitude-modulated by the intake valve. The
cutoff tracks throttle (800 Hz to 4 kHz) and the level is exposed as
**Turbulence**.

**Block vibration versus load.** The paper sums bare piston motion, which would
make the block tap exactly as loud at idle as at full throttle. Here the piston
term is scaled by `0.35 + 0.65 * throttle` so the chassis responds to load.

**Parallel junctions.** The paper's "sum inputs, divide the output equally" is
asymmetric and would make manifold level scale with cylinder count. `1/sqrt(M)`
is used in both directions instead: same round-trip self-gain, energy neutral,
level stable from one cylinder to twelve.

**Per-tap makeup.** The three model outputs leave the network at very different
levels — the block tap is a direct sum of drive functions while the exhaust
reaches the tailpipe only after the manifold junction, pipe, muffler and outlet.
`kIntakeMakeup`, `kBlockMakeup` and `kExhaustMakeup` put them on comparable
footing. These are measured, not derived.

**Transport sync.** A plugin-level addition. In Host Sync the engine cycle locks
to tempo at a chosen number of cycles per beat, so `targetRpm = 2 * bpm * ratio`;
when the transport is stopped or BBT is invalid it falls back to Idle. Either way
the target passes through the Inertia slew, so mode switches and transport
stop/start ramp like a flywheel.

## Stability

This is a network of up to 42 coupled feedback delay lines with time-varying
lengths and time-varying coefficients. Four things keep it bounded, and none of
them is optional:

1. **Non-expansive ends.** The two-port split maps an arriving wave `u` to
   `(g*u, (1-|g|)*u)`, whose gain is `sqrt(g^2 + (1-|g|)^2) <= 1`. Coefficients
   are clamped to `|g| <= 0.95` (`kMaxReflection`), making every end strictly
   contractive.
2. **Explicit loss.** Each pass is scaled by `lossGainForRoundTrip()`, a
   per-sample gain derived from a target round-trip gain, so decay is
   independent of length and sample rate. A one-pole low-pass in each direction
   makes the loop lossy at high frequency.
3. **Energy-neutral junctions.** `1/sqrt(M)` in both directions. Summing on both
   sides is the mistake that would turn twelve extractors into a 12x amplifier.
4. **Delay slew limiting.** `kDelaySlewPerSample = 0.10`. A delay line read at a
   moving length has an instantaneous gain of `1/(1 - dD/dn)`; unbounded, the
   read pointer can also overtake the write pointer. At 9000 rpm with a large
   chamber the unlimited piston sweep reaches `dD/dn ≈ 0.19`, giving a Doppler
   factor of 1.23 which, against a cylinder loop gain of 0.79, leaves almost no
   margin. Limiting to 0.10 caps the factor at 1.11.

Behind those: hard clamping on every delay-line write, denormal flushing on
every recursive state, `tanh` soft clipping on each output tap and on the final
mix, DC blockers on all three taps, and a per-block `isfinite` guard that clears
the whole network if it ever trips.

Hermite interpolation is used on every read. It matters most on the cylinder
lines, which are short and whose fractional part sweeps the full range twice per
engine cycle: linear interpolation there is a fraction-dependent low-pass and
would appear as piston-locked amplitude modulation.

Parameter-derived lengths pass through a 30 ms smoother at control rate. The
piston modulation does not — it is already continuous and band-limited, and
smoothing it would attenuate the effect it exists to produce.

## Control rate versus audio rate

Control rate (every 64 samples): RPM resolution and inertia slew, phase
increment, throttle, ignition amplitude, all length-to-samples conversions and
their smoothers, muffler prime snapping, per-waveguide loss gains, filter
coefficients, cylinder phase offsets, listening-position weights and pan gains.

Audio rate: the phasor and its wrap detection, all four cycle functions per
cylinder, the valve-derived reflection coefficients (they *are* cycle
functions), the chamber delay modulation and its slew limiter, every waveguide
read and write, noise, the backfire test at each phasor wrap, the three output
taps and the stereo mix.

A 4096-entry sine table serves all four cycle functions: twelve cylinders would
otherwise need 48 transcendental evaluations per sample.

## Visual acceptance

Panel review against the repository's UI screenshot criteria is recorded in
[../README.md](../README.md); recapture with
`scripts/capture-plugin-screenshots.sh magneto` after any UI change.
