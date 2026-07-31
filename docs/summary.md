# Downspout Plugin Capability Summary

Downspout is a collection of mostly generative and algorithmic VST3 plugins. The common architecture is a portable C++ musical/DSP core, thin DPF host wrapper, persistent text state where needed, deterministic tests, and a custom NanoVG interface. REAPER identifies the plugins below with maker `danja`.

## MIDI generators and processors

### BassGen (`bassgen.vst3`)

Transport-aware bass MIDI generator with persistent patterns, meter-aware rhythm, loop-boundary variation, and Auto, Straight, Reel, Waltz, Jig, and Slip Jig styles. Incoming MIDI can guide or suppress generated notes through configurable Follow/Dodge, channel/note matching, and sensitivity controls. Best used before a bass instrument such as Basilico.

### DrumGen (`drumgen.vst3`)

Transport- and meter-aware MIDI drum generator with pattern variation and fills. Its genre vocabulary includes Breakbeat, Amen, Jungle, and Hip Hop, making it useful for rapidly establishing rhythmic structure before DrumKit or another percussion instrument.

### MelGen (`melgen.vst3`)

Phrase-aware melody MIDI generator. It shapes contour, question/answer behavior, longer structure, and reactions to incoming musical context rather than emitting unrelated random notes.

### Ground (`ground.vst3`)

Long-form bass MIDI generator with Dub and Jazz orientations, phrase planning, and guarded bass-register output. It is suited to evolving foundational lines rather than short repeating loops.

### ArpGen (`arpgen.vst3`)

Transport-synced arpeggiator. Chord mode captures incoming chord slices; Scale mode derives scale runs, triads, or sevenths from held register anchors. Supports straight and triplet rates from quarter to thirty-second notes, one to four octaves, and up/down alternating orders.

### Cadence (`cadence.vst3`)

Transport-aware MIDI harmonizer and comping generator. It can turn incoming material into harmonic support, making it useful between a chord source and an instrument.

### Counterpointer (`counterpointer.vst3`)

Learns incoming MIDI and emits a monophonic counter-melody. Useful for generating a second melodic voice that relates to, but does not merely double, the source.

### Sidecar (`sidecar.vst3`)

MIDI phrase player for generated solo material. It supports deterministic local generation and an optional localhost coordinator mode for externally supplied phrases.

### M-Mix (`m_mix.vst3`)

Transport-aware MIDI gate that combines probabilistic P-Mix-style transitions with E-Mix-style Euclidean blocks. It rhythmically removes or passes notes before they reach an instrument.

### GremlinDriver (`gremlin_driver.vst3`)

MIDI modulation and action sequencer designed for Gremlin. Multiple lanes and triggers send controller changes, scene/action commands, randomization bursts, and optional input pass-through, synchronized either to transport or an internal clock.

### Luma (`luma.vst3`)

Launchpad-oriented performance generator. Lit grid cells become coordinated bass, chord, melody, and drum agents, with transport-clocked or free-running steps and visual LED feedback.

### Lifeform (`lifeform.vst3`)

Conway's Game of Life MIDI sequencer for Launchpad. Each beat advances a cellular generation and maps living/born cells to melodic or drum notes, with scale mapping, velocities, gates, randomization, pattern seeding, and LED feedback.

### Xoxolo (`xoxolo.vst3`)

Straightforward x0x-style drum sequencer with 11 DrumKit-oriented lanes and patterns up to 32 steps. Useful when explicit hand-programmed dance patterns are preferable to generative rhythm.

### Mixgen (`mixgen.vst3`)

Transport-synchronized automatic producer for T-Mix. Eight repeatable gain
lanes use random, low-discrepancy quasi-random, or Euclidean patterns. Route
its MIDI output to the T-Mix track; fixed CC 20-27 address strips 1-8.
T-Mix, FX-only, and Full-bus profiles can also route four configurable macros
to Loopdelay and Lightverb. CC 19 owns/releases the producer bus.

### Loopdelay (`loopdelay.vst3`)

Stereo delay and capture looper designed for `T-Mix → Loopdelay → Guardian`.
Time can run freely from 20–4000 ms or follow the host BBT from a quarter beat
through four bars. Route controller MIDI to its track to drive time with fixed
CC 30 and feedback with CC 31; either CC temporarily takes over its saved panel
value until **Release MIDI** is pressed.
It supports optional CC 19 gating, per-chain MIDI channel filtering, and MIDI
through to the next effect.

### Lightverb (`lightverb.vst3`)

Fixed-cost stereo feedback-delay-network reverb that favors low CPU use over
natural-room simulation. It works as an insert or a 100% wet send, normally in
`T-Mix → Loopdelay → Lightverb → Guardian`. Producer MIDI uses fixed CC 32 for
Wet mix and CC 33 for Space, continuing the suite's non-conflicting control
range.
It supports the same CC 19 lifecycle, channel isolation, and MIDI-through
behavior as Loopdelay.

