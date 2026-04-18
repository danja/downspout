# p-mix audit

Audit date: 2026-04-18

Source plugin: `~/github/flues/lv2/p-mix`

## Summary

`p-mix` is a smaller port than `bassgen`, but its current LV2 implementation mixes host adaptation and core behavior in one file.

The actual portable logic is compact:

- parameter normalization
- bar-boundary detection
- probabilistic transition decisions
- fade scheduling
- per-sample fade progression
- uniform gain application across up to eight channels

The first `downspout` task is to split that logic into core modules so the eventual DPF wrapper remains thin.

## Source concerns

### Core behavior that should be extracted

- gain state:
  - `current_gain`
  - `target_gain`
  - `fade_step`
  - `fade_remaining`
- parameter cache:
  - granularity
  - maintain
  - fade
  - cut
  - fade duration max
  - bias
- transition rules:
  - maintain/fade/cut weights normalized by relative proportion
  - target on/off state selected independently via `bias`
  - transitions only triggered at configured bar boundaries
- fade progression:
  - random fade fraction in `[0.125, fade_dur_max]`
  - gain interpolation over computed frame count

### Wrapper behavior that should not stay in the core

- LV2 atom/time parsing
- LV2 state callback wiring
- port wiring for 8 audio inputs and outputs
- direct host-frame loop integration

## Proposed portable core split

### Core types

- parameter struct
- transport snapshot
- engine state
- boundary descriptor

### Core services

- parameter clamping
- transition decision function
- fade scheduling function
- boundary calculation helper
- frame-block processing function

### Wrapper responsibilities

- map host parameters into core parameters
- map host transport into transport snapshot
- provide audio buffers to the core block processor
- map plugin save/restore to core state serialization

## Behavior to preserve

- no transitions when transport is stopped or invalid
- optional transition exactly at frame 0 when the host block begins on a bar boundary
- no new transition while a fade is still in progress
- shared gain applied identically to every active channel
- deterministic state save/restore for user parameters

## Risks

- RNG state is currently seeded from plugin instance address, which is fine for runtime variety but poor for deterministic testing.
- State persistence currently covers parameters but not runtime gain/fade state. That may be correct, but the contract should be explicit.
- Boundary collection is capped at 16 entries per block. That is probably safe in normal host block sizes, but it should become a named design assumption or a dynamic structure.

## Recommended extraction order

1. Define parameter, transport, and engine-state types.
2. Port transition and fade logic into a pure C++ engine.
3. Port boundary detection into a helper that returns boundary frames.
4. Add deterministic tests using injectable RNG.
5. Add wrapper code for DPF after the core block processor is stable.

## Current extraction status

Completed in `downspout`:

- portable parameter, transport, boundary, and engine-state types
- portable gain-processing engine
- parameter serialization helpers
- CTest coverage for stopped transport, boundary cuts, fade progression, and serialization

Still missing:

- host/plugin-framework wrapper code
- host state bridge for plugin persistence
- real VST3 bundle target
