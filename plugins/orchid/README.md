# Orchid

`orchid` is a transport-aware voiced freeze/hold audio effect. It listens for
stable pitched material, captures a short period-aligned loop, and holds it on
the host grid before crossfading back to live input.

The implementation follows the repository's standard shape: portable C++ core,
text state serialization, deterministic core tests, thin DPF wrapper, and custom
NanoVG UI.

## Controls

- `Mode`: immediate or grid-aligned capture.
- `Grid`: transport division used for grid capture and hold timing.
- `Hold`: number of grid steps to sustain the captured loop.
- `Retrigger`: how readily a stronger candidate replaces an armed or active
  loop.
- detector and status controls expose pitch, confidence, and capture state back
  to the UI.

## Build Notes

`plugins/orchid` is wired into the root CMake build, local install script, and
release packaging as `orchid.vst3`.
