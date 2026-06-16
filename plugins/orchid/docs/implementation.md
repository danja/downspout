# Orchid implementation notes

`orchid` is an original transport-aware audio effect. The first pass implements
the conservative MVP from `docs/orchid-idea.md`:

- stereo audio input/output;
- pass-through when host transport is stopped or BBT data is unavailable;
- rolling input buffer;
- mono normalized-autocorrelation detector;
- period-aligned stereo loop capture;
- beat-grid hold duration;
- release crossfade and capture cooldown;
- text-serialized parameters;
- read-only status parameters for host/UI feedback;
- a custom NanoVG UI with parameter sliders and live detector status.

The processor has two capture timing modes. `Immediate` captures as soon as the
detector has a stable voiced window. `Grid` stores the stable loop candidate and
starts the hold at the next selected grid boundary. In both modes, hold length
is transport-grid based.

Buffered "catch-up" return is intentionally not implemented. The first pass
crossfades back to live input after a hold.

The UI is intentionally direct: all MVP controls are sliders, and the detector
panel mirrors the processor's status outputs. It is meant for DAW iteration on
the DSP behavior before adding more performance-oriented gestures.

The detector runs on a decimated mono analysis stream near 12 kHz while loop
capture and audio output stay at the host sample rate. This keeps the
autocorrelation scan out of full-rate audio territory and avoids large CPU
spikes when a voiced region becomes eligible for capture.
