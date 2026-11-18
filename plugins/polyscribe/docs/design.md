# Polyscribe design notes

An original polyphonic audio-to-MIDI analyser in the Magneto look. It does not
reuse Qune's monophonic tracker: that tracker estimates one continuous pitch and
its octave-error logic is tuned for a single voice, whereas Polyscribe needs to
choose several simultaneous notes and map percussive bands to drum messages.

## Layout

| File | Role |
|---|---|
| `include/polyscribe_params.hpp` | Parameter ids, specs, enums, helpers and preferred ranges. Portable |
| `include/polyscribe_core_types.hpp` | Engine state, event structures and read-only status API. Portable |
| `src/polyscribe_engine.cpp` | Analysis, note selection, MIDI lifecycle and pass-through. Portable |
| `tests/polyscribe_core_tests.cpp` | Deterministic core tests |
| `src/dpf/` | Thin DPF parameter/status/MIDI wrapper and NanoVG panel |

The wrapper holds one `Parameters` and one `EngineState`. The core knows nothing
about DPF, hosts or the UI.

## Pitched analysis

The two input channels are summed to mono and written into a 4096-sample ring.
Every 512 samples the engine applies a Hann window and measures Goertzel
magnitudes for MIDI notes 24-120. Analysis is driven by an absolute sample
counter, so changing the host block size does not change the measurement points.

Local maxima above a sensitivity-dependent floor become candidates. Candidates
are sorted by magnitude; a weaker candidate at an equal-tempered harmonic of an
accepted lower note (the rounded intervals for harmonics 2-8) is discarded.
This is a cheap way to stop a guitar harmonic or a bright synth partial becoming
a second note.

- **Melody** chooses the strongest accepted candidate. If the previous note is
  still within 85 % of the strongest magnitude it is kept, so phase drift across
  equal chord tones does not flip the output between notes.
- **Bass** chooses the lowest accepted candidate in C2-C4 with the same hysteresis.
- **Chords** keeps already-sounding candidates whose magnitude is at least 60 %
  of the strongest, then fills remaining slots up to **Max Notes**. Without this,
  Max Notes = 1 hopped between equal chord tones as their phases moved.
- **Low Note / High Note** intersect with the type's preferred range rather than
  extending it. Melody is C3-C6, Bass C2-C4, Chords C2-C7.

## Drum analysis

A 4096-sample window smears a short attack across many hops and made sustained
tones re-trigger. Drums mode therefore measures the most recent 1024 samples of
the ring with its own Hann window and three Goertzel probes: MIDI notes 36
(65 Hz), 62 (294 Hz) and 108 (4.2 kHz). These are *analysis* pitches; the
messages sent are the GM output notes 36 (kick), 38 (snare) and 42 (closed hat).
Using the output notes as probes put all three bands below 100 Hz and could not
distinguish a snare or hat from a kick.

Each band has an envelope, a peak, an armed flag and a 60 ms cooldown. It fires
once when the short-window level rises above the sensitivity floor and at least
45 % above the decaying envelope. It re-arms only after the level has fallen
below a fraction of its peak, so a ringing drum does not emit a stream of
repeated hits. The core test feeds three separated band-limited bursts and
asserts exactly one note-on for each output note.

## Note lifecycle

Each analysis produces a bounded desired-note set. `syncNotes()` compares it
with up to eight active notes:

1. A wanted note refreshes its `lastSeen` time.
2. An unwanted note is released once both Gate (from note-on) and Release (from
   `lastSeen`) have elapsed.
3. A new desired note takes the first free slot; a note-on records its absolute
   start time, channel and velocity.

Output is bounded to 64 events per block. Non-finite input samples are replaced
with zero and counted in `faults`. Changing input type, channel, Transpose or
MIDI Out releases all notes at frame 0 before the new setting takes effect, so a
host cannot leave notes stuck on a stale channel.

## State, transport and lifecycle

There is no `WANT_STATE`. All 13 user controls are ordinary automatable
parameters and five read-only output parameters carry live status to the panel.
`prepare()` builds the Hann and Goertzel tables and clears runtime;
`sampleRateChanged()` calls it. `activate()` calls `reset()`, which clears only
the ring, detection envelopes and active notes. The transport is ignored.

Detection is not latency-compensated: the first pitched result needs a full
4096-sample window (about 93 ms at 44.1 kHz), while Audio Thru stays
sample-parallel. The plugin declares no latency because it does not delay audio;
a host cannot backdate MIDI that has not been analysed yet.

## UI

`PolyscribeUI` derives from `MagnetoUI`. The four panels follow the musical
workflow: choose what to listen for, shape the emitted notes, choose the MIDI
destination, then read the live result. Enum parameters use DPF enumeration
values so hosts show names. The captured 920x720 panel was reviewed at full
resolution and published as `docs/pages/assets/plugins/polyscribe.png`.

## Tests and untested areas

The core suite covers mode selection, balanced note-on/off pairs, all four input
types, sensitivity, low/high filtering, Max Notes hysteresis, Gate and Release
timing, velocity/dynamics, Transpose, MIDI channel, MIDI Out, Audio Thru and
channel changes. It runs under `-UNDEBUG` through
`downspout_add_core_test()`.

Not covered by automated tests: real instruments and voices, polyphonic chord
recognition with inharmonic overlap, and host automation of output parameters.
Host validation in REAPER (MIDI routing, note-off balance, channel 10 drums,
output-parameter status and save/reopen) is being checked by the user.
