# Plank

`Plank` is an eight-voice string synthesizer derived from [Plinky](https://plinkysynth.com),
played from a Novation Launchpad grid. Plinky is a touch instrument where eight
fingers sound eight strings; the Launchpad grid is eight columns wide, so Plank
gives each column its own string and each row a scale degree.

The result is that Plinky's polyphony is directly playable rather than hidden
behind a round-robin allocator: hold six pads and you have six strings, each
keeping its own pitch and timbre, exactly as on the hardware.

## Controls

- Click a pad in the UI, or press the matching Launchpad grid pad, to pluck its
  column. The struck cell lights white and the rest of the column takes on that
  string's level colour.
- The `ROW PITCH` readout under the grid spells out the note each row plays, so
  the mapping is legible without consulting a manual.
- `Scale`, `Root` and `Octave` define the ladder the rows play.
- `Morph` crossfades from the polyBLEP oscillator pair into the band-limited
  wavetables, acting as a single continuous timbre axis.
- `Latch` holds strings after the pad is released. Tap a held cell again to
  silence it.
- `Panic` stops everything, resets the strings and clears the Launchpad.

## Launchpad Controls

The grid uses Novation programmer-mode note numbers, where note `11` is the
bottom-left cell and note `88` is the top-right cell. Row 0 is the bottom
hardware row, matching the `padseq` reference described in
[docs/launchpad.md](../../docs/launchpad.md).

Top-row buttons:

- `91`: octave down
- `92`: octave up
- `93`: morph down
- `94`: morph up
- `95`: latch on/off
- `96`: drive up
- `97`: resonance up
- `98`: LED feedback on/off
- `99`: panic; stop every string, re-enter programmer mode, clear the LEDs

Side buttons `19` through `89` select a performance scale: Chromatic, Major,
Minor, Dorian, Mixolydian, Pentatonic Major, Blues, Whole Tone.

## MIDI Mapping

Musical MIDI is optional and off by default. With `MIDI` enabled, plucking a pad
emits a note-on for the row's pitch and releasing it emits a note-off, on
`Base Channel`. The default base channel is `4`, because Launchpad programmer-mode
LED updates use MIDI channels 1-3 for static, flashing and pulsing colours.

`Pass` is off by default, so unrecognised controller input does not leak
downstream. Enable it only when you intend unhandled input MIDI to continue.

## LED Mapping

Grid LEDs use the static programmer-mode channel. Each string's cells take a
level ramp so a held note is visible across its column, and the cell currently
sounding reads white:

- silent: off
- low: green
- rising: yellow
- high: orange
- very high: red
- struck cell: white

Side buttons show the selected scale in pink, the rest dim. Top buttons are
coloured by action group, with latch and LED showing their state, and the
logo/panic button red.

`Panic` sends the same programmer-mode SysEx used by the `padseq` LV2 plugin,
then a bulk Launchpad LED clear message covering the grid, side buttons and top
buttons, followed by ordinary three-byte LED-off messages. The ordinary messages
are there because some VST3 hosts filter SysEx.

## Sound

The engine is a reimplementation of Plinky's voice architecture in portable C++,
not a port of its Cortex-M4 code:

- two 32-bit phase oscillators, summed as a polyBLEP saw pair with the second
  inverted, which is what hollows the tone out as `Interval` shrinks;
- `Morph` blending that pair into seventeen band-limited wavetables read at a
  quarter-cycle offset;
- Plinky's two-pole resonant filter, driven by the second envelope;
- two attack/decay/sustain/release envelopes;
- Cytomic-style dynamic parameter smoothing;
- a tape-style delay, a shimmer reverb, mid/side width and a soft output stage.

`docs/scales.md` is the reference for the scale list and its ordinals.

See [docs/design.md](docs/design.md) for implementation notes, including where
Plank deliberately departs from Plinky.

## Host compatibility

The core has no audio I/O of its own beyond rendering. The DPF/VST3 wrapper
declares two stereo outputs, zero inputs, MIDI in and out, and requests host
time position so the delay can follow the tempo.
