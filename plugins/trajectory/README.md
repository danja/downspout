# Trajectory

`trajectory.vst3` — polygon-bounce synthesizer.

The oscillator is a point bouncing inside a regular polygon. It travels in
straight lines, reflects off the edges, and the waveform is the point's x and y
position, mixed. Fewer sides give angular, bright, quasi-chaotic orbits; more
sides approach a circle and mellow out. Eight voices, each with its own point and
a full ADSR envelope.

It is ported from the Trajectory experiment in the Flues project
(`experiments/trajectory`, <https://danja.github.io/flues/trajectory/>), with the
oscillator and its six controls kept, the reverb removed, and the
attack/release envelope widened to ADSR. Porting decisions and the assumptions made
are in [docs/design.md](docs/design.md).

## Using it

Trajectory is an instrument: no audio input, stereo output (the voice is mono,
sent equally to both channels). It is silent until a note arrives.

**The oscillator.**

| Control | Range | Default | What it does |
|---------|-------|---------|--------------|
| Sides | 3-12 | 6 | Polygon edge count. 3 is a triangle, 12 nearly a circle |
| Start Position | 0-360 deg | 0 | Where on the boundary the point begins, as an angle from the centre. Read at each note-on |
| Launch Angle | 0-360 deg | 45 | The direction the point sets off in |
| Bounce Jitter | 0-10 deg | 0 | Each bounce turns the heading by a random angle up to this size. Seeded, so a render repeats |
| X Level | 0-100% | 0% | How much of the point's x position is in the output |
| Y Level | 0-100% | 100% | How much of the y position is in the output |

With both levels at zero the oscillator is silent. The **ORBIT** panel previews the
path and the waveform it makes, run through the same code the synth uses.

**The envelope.** Attack 1-1000 ms, Decay 5-3000 ms, Sustain 0-100%, Release
10-4000 ms. Times are for a full-scale ramp, so a low Sustain finishes its decay
sooner than the Decay time suggests.

**Voices and output.** **Velocity** sets how far note velocity scales a voice (0
ignores it, 100% is fully proportional); **Level** is the output level; **MIDI in**
filters notes and controllers (off, one channel, or all).

**Pitch.** Pitch follows the note: doubling the note doubles the rate. The
absolute pitch is a property of the polygon, though. The Flues algorithm drives
the point at a fixed fraction of the note frequency and does not tune each shape,
so a square launched at 45 degrees repeats at the note's frequency, a hexagon sits a
little flat of it, and a dodecagon about a fourth flat. This is deliberate
fidelity to the source; see [docs/design.md](docs/design.md) for the measurements.
Tune by ear, or transpose.

## MIDI control

| CC | Control |
|----|---------|
| 1 | Sides |
| 2 | Start Position |
| 3 | Launch Angle |
| 4 | Bounce Jitter |
| 5 | X Level |
| 6 | Y Level |
| 7 | Level |
| 120 | All sound off |
| 123 | All notes off |

CC 1-6 are the six oscillator controls in panel order. CC 1-4 are the numbers
[Drift](../drift/) sends from its four lanes by default, so routing Drift's MIDI
into Trajectory morphs the oscillator with no configuration. Point a lane at CC 5
or 6 to reach X Level or Y Level.

A controller moves the sound but not the on-screen slider: DPF has no DSP-side
path back to the panel. Start Position takes effect on the next note; Sides, Bounce
Jitter and Launch Angle also act on voices that are already sounding. A Launch Angle
change turns the point's heading without moving it, so a sweep does not click.

## Layout

```
include/trajectory_params.hpp      ParamSpec table, CC map (shared DSP/UI)
include/trajectory_oscillator.hpp  polygon-bounce oscillator
include/trajectory_envelope.hpp    ADSR
include/trajectory_engine.hpp      eight voices, MIDI handling, rendering
src/                               portable core (no DPF)
src/dpf/                           thin VST3 wrapper and the panel
tests/trajectory_core_tests.cpp    deterministic core tests
```

Every setting is an ordinary parameter, which DPF saves with the project, so the
plugin carries no state of its own.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DDOWNSPOUT_BUILD_TRAJECTORY=ON
cmake --build build --target downspout_trajectory_core_tests trajectory-vst3 --parallel "$(nproc)"
ctest --test-dir build -R downspout_trajectory_core_tests --output-on-failure
```

Refresh the catalogue screenshot after any UI change:

```sh
env -u DISPLAY scripts/capture-plugin-screenshots.sh trajectory
```
