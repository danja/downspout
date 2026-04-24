# cadence audit

Audit date: 2026-04-24

Source plugin: `~/github/flues/lv2/cadence`

## Summary

`cadence` fits the established `downspout` MIDI-generator porting pattern.

Its LV2 entry point mixes several concerns:

- parameter and port wiring;
- host transport interpretation;
- MIDI input capture and MIDI output scheduling;
- learned-state and variation lifecycle;
- LV2 state save/restore;
- X11 UI plumbing.

The musical logic is already separable enough to port into portable C++ modules:

- harmony and progression derivation;
- comp scheduling;
- cycle variation policy;
- transport helpers.

That makes `cadence` a good match for the same architecture used by `bassgen`
and `drumgen`: portable deterministic core plus a thin DPF/VST3 shell.

## Source concerns to preserve

- learning a cycle of incoming MIDI across host transport;
- deriving a progression from captured pitch-class and timing activity;
- transport-synced voiced chord playback;
- optional comp hits that respond to captured onset timing;
- loop-aware variation that mutates on cycle boundaries;
- stable saved state for controls, learned progression, and variation progress.

## Main migration risks

- learning and playback both depend on accurate bar and cycle boundary handling;
- transport restart and rewind behavior has to be explicit in the new engine;
- the original plugin mixes harmony and comp scheduling in one run loop, so the
  new engine has to preserve that timing without LV2 atom APIs;
- state has to remain stable even though the storage mechanism changes from LV2
  state blobs to DPF state keys.

## Portable split used in `downspout`

### Core types

- controls
- learned segment capture
- chord slots and progression state
- variation state
- transport snapshot
- MIDI event representation

### Core services

- harmony and progression generation
- comp planning from learned timing bins
- cycle variation policy
- transport and segment helpers
- text serialization for controls, progression, and variation
- host-neutral block engine

### Wrapper responsibilities

- map DPF parameters into core controls
- map DPF `TimePosition` into the core transport snapshot
- translate DPF MIDI input and output into core event buffers
- bridge DPF state keys to core serialization
- expose a control-complete custom UI
