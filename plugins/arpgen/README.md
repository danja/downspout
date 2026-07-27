# arpgen

`arpgen` is an original transport-synced MIDI arpeggiator with two source
modes:

- **Chord** captures incoming note-ons into quarter-, half-, or whole-bar
  slices and arpeggiates the most recently completed non-empty slice.
- **Scale** treats held notes as register anchors, snaps them to a selected
  scale, and derives scale runs, triads, or sevenths across one to four octaves.

Both modes share quarter through thirty-second note rates, including eighth-
and sixteenth-note triplets, plus up, down, up/down, and down/up traversal.
Alternating orders do not repeat their endpoint notes.

See [docs/design.md](docs/design.md) for the musical and transport decisions.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
