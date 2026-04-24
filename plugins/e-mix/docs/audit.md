# e-mix audit

## LV2 source layout

- shell and DSP: `~/github/flues/lv2/e-mix/src/e_mix_plugin.cpp`
- UI: `~/github/flues/lv2/e-mix/src/ui/e_mix_ui_x11.c`
- metadata: `~/github/flues/lv2/e-mix/e-mix.lv2/e-mix.ttl`

## Core behavior

The DSP is simple and host-neutral once transport access is separated:

- cache five scalar parameters;
- read host bar position, beat-in-bar, beats-per-bar, and BPM;
- lock a cycle origin on transport start;
- divide `totalBars` into `division` equal blocks;
- mark blocks active with a Euclidean pattern using `steps` and `offset`;
- apply gain `0` to inactive blocks;
- apply fade-in/out inside active blocks using `fadeBars`;
- copy input to output for each connected channel.

## Parameters

The LV2 plugin exposes five controls:

- `total_bars`: integer, `1..4096`, default `128`
- `division`: integer, `1..512`, default `16`
- `steps`: integer, `0..512`, default `8`
- `offset`: integer, `0..511`, default `0`
- `fade`: integer, `0..4096`, default `0`

Runtime clamping in the DSP narrows the dependent ranges further:

- `steps <= division`
- `offset <= division - 1`
- `fade <= totalBars`

## State

LV2 state stores the five scalar parameters individually as float POD values.

The `downspout` port replaces that with one text payload containing the same
five parameter values under explicit keys.

## UI findings

The LV2 UI is a direct numeric editor. It exposes the values but does little to
explain how those values interact musically.

The VST3 UI therefore should not be a literal port. The useful view is:

- one control column for the five values;
- one visual panel that shows the Euclidean block pattern directly;
- one timing panel that shows block length and fade shape consequences.

## Port decisions

- preserve DSP behavior before redesign;
- keep the portable core channel-count agnostic;
- make the public VST3 wrapper stereo;
- redesign the UI around pattern readability instead of raw field editing.
