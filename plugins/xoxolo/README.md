# Xoxolo

`xoxolo` is a simple transport-synced MIDI drum pattern editor. It presents an
11-lane step grid mapped by default to `drumkit.vst3`, loops from host
transport, and emits MIDI notes on channel 10 by default.

The right panel also includes a pattern generator. Choose one of the Jazz,
Drum & Bass, House, Funk, Rock, or Latin templates, set Density and Tension,
then click **Go** to replace the visible grid with a new variation. Density
controls overall activity; Tension favors syncopated hits and end-of-phrase
fills.

The first version deliberately keeps the pattern small:

- 11 Downspout lanes, or 29 AVL-Drumkits lanes;
- 16 default steps;
- selectable `8..32` visible steps;
- `1/4`, `1/8`, and `1/16` resolution;
- per-lane MIDI note selection and preview;
- note-name presets for Downspout and AVL-Drumkits mappings.

Pattern cells, lane note numbers, and the selected note-name preset are stored
in explicit text state rather than as individual cell parameters.
Generated cells use that same pattern state, while generator settings remain
local drafting controls and are not host-automatable.

## Default MIDI map

The default `Downspout` preset mirrors `drumkit.vst3`.

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

## AVL-Drumkits preset

The `AVL-Drumkits` preset exposes all 29 entries from `docs/avl-drum-map.md`,
covering notes 36 through 64.

## Build Notes

`plugins/xoxolo` follows the standard Downspout shape: portable C++ core,
deterministic core tests, text serialization, thin DPF wrapper, and custom
NanoVG UI.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
