# Campione Feature Requests

## Beat slicing on sample load

When loading a sample (e.g. a drum loop like `funky-drummer.wav`), offer optional
automatic beat slicing so each slice is spread across the keyboard as a separate zone.

The "Load File" prompt should present immediate treatment options:
- **Normalise** — peak-normalise the file before adding
- **Beat slice** — detect transients, split into slices, map each to successive keys
- **Crop ends** — trim silence/low-level content from head and tail

Note: `slice_zone` (equal or auto-detected slices) is already available via the MCP
server and the UI zone row. What is still missing is an automatic prompt at load time.

## ~~Per-zone envelopes and filters~~ ✓ Implemented

Per-zone ADSR envelope and biquad filter are fully implemented:

- Attack, Decay, Sustain, Release — rotary knob controls in the zone DSP panel
- Filter type (LP / BP / HP / Notch), frequency, and Q — rotary knobs
- Pan — rotary knob, −1 (full left) to +1 (full right)
- All DSP parameters persisted in zone state and in Turtle patch files
- Editable via the UI knob panel and via the `update_zone_dsp` MCP tool
