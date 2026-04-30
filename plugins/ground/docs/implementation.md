# ground implementation

## Architecture

`ground` follows the normal `downspout` shape:

- portable core in `include/` and `src/`;
- text state serialization;
- thin DPF VST3 wrapper;
- custom NanoVG UI;
- deterministic core tests.

## Core split

Main pieces:

- `ground_core_types.hpp`
  Shared control, form, phrase, event, variation, and transport types.
- `ground_pattern.cpp`
  Form planning and phrase/cell regeneration.
- `ground_variation.cpp`
  Mutation cadence for completed form loops.
- `ground_engine.cpp`
  Transport-aware MIDI scheduling and live phrase-role status.
- `ground_serialization.cpp`
  Stable text save/load contract.

## Scheduling model

The engine is monophonic and transport-synced.

- it keeps one active note at a time;
- it maps host bar/beat timing into 16th-note steps;
- it emits note-offs before note-ons on relevant boundaries;
- on transport restart or rewind, it re-synchronizes to the note that should be
  sounding at the current position.

## Variation model

`Vary` acts at the form-loop level, not every bar.

Depending on amount, completed form loops can:

- mutate the local cell of one phrase;
- refresh a phrase role and its material;
- regenerate the whole form.

That keeps the plugin changing on a higher structural timescale than
`bassgen`.

## Phrase feel

The phrase planner now does two extra passes beyond raw onset generation:

- it adds role/style-dependent syncopated pickups and off-beat placements so
  later bars in a phrase do not feel locked to only square quarter/half-beat
  starts;
- it stretches note durations toward the next onset, then merges contiguous
  repeated notes, so the line reads more legato instead of pecking every event.

## UI status pattern

The wrapper exposes output parameters for:

- current phrase index;
- current phrase role.

The UI uses those values to show the live position through the form instead of
only reflecting the last edited parameter value.
