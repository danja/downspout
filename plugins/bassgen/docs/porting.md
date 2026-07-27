# bassgen porting notes

Source plugin: `~/github/flues/lv2/bassgen`

Notable source concerns to preserve:

- MIDI note generation
- host transport sync
- rewind-aware playback reset
- persisted pattern state
- variation behavior across loop boundaries

Current `downspout`-specific port note:

- the VST3 wrapper advertises a silent stereo audio output while continuing to
  generate MIDI normally. This is an Ableton compatibility experiment: the
  portable core remains MIDI-only, and the DPF shell clears both output
  channels on every process block so the added bus cannot emit stale or
  unintended audio. Native Linux and cross-built Windows bundles both report
  one stereo audio output plus the original MIDI input/output buses and pass
  all 47 Steinberg validator tests; real Ableton validation is still pending;
- the portable core now persists a normalized `Meter` in pattern state and
  derives pulse accents, pickups, and longer landmark note lengths from it, so
  `bassgen` no longer treats every bar like a flat quarter-beat grid even
  though its genre vocabulary is still broad rather than folk-specific;
- the wrapper now exposes explicit `Style` modes for `Auto`, `Straight`,
  `Reel`, `Waltz`, `Jig`, and `Slip Jig`, and those modes are implemented in
  the portable pattern generator rather than only as UI labels;
- incoming MIDI influence is modeled in the portable engine. DPF only enables
  MIDI input and adapts host `MidiEvent`s into core `InputMidiEvent`s.
  `Input Match` can require one exact channel/note, any note on the listen
  channel, or any note. `Input Sensitivity` scales the bipolar follow/dodge
  response. In channel or any-note matching, matched input pitch is retained so
  injected follow notes can become a fifth or octave companion above a guide
  line such as Ground. Channel 10 preserves the exact listen-note trigger use
  case, while channel 1 note-ons are treated as musical guides and steer
  follow-injected notes toward bass-register pitch classes related to the
  incoming note;
- Jazz is implemented as an appended genre value for state compatibility. It
  adds ii-V-I-turnaround roots, walking beat anchors, explicit chord-role
  targets, dominant color choices, and chromatic approach/enclosure behavior.
  Jazz color scales were appended after the existing scale IDs: Altered,
  Half-Whole Diminished, Whole-Half Diminished, Bebop Dominant, Bebop Major,
  and Bebop Minor. Explicitly selected Jazz scales remain constrained
  vocabularies, while ordinary scales use the role model for Dorian,
  Mixolydian/altered, and Major/Lydian color;
- Fugue is implemented as another appended genre value. It adds a first
  subject/answer model with tonic subject, dominant answer, short episode, and
  tonic pedal/cadence bars while preserving existing saved-state genre IDs;
- Rock is implemented as an appended genre value after Fugue. It preserves
  saved-state genre IDs while forcing beat-start anchors and riff-like
  root/fifth/octave emphasis;
- Moroder is implemented as an appended genre value after Rock. It preserves
  saved-state genre IDs while reinforcing short subdivision pulses and a
  compact tonic/fifth/octave cell. `Density`, `Hold`, `Accent`, `Color`,
  `Scale`, `Register`, `Subdivision`, and `Seed` still shape holes, note
  length, velocity, brightness, pitch vocabulary, range, pulse grid, and
  deterministic variation;
- the appended `Color` control is serialized in text state and exposed through
  DPF/UI. It is intentionally general rather than Jazz-only: Jazz uses it for
  dominant color intensity, Fugue uses it for leading-tone pickup behavior, and
  Rock, Moroder, and other genres use it to increase
  vocabulary-appropriate tension or motion without changing existing enum IDs.

Likely reusable source modules:

- pattern generation
- variation logic
- transport interpretation
- state serialization
