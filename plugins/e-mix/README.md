# e-mix port

This directory holds the `downspout` port of `~/github/flues/lv2/e-mix`.

`e-mix` is a transport-aware Euclidean audio gate. It slices a cycle of bars
into evenly sized blocks, activates a Euclidean subset of those blocks, and
applies optional fade-in/out inside each active block.

## Source audit

The source LV2 plugin is compact:

- main shell and DSP: `src/e_mix_plugin.cpp`
- UI: `src/ui/e_mix_ui_x11.c`
- metadata: `e-mix.lv2/e-mix.ttl`

## Port status

This port now has:

- a portable `e-mix` core with deterministic tests;
- text state serialization for VST3 state save/restore;
- a first DPF-backed `e_mix.vst3` wrapper target;
- a redesigned DPF UI that emphasizes the Euclidean pattern and block timing.

## Mapping notes

- The source LV2 shell exposed eight optional audio inputs and outputs.
  The VST3 wrapper is intentionally stereo-first for normal DAW use, while the
  portable core still accepts variable channel counts.
- The LV2 README says stopped transport should pass audio through, but the
  source code actually advances a fallback internal bar clock when host
  transport is unavailable or stopped. The current port preserves the source
  code behavior.

## Next checks

1. Validate `e_mix.vst3` in Reaper, especially stopped transport behavior and
   restart handling.
2. Tune the UI if the cycle preview still feels opaque during real host use.
