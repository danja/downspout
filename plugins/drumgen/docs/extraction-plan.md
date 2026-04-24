# drumgen extraction plan

## Phase 1: contracts

Define framework-neutral types for:

- controls and parameter defaults
- lane and step state
- pattern state
- variation state
- transport snapshot
- pending note-off entries
- scheduled MIDI output

Exit condition:

- `downspout` has stable `drumgen` core types with no LV2 or DPF headers.

Status: completed.
Portable core type definitions now exist in `include/drumgen_core_types.hpp`.

## Phase 2: pure core port

Port the following source modules into portable C++:

- pattern generation and cleanup
- transport helpers
- variation logic
- state sanitization and legacy upgrade helpers
- serialization helpers for wrapper-facing state persistence

Exit condition:

- a `downspout_drumgen_core` target builds with no LV2 headers.

Status: completed.
Portable pattern generation, transport helpers, variation logic, and state
sanitization now build as `downspout_drumgen_core` with no LV2 headers.
Text serialization for controls, pattern state, and variation state now exists.

## Phase 3: scheduling boundary

Extract the run-loop decision logic from `drumgen_plugin.cpp` into a host-neutral
engine layer that:

- consumes current controls plus a transport snapshot;
- maintains pending note-offs across blocks;
- emits scheduled note on/off events for all active lanes in a frame block;
- handles stop, rewind, and loop-boundary mutation correctly.

Exit condition:

- the remaining wrapper work is parameter I/O, transport adaptation, MIDI sink
  adaptation, and state bridge code.

Status: completed.
`drumgen_engine.*` now owns the portable scheduling boundary, including pending
note-off carryover, stop handling, restart resync, and loop-boundary mutation
cleanup.

## Phase 4: tests

Add deterministic tests for:

- same-seed regeneration stability;
- fill-only refresh preserving earlier bars;
- single-bar refresh behavior;
- loop variation cadence;
- stopped-transport pending-note cleanup;
- restart-at-boundary hit resync;
- serialization round-trips for controls, pattern, and variation;
- legacy saved-control upgrade behavior.

Status: in progress.
Initial tests now cover deterministic generation, fill refresh, single-bar
refresh preservation, transport helpers, variation behavior, state
sanitization, serialization round-trips, pending note-off carryover, stopped
transport cleanup, restart-at-boundary resync, and in-block boundary
scheduling. Legacy saved-control upgrade coverage is still missing.

## Phase 5: DPF wrapper

Only after the core phases above pass tests:

- add DPF plugin wrapper code;
- map host time to the core transport snapshot;
- map DPF state keys to core serialization;
- emit MIDI output from scheduled core events;
- add a first VST3 target.

Status: completed.
`drumgen.vst3` now builds through DPF on top of the portable engine,
serialization, and transport/state adapters. The first wrapper intentionally
uses the host's generic parameter UI instead of a custom DPF UI.

## Phase 6: UI follow-up

After a correct wrapper exists:

- decide how much of the current LV2 UI should be carried over;
- prefer a thinner DPF UI that does not duplicate pattern heuristics in the UI layer;
- if a pattern preview is added, drive it from core state rather than a separate
  preview-only generator.

Status: not started.
