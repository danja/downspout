# bassgen audit

Audit date: 2026-04-18

Source plugin: `~/github/flues/lv2/bassgen`

## Summary

`bassgen` is already close to a portable architecture.

The LV2 entry point in `bassgen_plugin.cpp` mainly does four host-facing jobs:

- read controls from ports;
- read transport/time objects from the host;
- emit MIDI events;
- save and restore LV2 state.

Most musical behavior already lives in separate modules:

- `bassgen_pattern.*`
- `bassgen_transport.*`
- `bassgen_variation.*`
- `bassgen_state.*`

That means the first `downspout` port should not rewrite behavior. It should extract these modules into a framework-neutral core and replace only the LV2-specific shell.

## Source module breakdown

### `bassgen_schema.h`

Contains the core domain schema:

- control defaults
- enums for scale, genre, subdivision
- `ControlSnapshot`
- `NoteEventData`
- `PatternStateBlob`
- `VariationStateBlob`

This is the clearest starting point for portable type definitions.

### `bassgen_pattern.*`

Owns deterministic pattern generation and mutation:

- control clamping
- structural-change detection
- subdivision to steps-per-beat mapping
- register mapping
- rhythm generation
- note generation
- full regeneration
- partial note mutation

This should become the heart of the portable core.

### `bassgen_transport.*`

Owns step-domain transport helpers:

- restart detection
- local-step wrapping
- active-event lookup
- event start lookup
- event end detection
- frame calculation for step boundaries

This is already almost fully reusable as-is, but should be detached from LV2 naming assumptions.

### `bassgen_variation.*`

Owns loop-driven mutation policy:

- loop counting
- mutation interval calculation
- vary-strength behavior
- decision tree for partial mutation vs note regeneration vs rhythm regeneration

This is portable logic and should remain independent of host/framework code.

### `bassgen_state.*`

Contains two separate concerns that should be split in `downspout`:

- validation/clamping of saved state payloads
- LV2-specific storage/retrieval callbacks

The validation and versioning logic is reusable.
The direct `LV2_State_*` API usage is not.

### `bassgen_plugin.cpp`

Contains the true LV2-specific shell:

- URID setup
- atom/time parsing
- port wiring
- MIDI event emission
- activation/deactivation lifecycle
- run-loop scheduling against host frame windows

This file should not be ported directly.

## Functional behavior to preserve

- Deterministic generation from controls plus seed.
- Phrase regeneration when structural controls change.
- Edge-triggered actions for `New`, `Notes`, and `Rhythm`.
- Loop-aware variation that mutates only at loop boundaries.
- Immediate note-state resync on transport restart or rewind.
- Note-off before note-on around boundary transitions.
- Safety-gap sample offset for generated note-on events.
- Exact pattern persistence, not just control persistence.

## LV2-specific dependencies to replace

- `LV2_Atom_Sequence` for transport input and MIDI output
- `LV2_TIME__Position` parsing
- `LV2_State_*` save/restore entry points
- URID mapping

In `downspout`, these should become:

- a small transport snapshot input structure
- a MIDI event sink/output buffer abstraction
- a serialization API owned by the core
- framework wrapper code in the DPF layer

## Proposed portable core split

### Core types

- controls
- pattern event
- pattern state
- variation state
- transport snapshot
- scheduled MIDI event

### Core services

- control normalization
- pattern generation
- pattern mutation
- variation application
- transport stepping / boundary processing
- state validation and serialization helpers

### Wrapper responsibilities

- map host parameters into core controls
- map host transport into transport snapshot
- pass frame count and sample rate into the core
- translate scheduled MIDI events to host/plugin-framework output
- translate host save/restore APIs to core serialization

## Risks

- The current state code stores raw structs. That is convenient, but long-term portability is fragile because layout assumptions can leak across compilers or formats.
- The run loop mixes core scheduling decisions with direct MIDI emission. That split needs to be made explicit.
- Action ports are edge-triggered counters in practice. That needs a documented contract in the new core wrapper.

## Recommended next extraction order

1. Define portable types and parameter metadata in `downspout`.
2. Define a wrapper-independent MIDI scheduling/result model.
3. Port control, pattern, transport, and variation logic into pure C++ core files.
4. Replace LV2 state code with framework-neutral serialization helpers.
5. Add the DPF wrapper only after the portable core is testable.

## Current extraction status

Completed in `downspout`:

- portable core types
- wrapper-independent scheduled MIDI model
- pattern generation and mutation
- transport helpers
- loop variation logic
- state sanitization helpers
- first host-neutral engine layer for block processing

Still LV2/host-facing work to replace:

- transport snapshot adaptation from the host
- parameter bridge from host controls to `Controls`
- persistence bridge from host save/restore APIs to core serialization
- actual plugin-format wrapper code
