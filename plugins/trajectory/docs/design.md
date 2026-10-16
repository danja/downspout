# trajectory — design notes

`trajectory` is a port of the Trajectory experiment in the Flues repository
(`experiments/trajectory/src/audio/modules/TrajectoryOscillator.js`, with the
algorithm written up in that directory's README and in `docs/2026-01-10_trajectory.md`).
This document records what was kept, what changed, and what was measured.

## Layout

```
include/trajectory_params.hpp      ParamSpec table, ParamId, MIDI channel names, CC map
include/trajectory_oscillator.hpp  Oscillator: point in a regular polygon (fixed storage)
include/trajectory_envelope.hpp    Adsr and per-block AdsrSteps
include/trajectory_engine.hpp      Engine: 8 voices, MIDI, smoothing, DC blocker
src/trajectory_oscillator.cpp
src/trajectory_engine.cpp
src/dpf/                           DistrhoPluginInfo.h, TrajectoryPlugin.cpp, TrajectoryUI.cpp
tests/trajectory_core_tests.cpp
```

The core never touches DPF. The wrapper forwards parameter writes and MIDI to
`Engine` and writes nothing else.

## What was kept

- The oscillator: a point in a unit-circumradius regular N-gon (N 3-12), vertices at
  `2 pi i / N + pi / N`, outward edge normals, an inside test per edge, and the output
  `x * mixX + y * mixY`.
- The reflection rule: when a step leaves the polygon, the edge it went furthest past
  supplies the normal, the velocity is mirrored in it, and the point is projected back
  inside with a `1e-4` nudge.
- **The two-bounce step.** The original loops `for (bounce = 0; bounce < 2; ...)`, and
  after a reflection the loop takes a second step from the corrected position. So a
  sample that bounces moves the point twice. It is kept as is: it is part of how the
  oscillator sounds, and it is why pitch is not exactly proportional to step size.
- Speed `= frequency * 4 / sampleRate`, with the same constant.
- Start position (a ray from the centre to the boundary, at 0.995 of the distance) and
  launch angle.
- Bounce jitter: a random rotation of up to the set angle on each bounce.
- The six controls and their ranges: Sides 3-12, Start Position 0-360 deg, Launch Angle
  0-360 deg, Bounce Jitter 0-10 deg, X Level and Y Level 0-1. Defaults X 0, Y 1 as in the
  original. The original's default Sides (0.33 of 3-12) is 6 and Launch Angle (0.125 of
  360 deg) is 45 deg, so those match too.

## What changed

- **Reverb removed.**
- **ADSR.** The original is attack/release only (exponential mapping 1 ms-1 s and
  10 ms-3 s on a 0-1 control, gate on = climb, gate off = fall). Now Attack, Decay,
  Sustain, Release, all linear ramps like the original. Times are for a full-scale
  ramp: a step is `1 / (seconds * sampleRate)`, so release matches the original exactly
  and decay from 1 down to Sustain takes `(1 - Sustain) * Decay`.
  Ranges are linear in ms because the panel's slider kit is linear (Attack 1-1000,
  Decay 5-3000, Release 10-4000), unlike the original's 0-1 exponential knobs.
- **Retrigger.** The original resets the envelope to zero on every gate. Here a retrigger
  climbs from the current level, so stealing a sounding voice does not click. A fresh voice
  starts at zero either way.
- **Live changes do not re-seat the point.** The original rebuilds the polygon and
  re-seats the point and velocity whenever Sides, Start Position or Launch Angle moves.
  That is fine for a knob but would reset the oscillator on every CC from Drift. Here:
  - Start Position is read at note-on.
  - Launch Angle turns the heading of sounding voices without moving them.
  - Sides rebuilds the polygon; a point left outside it is scaled toward the centre
    until it is back inside.
- **Speed clamp.** The step is clamped to 0.5 (note frequency about sampleRate / 8). The
  original has no clamp; with at most two bounces per sample, a step much longer than the
  polygon can escape it, and the oscillator re-seats the point if that happens.
