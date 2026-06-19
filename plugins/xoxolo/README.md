# Xoxolo

`xoxolo` is a simple transport-synced MIDI drum pattern editor. It presents an
11-lane step grid mapped by default to `drumkit.vst3`, loops from host
transport, and emits MIDI notes on channel 10 by default.

The first version deliberately keeps the pattern small:

- 11 fixed lanes;
- 16 default steps;
- 32 visible steps maximum;
- `1/4`, `1/8`, and `1/16` resolution;
- one to four bars, with unsupported bar/resolution combinations avoided;
- per-lane MIDI note selection and preview.

Pattern cells and lane note numbers are stored in explicit text state rather
than as individual automatable parameters.

## Default MIDI map

- Kick 36
- Clap 39
- Snare 40
- Crash 41
- Closed HH 42
- Low Tom 45
- Open HH 46
- High Tom 50
- Bash 51
- Cowbell 52
- Clave 53

## Build Notes

`plugins/xoxolo` follows the standard Downspout shape: portable C++ core,
deterministic core tests, text serialization, thin DPF wrapper, and custom
NanoVG UI.
