# Ensemble patch guide

This note describes practical DAW routing for using the Downspout generators as
a coherent ensemble. The examples assume separate tracks with MIDI sends, but
the same ideas apply in any host that can route MIDI between plugins.

## Basic roles

- `DrumGen` -> `DrumKit`
  Use DrumGen as the time anchor. Route its MIDI output to DrumKit, usually on
  channel 10.
- `Ground` -> `Basilico`
  Use Ground as the long-form low part when you want section shape, pedal
  points, and cadences. Ground keeps generated notes inside a guarded bass
  register lane, so `Register` and `Register Arc` shape lift without turning the
  part into wide octave jumping.
  Use `Dub` style for sparse low-weight parts and `Jazz` style for walking bass.
- `BassGen` -> `Basilico` or another bass instrument
  Use BassGen as the riff/continuo voice. If Ground is also active, keep one
  part lower and simpler so the two bass generators do not fight.
- `MelGen` -> lead instrument
  Use MelGen for the primary melodic subject or top line. Canticle works well
  here when the line should stay readable instead of hybrid or glitchy.
- `Counterpointer` -> second melodic instrument
  Send MelGen or BassGen MIDI into Counterpointer, then route Counterpointer to
  a separate instrument. Disable `Pass` when you want only the generated answer
  on that track.
- `Cadence` -> chord instrument
  Send MelGen, BassGen, or Ground MIDI into Cadence. Route Cadence output to a
  polyphonic synth such as Canticle or Floozy. Use Canticle for stable keys,
  reed, pad, pluck, or glass support; use Floozy when the chord sound should be
  more synthetic and physical-model-like. Disable `Pass` when the source part
  should not also play through the chord sound.

## Learning and timing

- Start playback before judging Counterpointer or Cadence. Both need to hear a
  complete learned cycle before their generated output is fully meaningful.
- Match cycle lengths deliberately. For a compact sketch, use 1 or 2 bars on
  BassGen/Cadence/Counterpointer. For long-form writing, let Ground handle 16 or
  32 bars while the other generators use shorter cycles.
- Hit `Relearn` or the equivalent action after changing upstream routing,
  source scale, phrase length, or meter.
- If a learned plugin sounds silent, check the source route first. In
  Counterpointer, `MIDI In` should light before `MIDI Out` can be expected.

## MIDI pass-through hygiene

- Leave `Pass` off on generated/learned support tracks when the source should
  not double the destination instrument. This is especially useful for Cadence
  and Counterpointer.
- Luma and Lifeform block unhandled input by default, so Launchpad/control notes
  do not accidentally trigger a downstream drum or synth track. Enable `Pass`
  only for deliberate MIDI forwarding.
- Gremlin Driver passes input by default for the normal
  `source -> Gremlin Driver -> Gremlin` chain. Turn `Pass Input` off if it
  should only emit modulation CCs, action notes, and patch-randomization bursts.
- M-Mix is different: it is a MIDI gate/effect, so passing or muting incoming
  notes is its main job rather than an auxiliary routing option.

## Guided bass layers

Use this when Ground should own the form but BassGen should add a second
instrument above it:

1. Route `Ground` MIDI to a low bass instrument.
2. Also send `Ground` MIDI to `BassGen`'s MIDI input.
3. Route `BassGen` MIDI to a separate low-mid pluck, muted bass, or continuo
   instrument.

Useful BassGen settings:

- `Input` set to `Channel`.
- `Listen Ch` set to Ground's output channel.
- `Follow/Dodge` positive for companion notes, negative for avoidance.
- `Input Sens` around 35-60 for gentle interaction, higher for a line that
  closely follows Ground's pitch contour.
- Keep BassGen `Register` above Ground, or use a brighter instrument, so the
  two parts read as guide plus response rather than doubled bass.

## Fugue/classical patch

Suggested routing:

1. `DrumGen` in `Fugue` genre -> `DrumKit`
2. `Ground` -> low `Basilico`
3. `BassGen` in `Fugue` genre -> second bass/continuo sound
4. `MelGen` -> lead sound
5. MIDI send from `MelGen` to `Counterpointer` -> second Canticle or Floozy lead
6. MIDI send from `MelGen` or `Ground` to `Cadence` -> Canticle chord/pad sound

Useful settings:

- Ground: high `Sequence`, high `Cadence`, moderate `Density`. This creates the
  longer subject, answer, pedal, and cadence arc.
- BassGen: `Fugue` genre, major or harmonic minor scale, low-to-moderate
  `Color` for clearer leading-tone behavior.
- MelGen: `Call Answer`, high `Structure`, low `Leap`, low `Rest`. This creates
  a compact subject and dominant answer.
- Counterpointer: high `Regularity`, high `Counter`, low short/long random.
  Use high `Color` only when you want inversion or more adventurous answering.
- Cadence: major, natural minor, or harmonic minor scale, high `Color`.
  Triads gives clearer suspended-dominant support; Sevenths or Extended gives
  denser harmonic color. Keep `Arpeggio` low for organ-like support, raise it
  for broken continuo.

## Jazz patch

Suggested routing:

1. `DrumGen` in `Jazz` genre -> `DrumKit`
2. `BassGen` in `Jazz` genre -> `Basilico`
3. `MelGen` with Dorian, Mixolydian, Bebop, Altered, or diminished scales -> lead
4. Send the bass or melody to `Cadence` -> chord instrument
5. Optional: send MelGen to `Counterpointer` for a second horn-like line

Useful settings:

- BassGen: raise `Color` for more altered/diminished dominant behavior.
- Ground: choose `Jazz` style when it should own the walking bass role; choose
  `Dub` style for a sparse pedal-heavy low part under busier drums or chords.
- Cadence: `Color` high, `Chord Size` set to Sevenths or Extended, `Spread`
  around the middle, `Arpeggio` to taste.
- MelGen: use the improvisation/randomness controls sparingly at first, then
  raise `Color` and variation once the harmonic loop is stable.

## Minimal sketch patch

Use this when the full ensemble is too dense:

1. `DrumGen` -> `DrumKit`
2. `Ground` or `BassGen`, not both -> `Basilico`
3. `MelGen` -> lead instrument
4. Send `MelGen` to `Cadence` -> chord instrument

This gives drums, bass, melody, and harmony with the fewest moving parts. Add
Counterpointer only after the main melody has a clear shape.

## Arrangement tips

- Avoid doubling the same register. Put Ground low, BassGen slightly higher or
  mute one of them, Cadence mid/high, and Counterpointer above or beside MelGen.
- If Ground still feels too animated for a bass anchor, lower `Register Arc`
  before lowering `Motion`; the register guard will keep the line in lane, but
  a smaller arc gives a flatter section shape.
- Let one plugin own the form. For classical/fugal sketches, Ground is the best
  form owner. For short loops, BassGen or MelGen can own the phrase.
- Use Cadence as support, not the source of all motion. If the chord track gets
  too busy, lower `Comp`, `Arpeggio`, or `Color` before changing the source.
- Use DrumGen Fugue sparingly. Its sparse pulse is useful for testing and
  metrical grounding, but a real Bach-like texture may work better with drums
  muted.
- Save stable DAW routing templates. The generated plugins are deterministic,
  but learned processors need the same upstream routing to recreate the same
  musical result.
