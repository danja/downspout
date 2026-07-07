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

- follows an enabled sequence cell;
- passes audio through unchanged; or
- replays a slice from the recent past using one of a small set of actions.

This gives it three useful properties:

1. It stays locked to transport.
2. It only changes at musically legible moments.
3. The source material stays musically attached to the session.

In live mode the source material comes from what the user just played. In sample
mode, a loaded WAV file is mapped by musical beat position and then fed into the
same rolling-buffer engine as live input. This keeps the action set and
smoothing behavior shared: sample playback is source material for `rift`, not a
separate effect that bypasses the existing processor.
Pass-through still means the original plugin input. A loaded sample only becomes
audible when the engine selects a wet action from density, scatter, or an
enabled sequence cell.

Sample mapping uses an explicit loop length in beats. The current DAW-facing
default is four beats because break loops often need to be treated as one
musical bar even when the raw file duration does not exactly imply that length.

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
- a simple live/sample source selector;
- a bottom strip of named mode recipes for quick direction changes;
- an explicit blend control for loop-wrap smoothing;
- a 16-cell sequence editor for repeatable stutters and replay gestures;
- visual explanations of action bias.

That tradeoff matters. This plugin is supposed to spark ideas in a DAW session,
not ask the user to become a scheduler debugger.

## Sequence editing

The sequence lane is the deterministic path. With the factory default
`Density = 0`, an empty sequence passes dry audio. Raising `Density` above zero
enables the macro-driven probabilistic chooser when no cells are programmed.
`Dub` is separate: it gives the dub echo action its own random per-block chance
so snare-style throws can appear without raising general mutation density.
When any cell is enabled, the sequence repeats over 16 block boundaries: empty
cells pass dry audio and enabled cells force their recipe.

The initial recipes are intentionally musical rather than implementation-shaped:
`Ratchet`, `1/2 beat`, `1 beat`, `2 beat`, `Reverse`, `Smear`, `Slip`, and
`Dub`.
The beat recipes replay that much source material from the rolling input buffer,
and hold for that musical duration across the global block grid. A cell can
therefore be made into a two-beat stutter without changing the sequence grid.

## Transition smoothing

The mutation engine is allowed to jump to a completely different slice at a
block boundary, but the output should not hard-click when that happens. The
implementation therefore uses a short boundary crossfade instead of trying to
solve the problem with full-plugin oversampling.
