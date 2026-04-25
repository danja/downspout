# bassgen extraction plan

## Phase 1: contracts

Create framework-neutral contracts for:

- controls
- parameter defaults and ranges
- pattern state
- variation state
- transport snapshot
- MIDI event output

Exit condition:

- the `downspout` repository has stable core types that describe `bassgen` without any LV2 or DPF headers.

Status: completed on 2026-04-18.

## Phase 2: pure core port

Port the following source modules into portable C++:

- pattern generation
- transport helpers
- variation logic
- state validation helpers

Exit condition:

- a core target builds without DPF and without LV2 headers.

Status: completed on 2026-04-18 for pattern generation, transport helpers, variation logic, RNG, and state sanitization.

## Phase 3: scheduling boundary

Extract the run-loop decision logic from `bassgen_plugin.cpp` into a host-neutral engine layer that:

- consumes transport snapshots plus current controls;
- maintains active-note and loop state;
- emits scheduled MIDI note on/off events for a frame block.

Exit condition:

- the only remaining framework code is parameter I/O, transport adaptation, MIDI sink adaptation, and state bridge code.

Status: in progress.
Completed.
The host-neutral engine layer now exists and emits scheduled MIDI events for a block.

Update:

- framework-neutral serialization helpers now exist for controls, pattern state, and variation state.
- first DPF wrapper work is complete enough to build `bassgen.vst3` with UI.
- the remaining wrapper work is host validation and any follow-up fixes discovered there.

## Phase 4: tests

Add deterministic tests for:

- repeated generation with the same seed;
- structural-change regeneration;
- note-only and rhythm-only actions;
- rewind handling;
- loop variation cadence;
- persisted-pattern restoration;
- boundary scheduling around note end/start collisions.

Status: in progress.
Current coverage includes deterministic generation, rhythm-only regeneration behavior, transport helpers, loop variation, state sanitization, rewind resync, stopped-transport note-off handling, and boundary end/start scheduling.

Current coverage also includes serialization round-trips for controls, pattern state, and variation state.

## Phase 5: DPF wrapper

Add DPF plugin code only after the core phases above pass tests.

The first wrapper should target correctness, not UI completeness.

Status: completed.
`bassgen.vst3` now builds through DPF on top of the portable core, transport,
serialization, and scheduling code. The remaining work is host validation and
incremental fixes found during DAW testing.