- **Polyphony.** The original is monophonic. Here: eight voices, the same note retriggers
  its own voice, then a free voice, then the quietest releasing voice, then the oldest.
- **Velocity.** New: a Velocity parameter scales the voice (`1 - v + v * velocity / 127`).
  Default 50%.
- **Output.** New: Level, a one-pole DC blocker (the point is rarely symmetric about the
  origin), 5 ms smoothing on X Level, Y Level and Level so a CC step does not zipper, and a
  hard clamp at +-1 as a safety net.
- **MIDI.** New: a MIDI-in channel filter (off, 1-16, all), CC 1-7 and CC 120/123.

## Assumptions

- **CC map.** The task was to align the six oscillator controls with Drift's channels.
  Drift has four lanes with CC defaults 1-4 (`plugins/drift/include/drift_core.hpp`, and
  the Magneto convention in `plugins/magneto/include/magneto_params.hpp`). So the six
  controls take CC 1-6 in the Flues order, and CC 7 is Level. Drift's four default lanes
  drive Sides, Start Position, Launch Angle and Bounce Jitter; lanes retargeted to CC 5 or 6
  reach X Level and Y Level. `testControllersAlignWithDrift` reads Drift's lane table and
  fails if the two drift apart. Drift lanes also carry a channel (default 1); Trajectory's
  **MIDI in** defaults to All so no setup is needed.
- **Sides via CC.** The 0-127 range is spread across 3-12 and rounded, so CC 1 steps
  between polygons rather than gliding.
- **Mono voice.** The oscillator is mono and goes equally to both channels. The original
  is mono too.
- **Transport.** Not used. Nothing here follows host tempo.
- **State.** Everything is an ordinary parameter, which DPF saves with the project. The
  plugin has no `WANT_STATE`, so there is nothing for `scripts/check-plugin-state.sh` to flag.
- **Aliasing.** The oscillator is a raw geometric path, with no band-limiting, as in the
  original. High notes alias. The step clamp keeps it stable but does not keep it clean.
- **Controller feedback.** A CC moves the sound but not the on-screen slider (DPF has no
  DSP-side path to the panel); the voice count comes from a read-only output parameter.

## Measured: pitch is a property of the shape

The Flues algorithm moves the point at a fixed fraction of the note frequency and makes no
attempt to tune each polygon. Measured by autocorrelation of the y coordinate (launch 45
degrees, 220 Hz note, 48 kHz):

| Sides | Repeat rate / note frequency |
|-------|------------------------------|
| 4     | 1.01 |
| 6     | 0.94 |
| 8     | 0.77 |
| 12    | 0.74 |

A triangle lands near 0.4-0.5 (an octave-ish below). The ratio drifts up by a few percent
with note frequency because of the two-bounce step. What does hold: doubling the note
doubles the rate for every shape (within 10%), and the pitch is the same at 44.1, 48 and
96 kHz (within 3%). `testPitchIsGeometryDependent` and `testSampleRateScalesTheOscillator`
pin these down.

It was left this way on purpose, to keep the sound of the original. If in-tune playing
matters more than fidelity, the fix is a per-shape tuning factor on the speed; the table
above is the starting point.

## Panel

The panel uses the shared kit in `include/downspout/dpf/MagnetoKit.hpp`: sliders, a stepper
and flat accent panels, amber for the oscillator, hot metal for the orbit, cold steel for the
envelope and olive for voices and output. Two drawings are computed rather than static: the
orbit preview runs `Oscillator` itself at a fixed on-screen speed, and the ADSR sketch uses
log-scaled segment widths with the decay drawn at its real `(1 - Sustain)` length.

The kit has no rotary knob or X-Y pad and its sliders are linear, which is why the envelope
sliders get two wide columns: a 122 px slider on a 1000 ms range gave about 8 ms per pixel.