## Instruments

### DrumKit (`drumkit.vst3`)

Stereo synthesized drum instrument with a single MIDI input and mixer-style interface. It is the natural sound source for DrumGen, Xoxolo, Luma drum output, or conventionally authored GM-style drum MIDI.

### Basilico (`basilico.vst3`)

Monophonic bass instrument with Upright, Electric, Dub, Acid, and Industrial models. It includes note priority, glide, velocity accent, model-specific filtering/drive, tempo-synced or free wobble, stereo phase/flange modulation, and an acid-style Squelch macro.

### Canticle (`canticle.vst3`)

Twelve-voice instrument covering keys, reed, pad, pluck, and glass timbres. Designed for melody, counterpoint, chords, and layered harmonic parts.

### Floozy (`floozy.vst3`)

Eight-voice hybrid physical/modulation synthesizer derived from `floozy-poly`. Suitable for expressive leads, plucks, and unusual synthetic textures.

### Gremlin (`gremlin.vst3`)

Chaotic glitch instrument with performance scenes, live and hidden parameters, macros, momentary controls, randomization actions, delay-oriented behaviors, controller feedback, and a master trim. GremlinDriver is its intended automation companion.

### Tuney VST (`tuney_vst.vst3`)

Text-to-music instrument and MIDI generator derived from Tuney 0.3.39. Focused Unicode typing or stored text is mapped through configurable alphabets, scales, and microtonal tunings, then played with seeded free-time phrasing through a simple polyphonic synth and ordinary MIDI output.

## Audio effects

### T-Mix (`t_mix.vst3`)

Eight-input mono-to-stereo mixer with level, pan, mute, solo, meters, and a
master fader. Its producer layer accepts Mixgen CC 20-27 as click-smoothed
transient gain multipliers while preserving manual faders as the saved balance.

### P-Mix (`p_mix.vst3`)

Transport-aware probabilistic stereo gate/mix processor. It creates evolving channel switching and rhythmic dropouts, useful for motion in loops, percussion, or textures.

### E-Mix (`e_mix.vst3`)

Transport-aware Euclidean stereo gate. It applies deterministic Euclidean block rhythms to audio, creating repeatable rhythmic stereo patterns.

### Rift (`rift.vst3`)

Transport-locked live/sample buffer disruptor with WAV loading, chop/stutter repeats, reverse, skip, smear, and pitch-slip actions. Best for fills, transitions, breakdown edits, and controlled glitching.

### Orchid (`orchid.vst3`)

Transport-aware voiced freeze/hold effect. It uses autocorrelation capture and grid-synchronized loop holds to turn incoming notes or textures into sustained rhythmic freezes.

### Ambo (`ambo.vst3`)

Stereo ambient processor with four rearrangeable module chains. Its Time, Spectral, Tape, Shimmer, Delay, Drive, Feedback, Mix, and Output controls cover granular smear, frozen-band motion, tape color, bright diffusion, ping-pong echoes, saturation, and dense regenerative ambience.

### PaunchLad (`paunchlad.vst3`)

Launchpad dub performance effect/instrument. Pads trigger echo throws, spring splashes, sirens, alarms, synthetic snare/crash/sub hits, lasers, thunder, rewind, bubbles, risers, horn effects, dropouts, chops, and freezes, with LED feedback and audio pass-through processing.

## Practical combinations

- Techno rhythm: Xoxolo or DrumGen -> DrumKit -> E-Mix/P-Mix -> Rift for fills.
- Acid bass: BassGen or Ground -> Basilico, using Acid model, glide, wobble, and Squelch.
- Anthem harmony: authored chords or Cadence -> Canticle; duplicate into ArpGen -> Canticle/Floozy for motion.
- Glitch lead: GremlinDriver -> Gremlin, optionally followed by Rift or Ambo.
- Breakdown atmosphere: Canticle/Floozy -> Orchid -> Ambo.
- Live dub transitions: a drum or full-mix bus -> PaunchLad.
- Automatic arrangement: route instruments into T-Mix, then route Mixgen MIDI
  to T-Mix and tune Density, Depth, Variation, and Lane Spread.
- Full producer bus: set Mixgen to Full bus and route one MIDI send to a chain
  of T-Mix, Loopdelay, and Lightverb. Match their Control channel and optionally
  require CC 19 ownership. See [Producer Control Bus v1](producer-control-bus.md).

## Host notes

Generators and MIDI effects must precede their receiving instrument in the track FX chain or route MIDI to another instrument track. Mixgen normally belongs on a control track whose MIDI send targets the T-Mix track. Transport-aware plugins need valid host tempo/play-state information. Launchpad-focused plugins remain usable without hardware where their UI exposes equivalent controls, but their intended performance feedback depends on Launchpad MIDI I/O. High Ambo feedback, stacked delay/shimmer, Rift repeats, and Gremlin randomization can become dense quickly, so conservative output trims are advisable.
