# p-mix extraction plan

## Phase 1: contracts

Define portable types for:

- parameters
- transport snapshot
- engine state
- transition mode/result

Status: completed on 2026-04-18.

## Phase 2: pure core engine

Extract the current in-file logic into a reusable engine that can:

- accept current parameters and transport state;
- decide whether a bar-boundary transition should occur;
- advance fade state across a frame block;
- apply the resulting gain to N channels.

Status: completed on 2026-04-18 for the transport-aware gain engine and parameter serialization.

## Phase 3: test coverage

Add tests for:

- stopped transport
- boundary at frame 0
- granularity skipping
- maintain/fade/cut weight normalization
- fade duration range
- bias target selection
- fade completion snapping to final gain

Status: in progress.
Current tests cover stopped-transport pass-through, boundary cut behavior, deterministic fade progression, and parameter serialization round-trips.

## Phase 4: wrapper integration

Only after the core exists:

- add DPF plugin wrapper code;
- map state save/restore;
- add UI work if needed.

Status: next priority.
Completed on 2026-04-18 for the first DPF/VST3 wrapper, including transport mapping and parameter state save/restore.

## Phase 5: wrapper validation and release path

Follow-up work after the first wrapper lands:

- validate stereo and multichannel behavior in Reaper;
- decide whether to add a minimal UI or keep the first release no-UI;
- verify `Release` build and install flow as part of the repository-level release workflow.

Status: next priority.
