# counterpointer

`counterpointer` is an original transport-aware MIDI processor for generating a
monophonic counter-melody from the incoming MIDI pattern.

Current scope:

- capture incoming MIDI against the host transport cycle;
- generate a deterministic complementary melodic line from that captured pattern;
- expose controls for follow/counter movement, rhythm following, density,
  embellishment density, regularity, consonance, color, register, short
  randomness, and long cycle variation;
- keep the implementation portable before adding DPF/VST3 glue.

The input MIDI stream is the source pattern. There is intentionally no separate
source selector; this should behave like `cadence` and follow whatever upstream
MIDI generator or performer is routed into it.

Current status:

- portable core types, transport helpers, and engine exist;
- deterministic core tests exist;
- a first DPF-backed `counterpointer.vst3` wrapper and custom UI exist.
- scale choices now include Lydian, Phrygian, Locrian, Melodic Minor,
  Phrygian Dominant, Whole Tone, Altered, diminished, and bebop colors,
  appended after the original scale IDs so saved-state scale values remain
  stable.
- the `Color` control makes the generated counterline less strictly consonant
  and more chromatic/adventurous, especially on Jazz-capable scales.
- strict, Bach-like answering is available through existing controls: high
  `Regularity`, high `Counter`, and low random amounts preserve learned subject
  intervals and answer them around the dominant.
- `Mode` chooses the response shape. `Counterpoint` keeps the original
  monophonic counter-melody behavior. `Bass Descend` shifts the generated line
  down into a bass register and favors consonant contrary downward motion
  against rising input phrases.
- the UI reports separate `MIDI In`, `MIDI Out`, and phrase-ready status so
  host routing can be checked before the learned counter-melody starts.

Routing notes:

- with `Pass` enabled, incoming MIDI is forwarded immediately, including while
  transport is stopped or before a phrase is ready;
- Counterpointer generates its own counter-melody only after transport is
  running and a full `BARS` cycle of incoming MIDI has been captured;
- if `MIDI In` lights but `MIDI Out` does not, the plugin is receiving the send
  and is probably still learning, muted by density, or not routed onward;
- if `MIDI In` never lights, the problem is upstream host routing rather than
  the learned phrase model.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
