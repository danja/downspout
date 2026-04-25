# rift design

## Intent

`rift` is an original transport-aware audio effect for `downspout`.

The repository already has:

- `p-mix` for probabilistic gain-state switching;
- `e-mix` for Euclidean gating;
- several MIDI-generative and controller-heavy plugins.

What it did not have was a buffer-driven effect that can stay rhythmically
useful while still producing accidental ideas. `rift` fills that gap.

## Core idea

The plugin keeps a rolling audio buffer of recent input. At each rhythmic block
boundary, it either:

- passes audio through unchanged; or
- replays a slice from the recent past using one of a small set of actions.

This gives it three useful properties:

1. It stays locked to transport.
2. It only changes at musically legible moments.
3. The source material always comes from what the user just played.

## Action set

- `Pass`
  Dry audio.
- `Repeat`
  Replays a recent slice forward at normal speed.
- `Reverse`
  Replays that slice backward.
- `Skip`
  Drops the wet slice entirely.
- `Smear`
  Replays a slice at a slower rate.
- `Slip`
  Replays a slice at a pitch-derived rate.

## User-facing design choices

The UI avoids exposing too many microscopic options. Instead it uses:

- a few strong musical macros;
- three large performance gestures;
- a bottom strip of named mode recipes for quick direction changes;
- visual explanations of the block plan and action bias.

That tradeoff matters. This plugin is supposed to spark ideas in a DAW session,
not ask the user to become a scheduler debugger.

## Transition smoothing

The mutation engine is allowed to jump to a completely different slice at a
block boundary, but the output should not hard-click when that happens. The
implementation therefore uses a short boundary crossfade instead of trying to
solve the problem with full-plugin oversampling.
