# drumgen audit

Audit date: 2026-04-24

Source plugin: `~/github/flues/lv2/drumgen`

## Summary

`drumgen` is a strong fit for the existing `downspout` MIDI-generator pattern.

Its LV2 entry point in `drumgen_plugin.cpp` mainly does five host-facing jobs:

- read controls from ports;
- read transport/time objects from the host;
- emit MIDI note on/off events;
- manage pending note-offs across block boundaries;
- save and restore LV2 state.

Most musical behavior already lives outside the LV2 shell:

- `drumgen_pattern.*`
- `drumgen_transport.*`
- `drumgen_variation.*`
- `drumgen_state.*`

That means the port should follow the `bassgen` model: preserve the pattern and
variation logic, replace the LV2 shell, and make the scheduling boundary explicit.

## Source module breakdown

### `drumgen_schema.h`

Contains the core domain schema:

- control defaults
- genre, resolution, kit-map, and lane enums
- `ControlSnapshot`
- `DrumStepCell`
- `DrumLaneState`
- `PatternStateBlob`
- `VariationStateBlob`

This should become the starting point for framework-neutral core types.

### `drumgen_pattern.*`

Owns deterministic pattern generation and cleanup:

- control clamping
- structural-change detection
- resolution to steps-per-beat mapping
- lane note mapping
- full pattern regeneration
- bar-only refresh
- fill overlay generation
- post-generation cleanup rules

This is the heart of the portable core.

### `drumgen_transport.*`

Owns reusable step-domain transport helpers:

- restart detection
- local-step wrapping
- boundary-to-local-step mapping
- block-frame calculation for boundaries

This is already portable logic.

### `drumgen_variation.*`

Owns loop-driven mutation policy:

- loop counting
- vary-strength cadence
- decision tree for bar refresh vs fill refresh vs full regeneration

This should remain framework-independent.

### `drumgen_state.*`

Contains two concerns that should be split in `downspout`:

- validation and upgrade of saved state payloads
- LV2-specific storage/retrieval callbacks

The data validation and legacy upgrade logic is reusable.
The direct `LV2_State_*` calls are not.

### `drumgen_plugin.cpp`

Contains the true LV2 shell:

- URID setup
- atom/time parsing
- port wiring
- MIDI emission
- pending note-off queue management
- activation/deactivation lifecycle
- run-loop scheduling against host frame windows

This file should not be ported directly.

## Behavior to preserve

- deterministic regeneration from controls plus seed and generation serial;
- edge-triggered `New`, `Mutate`, and `Fill` actions;
- fill-only refresh of the last bar when the existing pattern shape is compatible;
- loop-aware `Vary` mutations only at step-zero boundaries;
- pending note-offs across block boundaries for short drum gates;
- immediate pending-note cleanup when transport stops or loop mutation replaces hits;
- open-hat one-sample safety offset on note-on;
- exact pattern persistence, including lane steps and variation state;
- legacy control-state upgrade behavior from the older saved-state layout.

## LV2-specific dependencies to replace

- `LV2_Atom_Sequence` transport input and MIDI output
- `LV2_TIME__Position` parsing
- `LV2_State_*` save/restore callbacks
- URID mapping

In `downspout`, these should become:

- the existing transport snapshot pattern used by `bassgen` and `p-mix`;
- scheduled MIDI event output from a host-neutral engine;
- text serialization helpers owned by the core;
- DPF wrapper code for parameter I/O, host time, MIDI output, and state keys.

## Proposed portable core split

### Core types

- controls
- lane and step state
- pattern state
- variation state
- transport snapshot
- pending note-off entry
- scheduled MIDI event

### Core services

- control normalization
- pattern generation and cleanup
- bar refresh and fill refresh
- variation application
- transport stepping helpers
- block engine with note-off carryover
- state sanitization and serialization

### Wrapper responsibilities

- map host parameters into core controls
- map DPF time position into transport snapshot
- translate scheduled MIDI events to DPF MIDI output
- map plugin save/restore to core serialization
- keep UI logic separate from pattern generation

## Main risk

The primary risk is not the pattern generator. It is the scheduling engine.

Unlike `bassgen`, `drumgen` is polyphonic and schedules many short note-offs.
If pending note-off handling or restart resync is wrong, the musical output will
be audibly wrong even when the underlying pattern data is correct.

## Recommended extraction order

1. Define portable core types from the schema.
2. Port pattern, transport, variation, and state-sanitization logic into pure C++.
3. Add a host-neutral engine layer that owns pending note-offs and MIDI scheduling.
4. Add deterministic tests for regeneration, fill refresh, restart, and state.
5. Add the DPF wrapper only after the core passes tests.

## Current extraction status

Completed in `downspout`:

- plugin scaffold
- guarded build wiring
- per-plugin audit and extraction docs
- portable core types
- pattern, transport, variation, and state-sanitization code
- host-neutral engine
- serialization helpers
- deterministic core and engine tests
- first DPF/VST3 wrapper target

Still missing:

- legacy saved-state upgrade coverage
- custom UI work
