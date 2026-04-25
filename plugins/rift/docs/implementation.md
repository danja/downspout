# rift implementation

## Architecture

`rift` follows the normal `downspout` pattern:

- portable core in `include/` and `src/`;
- text state serialization;
- thin DPF VST3 wrapper;
- custom NanoVG UI;
- deterministic core tests.

## Transport behavior

Unlike `e-mix`, `rift` does not try to keep mutating when host transport is
unavailable or stopped. In that case it records the input into its rolling
buffer but outputs dry audio.

This is deliberate. For a buffer effect with disruptive actions, stopped-host
surprises are usually the wrong default.

## Buffer model

- the core stores a rolling interleaved buffer sized for the current sample
  rate, channel count, and memory requirement;
- slice playback always reads from the past, never from future/current samples;
- input is written into the buffer after output for that frame is rendered.
- when the selected block changes, the core crossfades from the previous block
  to the new one for a short fixed window so large read-head jumps do not click.
- when a slice loops back to its start, the core can also crossfade the tail
  into the next pass, controlled by the user-facing `Blend` parameter.

## Performance gestures

- `Hold` freezes the current block action and source slice;
- `Scatter` forces several upcoming blocks into mutation;
- `Recover` clears scatter pressure and forces several dry blocks.

These are intentionally simple to reason about in host automation and from the
custom UI.
