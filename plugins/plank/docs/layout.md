# Plank Launchpad layout

How Plank uses a Novation Launchpad in programmer mode: the 8 x 8 pad grid, the
row of function buttons along the top, and the column along the right-hand side.
Everything here is read from `plank_core.cpp` and `plank_params.hpp`. For the UI
panel see the README.

```
   91   92   93   94   95   96   97   98   99          top row (CC)
  oct- oct+ mrp- mrp+ ltch drv+ res+ LED  panic

 +----+----+----+----+----+----+----+----+            +----+
 | 81 | 82 | 83 | 84 | 85 | 86 | 87 | 88 |  row 7     | 89 |  whole tone
 | 71 | 72 | 73 | 74 | 75 | 76 | 77 | 78 |  row 6     | 79 |  blues
 | 61 | 62 | 63 | 64 | 65 | 66 | 67 | 68 |  row 5     | 69 |  pentatonic major
 | 51 | 52 | 53 | 54 | 55 | 56 | 57 | 58 |  row 4     | 59 |  mixolydian
 | 41 | 42 | 43 | 44 | 45 | 46 | 47 | 48 |  row 3     | 49 |  dorian
 | 31 | 32 | 33 | 34 | 35 | 36 | 37 | 38 |  row 2     | 39 |  minor
 | 21 | 22 | 23 | 24 | 25 | 26 | 27 | 28 |  row 1     | 29 |  major
 | 11 | 12 | 13 | 14 | 15 | 16 | 17 | 18 |  row 0     | 19 |  chromatic
 +----+----+----+----+----+----+----+----+            +----+
   A    B    C    D    E    F    G    H                side (CC)
 string (column)
```

## The grid

Pads send note-on and note-off. Note number is `(row + 1) * 10 + (column + 1)`, so
`11` is bottom-left and `88` is top-right.

- **Column = voice.** Columns A to H are eight independent monophonic voices.
- **Row = scale degree.** Row 0 is the bottom and the lowest pitch. The pitch of a
  pad depends on `Scale`, `Root`, `Octave`, `Rotate`, and on `Spread` and `Stride`,
  which tune the columns against each other. The standalone UI labels each pad with
  its note, so use that to see what a given layout plays.
- **Press** plucks the voice at that row. Pressing another row in the same column
  re-strikes the voice at the new pitch.
- **Release** lets the voice go through its release stage, unless `Latch` is on.
- With `Latch` on, a released pad keeps ringing. Pressing a sounding pad again
  silences it immediately.
- Pad input is consumed and never echoed back, because a note-off sent to a
  Launchpad in programmer mode reads as an LED-off command.

## Top row

Buttons `91` to `99`, left to right. They act on press; the release is ignored.

| CC | Button | Action | Step | Limits |
|---|---|---|---|---|
| 91 | Octave down | `Octave` - 1 | 1 octave | -2 |
| 92 | Octave up | `Octave` + 1 | 1 octave | +3 |
| 93 | Morph down | `Morph` - 0.06 | 6% | 0 |
| 94 | Morph up | `Morph` + 0.06 | 6% | 1 |
| 95 | Latch | toggle `Latch` | - | - |
| 96 | Drive up | `Drive` + 0.05 | 5% | 1 |
| 97 | Resonance up | `Resonance` + 0.05 | 5% | 1 |
| 98 | LED feedback | toggle `LED` | - | - |
| 99 | Panic | stop everything | - | - |

Notes:

- **Octave** shifts the whole grid, so held voices follow it to the new pitch.
- **Morph** blends from the oscillator pair into the wavetables. On the resonator
  engines it is the exciter hardness instead, so 93 and 94 soften and harden the
  strike.
- **Latch off** also releases every held voice, so toggling it is a way to let
  everything go.
- **Drive up** and **Resonance up** only go up. There is no matching down button,
  so bring them back from the plugin UI or the host. They stop at their maximum
  rather than wrapping.
- **LED feedback off** stops all LED updates. The grid and buttons keep whatever
  they last showed, which can be stale, so turn it back on from the UI or `98`.
- **Panic** (the logo button) releases every voice, re-sends the programmer-mode
  SysEx and clears the LEDs.

## Side column

Buttons `19` to `89`, bottom to top. Each selects a scale directly and the chosen
one lights pink:

| CC | Scale |
|---|---|
| 19 | Chromatic |
| 29 | Major |
| 39 | Minor |
| 49 | Dorian |
| 59 | Mixolydian |
| 69 | Pentatonic Major |
| 79 | Blues |
| 89 | Whole Tone |

This is a performance shortlist. The `Scale` selector in the UI and in the host
reaches all 24 scales; if one of those is chosen, no side button is lit.

## What the LEDs show

**Pads.** A voice lights its whole column by level, and the pad it is playing is
white.

| Level | Colour |
|---|---|
| silent | off |
| up to 25% | green |
| over 25% | yellow |
| over 55% | orange |
| over 85% | red |
| the struck pad | white |

**Top row.**

| CC | Colour |
|---|---|
| 91, 92 | blue |
| 93, 94 | purple |
| 95 latch | yellow when on, dim when off |
| 96 | orange |
| 97 | cyan |
| 98 LED | dim, always (it does not show its state) |
| 99 panic | red |

**Side column.** The selected scale pink, the rest dim.

LED changes are sent only when something differs, plus a full refresh about every
0.75 seconds so a Launchpad that missed an update recovers.

## Channels and MIDI

- LED messages use channel 1: note-on for pads, CC for the top and side buttons.
- Musical MIDI output is off by default. With `MIDI` on, a pluck sends a note-on
  and a release a note-off on `Base Ch` (default 4), clear of the LED channels.
- With `Pass` off, other input MIDI is not forwarded.

## Not on the Launchpad

Everything else (`Engine`, `Exciter`, `Spread`, `Stride`, `Root`, `Rotate`,
`Strike`, `Damp`, `Material`, the filter, envelopes, LFOs and effects) is
reachable only from the plugin UI or host automation. A good play setup is to
choose those in the UI, then perform with the grid, side column and `93`/`94`.
