# Luma

Luma is a Launchpad-oriented MIDI performance generator for Downspout. It is
not a direct port of one `flues` LV2 plugin; it combines the most immediate
ideas from `arpiso`, `achord`, and `padseq` into a smaller VST3 instrument.

The 8x8 grid holds lightweight musical agents:

- rows 1-2: bass anchors, intended for `basilico` or another bass instrument;
- rows 3-4: chord pulses;
- rows 5-6: melody and arp fragments;
- rows 7-8: drum and percussion sparks on channel 10.

Tap a Launchpad pad or UI pad to toggle an agent. Luma follows host transport
when available, otherwise `Auto` clock free-runs so it can still be played in a
simple setup. The same MIDI output carries generated notes and Launchpad LED
feedback.

## Routing

In Reaper, use the same basic pattern as Gremlin's MIDImix feedback:

1. Put `luma.vst3` on a MIDI utility track.
2. Send the Launchpad MIDI input to that track.
3. Route Luma's MIDI output to the synth/drum tracks you want to drive.
4. Set the Luma track's `MIDI Hardware Output` to the Launchpad output port for
   LEDs.

In split output mode, melodic/chord agents use the base channel, bass agents
use the next channel, and drum agents use channel 10. In single output mode,
all generated notes use the base channel.

`Pass Input` is off by default. Luma consumes recognised Launchpad controls and
blocks other incoming note/CC messages so controller pads do not leak into
instrument or drum tracks. Turn `Pass` on only when you intentionally want
unhandled input MIDI forwarded with Luma's generated notes and LED feedback.

## Launchpad controls

- grid pads toggle musical agents;
- top row 1-2 change root note;
- top row 3-4 change scale;
- top row 5-6 change density;
- top row 7 clears the grid;
- top row 8 scatters a sparse pattern;
- top row 9 panics and clears;
- right side buttons set scene energy from low to high.

The first pass targets Novation Launchpad Mini Mk3 Programmer Mode note and CC
mapping. Other Launchpad-family mappings should be added behind a small mapping
layer rather than mixed into the generator core.

## Build

Luma is enabled by default with `DOWNSPOUT_BUILD_LUMA=ON`.

```bash
cmake -S ../.. -B ../../build -DDOWNSPOUT_BUILD_LUMA=ON
cmake --build ../../build --target luma-vst3 downspout_luma_core_tests
ctest --test-dir ../../build --output-on-failure -R luma
```

## Implementation

The portable core handles Launchpad grid/CC input, deterministic step
scheduling, generated MIDI notes, pending note-offs, and LED feedback. The DPF
wrapper is deliberately thin around parameters, transport, and MIDI I/O.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
