# rift implementation

## Architecture

`rift` follows the normal `downspout` pattern:

- portable core in `include/` and `src/`;
- text state serialization for parameters and the sequence lane;
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

## Sequence state

The UI stores a 16-cell sequence under the plugin-local `sequence` state key.
Each cell stores a recipe id. The DSP wrapper deserializes that state and passes
it to the portable engine for live input and sample-backed processing.

An empty sequence preserves the original probabilistic action chooser. If any
cell is enabled, the sequence repeats over block boundaries. Empty cells become
explicit dry/pass cells and enabled cells force their action recipe. Beat-window
recipes such as `2 beat` override the source slice length and hold the active
recipe for the matching musical duration while the global block grid still
decides the underlying boundary resolution.

## Sample source

The sample-source implementation adds:

- a portable `SampleSource` model for already-decoded interleaved PCM;
- `SamplePlayback` source routing for live input, sample input, or both;
- beat-position mapping from host transport to source frames;
- plugin-local WAV loading for RIFF/WAVE PCM and 32-bit float files;
- DPF file-browser support for selecting WAV files from the custom UI;
- deterministic tests that render synthetic in-memory samples through the
  existing pass and mutation paths;
- deterministic tests for generated 16-bit WAV loading.

The DPF wrapper exposes a host-visible `Source` parameter for DAW testing:

- `Live` keeps the original live-input behavior;
- `Sample` feeds a loaded WAV file into `rift`, falling back to the built-in
  four-beat synthetic loop when no file is loaded;
- `Live + Sample` mixes live input with the loaded/fallback sample before the
  rolling buffer.

The UI has a `Load WAV` control that stores the selected path in the
`sample_path` state slot. The DSP wrapper decodes the file outside the audio
callback and publishes an immutable sample snapshot to the processor. Current
file support is intentionally narrow: RIFF/WAVE PCM or 32-bit float files. MP3,
AIFF, FLAC, and compressed WAV are rejected rather than guessed.

`rift` is built with DPF `USE_FILE_BROWSER` enabled so `Load WAV` can fall back
to DPF's file browser when the host does not provide a state-file picker.

Loaded files are mapped by the `Sample Beats` parameter, which defaults to four
beats. This means a two-second break can still be treated as one four-beat bar
at the host tempo.

The sample path intentionally feeds the generated source frames into the normal
`AudioBlock` input path. That means pass-through, history capture, scatter,
recover, slice selection, and crossfades continue to use the existing engine
logic.

When sample-only mode is selected and host transport cannot be mapped, the
generated source is silence. This preserves the transport-gated behavior that
`rift` already uses for disruptive processing.

This batch does not detect transient chop points. `Chop` is currently a
grid-relative stutter control: it shortens the source slice used by repeat,
reverse, smear, and slip actions so a block can retrigger a smaller fragment.
Transient-aware chop point detection should be added after more DAW testing
with real breaks.

## Performance gestures

- `Hold` freezes the current block action and source slice;
- `Scatter` forces several upcoming blocks into mutation;
- `Recover` clears scatter pressure and forces several dry blocks.

These are intentionally simple to reason about in host automation and from the
custom UI.
