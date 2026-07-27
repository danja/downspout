# Lifeform

`Lifeform` is a Launchpad-oriented MIDI generator based on Conway's Game of
Life. The 8x8 Launchpad grid is the Life board, each musical beat advances one
generation, and living cells emit MIDI before the board evolves.

## Controls

- Click a cell in the UI, or press the matching Launchpad grid pad, to flip it.
- `Run` starts or stops beat-synced evolution.
- `Step` advances one generation immediately.
- `Random` fills the grid using the `Density` control.
- `Clear` empties the board.
- `LED` enables Launchpad feedback.
- `Pass` forwards unhandled input MIDI when enabled; it is off by default.
- `Scale`, `Root`, `Output`, and `Emit` choose how cells become notes.
- `Mutation` adds a small amount of random cellular drift.

## Launchpad Controls

The grid uses Novation programmer-mode note numbers, where note `11` is the
bottom-left cell and note `88` is the top-right cell.

Top-row buttons:

- `91`: run/stop
- `92`: step
- `93`: randomize
- `94`: clear
- `95`: next seed pattern
- `96`/`97`: mutation down/up
- `98`: density up
- `99`: panic; stop, clear cells, re-enter programmer mode, and clear Launchpad LEDs

Side buttons `19` through `89` load seed patterns.

## MIDI Mapping

Each beat emits MIDI from the current board first, then calculates the next
Game of Life generation. That means the notes you hear belong to the visible
pattern before it evolves.

In `Voices` mode, low, middle, and high rows are sent to adjacent MIDI channels
starting from `Base Channel`. The default base channel is `4`, because
Launchpad programmer-mode LED updates use MIDI channels 1-3 for static,
flashing, and pulsing colours. In `Single` mode all notes use the base channel.
In `Drums` mode living cells emit a compact GM-style drum palette.

`Emit` defaults to `Lean`, which limits dense generations to eight musical
note-ons per beat so host transport playback does not receive a large burst of
simultaneous MIDI. `Full` restores the original behavior where every living
cell emits a note on each generation.

Melodic pitch mapping:

- column chooses the main scale degree;
- odd rows add two scale degrees;
- each pair of rows shifts octave;
- `Root` and `Scale` choose the note set.

Velocity comes from the `Velocity` control, with small lifts for higher rows,
the current generation column, and newly born cells. Drum mode uses the palette
`36, 38, 42, 46, 45, 50, 39, 51`.

The musical core has no audio I/O. Route its MIDI output to an instrument track, and
route the same MIDI output back to the Launchpad if you want hardware LEDs.
`Pass` is off by default so unrecognised controller note/CC input does not leak
into the instrument route. Enable it only when you intentionally want unhandled
input MIDI to continue downstream.

## LED Mapping

Grid LEDs use static programmer-mode colours:

- dead cells: off
- newly born live cells: white
- live rows 0-1: blue
- live rows 2-4: green
- live rows 5-6: orange
- live row 7: purple

Side buttons show seed selection: the selected seed is pink, other seeds are
dim. Top buttons show their action groups, with the run button turning yellow
when stopped and the logo/panic button red.

`Panic` sends the same programmer-mode SysEx used by the `padseq` LV2 plugin,
then sends a bulk Launchpad LED clear message covering the 8x8 grid, side
buttons, top buttons, and logo.

See [docs/design.md](docs/design.md) for implementation notes.

## Host compatibility

The portable core has no audio I/O. The DPF/VST3 wrapper exposes a silent
stereo output bus for compatibility with hosts that reject event-only plugins
with no audio outputs, and clears both channels every block.
