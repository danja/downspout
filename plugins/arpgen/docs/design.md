# arpgen design

## Musical model

Chord mode uses meter-relative capture slices rather than a fixed number of
quarter notes. In 7/8, for example, a half-bar slice is 7/16 long. Note-ons are
collected during a slice and become the source at the next slice boundary. An
empty slice retains the previous chord, allowing deliberate rests upstream.
At initial play start the first arriving notes prime the source immediately so
the first bar is audible.

Scale mode runs continuously while transport is playing. With no held input it
uses the selected key's tonic in the octave near middle C as an internal
register anchor. Held input notes temporarily replace that fallback: each is
mapped to the nearest note in the configured key and scale, with downward
resolution on exact ties. `Scale run` emits consecutive scale degrees, while
`Triad` and `Seventh` emit diatonic degree stacks. Multiple held notes
contribute a sorted union, so close anchors do not duplicate MIDI pitches.

Changing source material restarts traversal at its directional endpoint. This
makes each chord or newly held scale shape articulate clearly. Up/down and
down/up use a `2N - 2` cycle, avoiding repeated top and bottom notes.

Discrete UI controls open menus for direct selection. The mode switch and Pass
Input remain explicit buttons, and selector values can still be adjusted with
the mouse wheel.

## Transport and MIDI mapping

- DPF BBT time is converted to absolute quarter-note position using both
  `beatsPerBar` and `beatType`.
- Grid events are scheduled inside each process block from host BPM and sample
  rate. The host transport remains the sole clock authority.
- Stop, rewind, and large position jumps emit a note-off before resynchronizing.
- Stop clears held scale-mode input and incomplete chord capture; Scale mode
  resumes from its internal tonic anchor, while the last completed Chord-mode
  latch remains available when playback resumes.
- One generated note is active at a time. Gate controls its fraction of the
  selected step duration.
- Output channel `0` follows the last note input channel; values `1..16` force
  that MIDI channel.
- Parameter automation is host-persistent. Captured and held notes are live
  performance state and are intentionally not serialized.
