# Retune

MIDI effect that retunes any note stream to a Scala (`.scl`) scale with one pitch
bend and one output channel per note, so melgen, arpgen, cadence and the other
generators can drive microtonal synths. The idea comes from
[Subsequence](https://subsystem.co/subsequence) (see `docs/subsequent.md`); the code
is original.

| Control | Meaning |
|---|---|
| Load .scl | Scala file, cents or ratio lines. No file means 12-TET. |
| Root note | MIDI note that plays the scale's 1/1 |
| Bend range | Semitones; must equal the receiving synth's pitch bend range |

- **Mapping:** the note's offset from the root picks a degree (the scale repeats
  every period, not necessarily an octave); the nearest 12-TET key becomes the
  carrier note and a 14-bit bend corrects it. A correction beyond the bend range,
  or a carrier outside 0-127, drops the note.
- **Channels:** each held note uses one of output channels 2-16, longest-idle first.
  When all 15 are busy the oldest note is stolen. Channel 1 is left free.
- **Other MIDI:** CC and program change go to every output channel; CC 120/123
  release every note; poly aftertouch follows its note. Incoming pitch bend, channel
  aftertouch and sysex are dropped.
- **State:** the `.scl` path is saved (`scale_file`), not the file contents. The
  parse happens on the host's main thread and reaches the audio thread as an
  immutable shared pointer.

Synths that currently follow per-channel pitch bend (2-semitone default, RPN 0 range):
`canticle`, `moka`, `floozy`, `syrinx`, `pratt`. The others are listed in `TODO.md`.

Not supported: `.kbm` keyboard maps, N-TET entry without a file, MPE zone setup
messages. The receiving synth must be in a one-channel-per-note mode.

## Build

Part of the root build (`DOWNSPOUT_BUILD_RETUNE`, default ON):

```
cmake --build build --target retune-vst3
ctest --test-dir build -R retune
```

Run `env -u DISPLAY scripts/capture-plugin-screenshots.sh retune` for the catalogue
image.
