# Xoxolo plan

`xoxolo` will be a minimal, direct MIDI drum pattern editor. The intent is a
small x0x-style sequencer that is useful immediately with `drumkit.vst3` and
other drum instruments, without becoming another probabilistic generator.

## First-pass goal

Build a VST3 MIDI generator/effect with:

- a clickable step grid;
- 11 Downspout drum lanes matching the current `drumkit` note map;
- editable MIDI note number per lane;
- a note-name preset selector for Downspout and AVL-Drumkits mappings;
- a per-lane preview button;
- host-transport-synced looping;
- stable text state serialization for the pattern and lane note map.

Default layout:

- 16 default columns;
- 11 Downspout rows, or 29 AVL-Drumkits rows;
- 1/16 resolution;
- selectable 8-32 columns/steps;
- MIDI channel 10;
- fixed normal velocity around 100.

Default `Downspout` lane map:

| Lane | Name | MIDI note |
| --- | --- | --- |
| 1 | Kick | 36 |
| 2 | Clap | 39 |
| 3 | Snare | 40 |
| 4 | Crash | 41 |
| 5 | Closed HH | 42 |
| 6 | Low Tom | 45 |
| 7 | Open HH | 46 |
| 8 | High Tom | 50 |
| 9 | Bash | 51 |
| 10 | Cowbell | 52 |
| 11 | Clave | 53 |

The `AVL-Drumkits` preset exposes all 29 entries from `docs/avl-drum-map.md`.

## UI

The main UI should be the grid, not a configuration page.

Suggested layout:

- left/main area: step grid;
- right edge of each row: preview button, then note dropdown;
- right-side control column:
  - Preset: `Downspout`, `AVL-Drumkits`;
  - Steps: `8` through `32`;
  - Resolution: `1/4`, `1/8`, `1/16`;
  - Channel: `1` to `16`, default `10`;
  - Clear;
  - optional Play Mode: `Host` only for first pass, with `Free` deferred.

Cell interaction:

- left click toggles a note on/off;
- active cells use one clear filled state;
- the current playback step is highlighted when transport is running;
- empty grid cells remain large enough to click reliably at the smallest planned
  plugin size.

For the first pass, avoid per-cell velocity editing unless it falls out very
cheaply. A later pass can add click-cycling or modifier gestures for accent and
ghost notes.

Keep the visible grid complete. Do not add paging for the first pass. If a
bar/resolution combination would exceed 32 columns, disable that combination in
the UI rather than hiding steps on another page.

## Pattern shape

The first implementation should treat columns as the full loop, not as a view
onto hidden material.

Recommended first-pass mapping:

- total steps are selected directly by the user;
- resolution controls how quickly those steps advance against host transport;
- default length is 16 steps;
- any integer length from 8 through 32 is valid;
- 32 total steps is the hard first-pass maximum.

The older Bars-derived shape was replaced because it prevented useful odd loop
lengths such as 13, 15, or 27 steps.

If a resize changes total step count:

- preserve existing cells that still fit;
- initialize new cells empty;
- do not regenerate or randomize anything;
- if shrinking and then expanding, lost cells are not restored unless an undo
  system is added later.

Rows are preset-dependent for the first pass: 11 for Downspout and 29 for
AVL-Drumkits. Row count controls can be added later if real use shows that
fixed preset row sets are too limiting.

## Transport and MIDI behavior

The core should use the same host-neutral transport snapshot pattern as
`drumgen`, `cadence`, and `rift`.

Playback rules:

- when host transport is stopped, do not advance the sequence;
- on play start, derive the current step from host bar/beat position rather than
  from an internal free-running counter;
- on rewind, loop, or jump, follow the host position immediately;
- bar and meter changes should recalculate step position without corrupting the
  saved pattern;
- events should be emitted at sample-accurate frame offsets inside the current
  audio block where practical.

MIDI event rules:

- do not pass MIDI input through in the first pass;
- emit note-on at active step boundaries;
- emit short note-off events for every emitted note-on, even for drum use, so
  external samplers do not accumulate held notes;
- default note length can be a small fixed duration such as 30 ms, clamped so
  note-off never precedes note-on;
- all notes use the selected output channel;
- preview buttons should send MIDI from the plugin processor, triggered by a UI
  parameter/serial counter, not directly from the UI thread.

Important edge cases:

- if two lanes share the same MIDI note, both active cells may emit note-ons;
- note numbers must clamp to `0..127`;
- channel must clamp to `1..16` in UI/state and convert to `0..15` at MIDI
  output;
- pending note-offs must be flushed or completed cleanly when transport stops,
  channel changes, note mapping changes, or the plugin is reset.

## State

Use explicit text serialization, following the existing Downspout pattern.

State should include:

- state version;
- steps;
- resolution;
- channel;
- selected note-name preset;
- preset lane count;
- each lane note number;
- active cell bits for every lane and step.

Do not store UI-only hover/selection state in the plugin state. It is fine to
store the selected lane later if it becomes useful, but the first pass should
only persist musical behavior.

## Architecture

Expected plugin shape:

```text
plugins/xoxolo/
├── CMakeLists.txt
├── README.md
├── docs/
│   └── implementation.md
├── include/
│   ├── xoxolo_core_types.hpp
│   ├── xoxolo_engine.hpp
│   ├── xoxolo_params.hpp
│   └── xoxolo_serialization.hpp
├── src/
│   ├── dpf/
│   │   ├── DistrhoPluginInfo.h
│   │   ├── XoxoloPlugin.cpp
│   │   └── XoxoloUI.cpp
│   ├── xoxolo_engine.cpp
│   └── xoxolo_serialization.cpp
└── tests/
    └── xoxolo_core_tests.cpp
```

Keep the portable core responsible for:

- pattern storage and sanitization;
- resize behavior;
- transport-to-step mapping;
- MIDI scheduling;
- preview trigger handling;
- state serialization/deserialization.

Keep the DPF wrapper responsible for:

- converting DPF time position to the portable transport snapshot;
- exposing parameters and UI-trigger serials;
- writing MIDI events;
- saving/restoring serialized state.

Keep the UI responsible for:

- grid drawing and hit testing;
- note dropdowns;
- preview buttons;
- compact control-column widgets.

## Parameters

Keep the parameter surface small. Suggested first-pass parameters:

- `Steps`
- `Resolution`
- `Channel`
- `Clear`
- `Preview Lane`
- `Pattern Dirty Serial` or equivalent UI-to-plugin update trigger if the grid
  is stored as plugin state rather than individual automatable parameters.

Avoid making every grid cell an automatable parameter. That would create a large
host automation surface for no clear benefit and would make maintenance harder.
The grid should live in explicit state, with a small number of trigger/status
parameters for wrapper/UI coordination.

## Tests

Core tests should cover:

- default pattern shape and default lane note map;
- toggling cells;
- resize preserves overlapping cells and clears new cells;
- note dropdown/state clamps invalid notes;
- stopped transport emits no sequence notes;
- play start emits the correct step for the host position;
- loop/rewind follows host position rather than internal elapsed time;
- note-offs are emitted after note-ons;
- preview trigger emits exactly one note-on/note-off pair;
- serialization round-trips pattern cells, lane notes, steps, resolution, and
  channel.

Wrapper/UI behavior still needs DAW validation, but the core should be
deterministic enough that first-pass regressions do not require a DAW to catch.

## Deferred

- velocity/accent editing;
- probability per step;
- flam/repeat/ratchet;
- swing/groove timing;
- named kits beyond the default `drumkit` map;
- row count editing;
- MIDI input recording;
- standalone/free-run transport;
- pattern import/export files;
- copy/paste between plugin instances.
