# cadence extraction plan

## Phase 1: contracts

Define framework-neutral types for:

- controls and parameter defaults
- segment capture state
- progression and variation state
- transport snapshots
- scheduled MIDI events

Status: completed.
Portable core types now exist in `include/cadence_core_types.hpp`.

## Phase 2: pure core port

Port the following source concerns into portable C++:

- harmony and progression derivation
- comp scheduling helpers
- variation logic
- transport helpers
- state serialization helpers

Status: completed.
These now build as `downspout_cadence_core` with no LV2 headers.

## Phase 3: engine boundary

Extract the run-loop behavior into a host-neutral engine that:

- consumes controls, transport, and incoming MIDI;
- learns segment activity from played notes;
- emits harmony and comp MIDI in block-relative frame order;
- handles stop, restart, rewind, and cycle-boundary transitions explicitly.

Status: completed.
`cadence_engine.*` now owns the portable learning and playback scheduling layer.

## Phase 4: tests

Add deterministic tests for:

- progression building from captured material;
- serialization round-trips;
- stopped-transport behavior;
- learn-to-playback boundary behavior.

Status: in progress.
Initial cadence core tests now pass, but wrapper-facing state coverage and
legacy-state migration coverage are still thin.

## Phase 5: DPF wrapper

Add the DPF/VST3 shell only after the core builds and tests cleanly:

- parameter mapping
- transport adaptation
- MIDI input/output bridge
- state bridge
- first custom UI

Status: completed.
`cadence.vst3` now builds through DPF with a custom UI.
