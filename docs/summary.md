# Downspout Plugin Capability Summary

Downspout is a collection of mostly generative and algorithmic VST3 plugins. The common architecture is a portable C++ musical/DSP core, thin DPF host wrapper, persistent text state where needed, deterministic tests, and a custom NanoVG interface. REAPER identifies the plugins below with maker `danja`.

## MIDI generators and processors

### BassGen (`bassgen.vst3`)

Transport-aware bass MIDI generator with persistent patterns, meter-aware rhythm, loop-boundary variation, and Auto, Straight, Reel, Waltz, Jig, and Slip Jig styles. Incoming MIDI can guide or suppress generated notes through configurable Follow/Dodge, channel/note matching, and sensitivity controls. Set **Conductor Ch** to receive Conductor's section CCs, which then steer density, accent, variation rate, and pattern resets automatically. Best used before a bass instrument such as Basilico.

### DrumGen (`drumgen.vst3`)

Transport- and meter-aware MIDI drum generator with pattern variation and fills. Its genre vocabulary includes Breakbeat, Amen, Jungle, and Hip Hop, making it useful for rapidly establishing rhythmic structure before DrumKit or another percussion instrument. Set **Conductor Ch** to receive Conductor's section CCs, steering density, variation, mutation rate, and pattern resets without manual automation.

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

### ToneWorm (`worms.vst3`)

Transport-locked MIDI melody generator that navigates a Tonnetz pitch lattice using Paterson's Worm algorithm. Six directional rules (each choosing from L120, L60, Straight, R60, R120) determine how the worm moves through pitch space, producing melodic lines with scale quantization, configurable register, and loop-boundary variation. Set **Conductor Ch** to receive CC 21–24 (density, velocity, vary, mutate). Route MIDI output to MelGen or any instrument. Randomize and Mutate actions make new rule configurations quickly.

### Lightverb (`lightverb.vst3`)

Fixed-cost stereo feedback-delay-network reverb that favors low CPU use over
natural-room simulation. It works as an insert or a 100% wet send, normally in
`T-Mix → Loopdelay → Lightverb → Guardian`. Producer MIDI uses fixed CC 32 for
Wet mix and CC 33 for Space, continuing the suite's non-conflicting control
range.
It supports the same CC 19 lifecycle, channel isolation, and MIDI-through
behavior as Loopdelay.

### Ghost (`ghost.vst3`)

Audio-triggered ghost-note generator. It listens to incoming audio (output
silent unless Audio Thru is on) and inserts MIDI ghosts quantised to the transport 16th
grid: onset accents jump to the next slot while quieter fills land on
off-16th slots with probability Density, pushed late by Drag. Drums mode
voices kick 36 and ghost snare 38 on MIDI channel 10; Notes mode walks a
pentatonic-minor ladder from Base Note on a selectable channel. Sensitivity,
Density, Velocity and Drag read Drift CC 1-4 by default. Ghosts only fire
while the transport runs. Natural partner: a dry drum bus into Ghost, with
Ghost MIDI driving DrumKit layered under the original kit.

### Sprout (`sprout.vst3`)

L-system melody generator. Pick one of seven grammars (Plant, Koch, Dragon,
Sierpinski, Cantor, Levy, Tree) or choose **Custom** and type your own rules into the
Rules field (`F; F -> F[+F]F`: start string, then rules separated by `;`), a number of
generations, a scale and a root; it plays
the rewritten string as a monophonic line locked to the transport. With **Grow every**
set it starts at generation 1 and gains a generation every N bars, so a piece thickens
over time. Output is plain MIDI, so feed any instrument; Cantor gives rhythm on one
pitch, Koch and Tree give melodic contour, and Plant gives long self-similar phrases.
Optional MIDI input (off by default): set **Pitch source** to Held (or Latched, which keeps the chord
after release) to play the pattern over the chord on its input (put Harmonic Atlas, Cadence or ArpGen in front of it), or set a
**CC channel** so Drift (CC 1-4) or Conductor (**Conductor ch**, CC 21-24) steers probability,
gate, range, generations, velocity and seed.

### Markov (`markov.vst3`)

Markov-chain melody generator. The melody is a random walk over the twelve pitch classes through a
12 x 12 transition matrix that you can see: brightness is each move's real chance and the digit is the
weight you set, and clicking a cell changes it. Eight styles fill the matrix (Stepwise, Triadic, Fifths,
Pentatonic, Blues, Chromatic, Tonic pull, Uniform), or switch **Learn** on and play a single line into
its input and it counts which note follows which. **Scale** keeps any matrix in key, **Chaos** sharpens
or flattens the favourites, **Order** 2 uses the two previous notes where it has learned them, and each
phrase restarts from the tonic so loops and renders repeat exactly. Output is plain MIDI; pair it with
Retune for microtonal Markov melodies.

### Retune (`retune.vst3`)

MIDI effect that retunes a note stream to a Scala (`.scl`) scale. Each held note is sent
on its own channel (2-16) after a pitch bend that moves the nearest 12-TET key onto the
scale degree, so put it between a generator (melgen, arpgen, cadence, ...) and an
instrument that honours per-channel pitch bend at the same range (default 2 semitones,
MPE-style). CC and program change reach every channel; incoming pitch bend and channel
aftertouch are dropped. With no file loaded it passes 12-TET through unchanged. Use it with Canticle, Moka, Floozy, Syrinx or Pratt, which bend per channel; other
instruments ignore the bend and will play 12-TET.

## Instruments

### DrumKit (`drumkit.vst3`)

Stereo synthesized drum instrument with a single MIDI input and mixer-style interface. It is the natural sound source for DrumGen, Xoxolo, Luma drum output, or conventionally authored GM-style drum MIDI.

### Basilico (`basilico.vst3`)

Monophonic bass instrument with Upright, Electric, Dub, Acid, and Industrial models. It includes note priority, glide, velocity accent, model-specific filtering/drive, tempo-synced or free wobble, stereo phase/flange modulation, and an acid-style Squelch macro.

### Canticle (`canticle.vst3`)

Twelve-voice instrument covering keys, reed, pad, pluck, and glass timbres. Designed for melody, counterpoint, chords, and layered harmonic parts.

### Floozy (`floozy.vst3`)

Eight-voice hybrid physical/modulation synthesizer derived from `floozy-poly`. Suitable for expressive leads, plucks, and unusual synthetic textures.

### Beards (`beards.vst3`)

Granular texture processor, a port of the Mutable Instruments Beads grain engine.
Stereo audio in; Position, Size, Shape, Pitch and Density shape the grains, Feedback,
Freeze, Reverb and Dry/Wet shape the result. Use it on pads, drum loops or vocals as a
send-style texture. Drift lanes default to CC 1-4 (Position, Size, Density, Dry/Wet);
Conductor drives Density, Dry/Wet and scatter on its channel and fires a grain on Reset.
Density 48-52% is silent, and Feedback plus Reverb can build up, so trim output.

### Grist (`grist.vst3`)

Granular grain delay. Stereo audio in; one grain starts every Window / Density and plays a
windowed slice of the recent input, scattered in time (Blur), direction (Reverse), pitch
(Pitch, plus Range on a Scale), length (Variance) and stereo position (Spread). Window and the
feedback Delay can follow the host tempo. Feedback runs the grains back through a delay and an
optional filter for evolving clouds. It adds latency (shown on the panel, not reported to
the host). Follow it with Resonance Garden for tuned resonances, or use it as a send texture;
reach for Beards instead when you want a freeze, reverb or Drift/Conductor control.

### Sam (`sam.vst3`)

Speech synthesiser: Software Automatic Mouth, the 1982 Commodore 64 voice, played two ways. In
**Keys** mode each MIDI note speaks the next word of the text, sung at the pitch of the key
(**Key = word** instead speaks the word the key selects); in **Transport** mode the host timeline
reads the text aloud, one word per step from a start bar, so it stays in time with the song through
tempo changes, relocations and loops. Type the text into the panel (`_` is a rest) as English or as
SAM phoneme codes. **Speed**, **Pitch**, **Mouth** and **Throat** are SAM's own voice controls, with
its six manual voices as presets. Use it as a robot lead from a keyboard or sequencer, or as a spoken
part that follows the arrangement. No audio input. SAM's source has no licence; see
`plugins/sam/third_party/sam/SAM-LICENSE.md`.

### Trajectory (`trajectory.vst3`)

Eight-voice polygon-bounce synthesiser: a point bounces inside a regular 3-12 sided
polygon and its x/y position, mixed by **X Level** and **Y Level**, is the waveform.
**Sides**, **Start Position**, **Launch Angle** and **Bounce Jitter** reshape the orbit from
stable to quasi-chaotic; a full ADSR shapes each voice. Edgy, buzzy, metallic leads, basses
and drones; triangles are brightest, high side counts mellower. Pitch follows the note
but a shape sets how far from concert pitch it lands (a square at 45 degrees is in tune).
The six oscillator controls answer CC 1-6 in order and CC 7 is Level, so Drift's four lanes
modulate Sides, Start Position, Launch Angle and Bounce Jitter without configuration.

### Fathom (`fathom.vst3`)

Four-voice drone synthesiser: a generator (five algorithms: Analog, Shaper, Sines, Table,
Cluster), a per-voice notch filter, then a delay network (Metallic or Diffuse) with a
frequency shifter in its feedback loop, so every repeat climbs or falls in pitch. Long
attack and release, **Hold** on the sustain pedal and **Glide** make it a drone instrument;
the four voices are panned apart for a wide chord. CC 1-4 are Warp, Span, Morph and Seed
(Drift's default lanes), so Drift animates the timbre unconfigured; CC 5 Fuse, 6 Field,
7 Level, 8 Notch, 9 Mix, 10 Time, 11 Form, 12 Feed, 13 Shift, 14 Basis.

### Qune (`qune.vst3`)

Pitch quantiser for a voice or any single-note audio: it tracks the pitch and moves it to the
nearest note of a chosen scale (24 scales, any root) or to MIDI notes. Put it on a vocal, bass
or lead track as an insert; to make a voice follow a melody or chord, set **Target** to MIDI
note or MIDI chord and route a MIDI track (an arpeggiator, Melgen, Cadence) to it. **Glide** off
gives the stepped, hard-tuned sound; a longer glide and a lower **Amount** nudges a performance.
**Quality** trades latency (Fast about 4 ms, Good 16-61 ms, Best about 43 ms) for smoothness
and formant control; the host is told the latency, so it compensates. One pitch at a time:
not for chords or full mixes.

### Magneto (`magneto.vst3`)

Physically informed combustion engine generator. A four-stroke cycle drives
per-cylinder digital waveguides feeding intake runners, extractors, a straight
pipe, a four-element muffler and a tailpipe. Cylinders, displacement,
compression, ignition width and **Growl** (uneven firing) set the machine;
lengths in metres and **Silencing** set the pipework; **Backfire** fires only on
overrun. A MIDI note sets engine speed, transposed down two octaves, so it can
be played from a keyboard, with velocity as engine load; note-off is ignored and
the engine holds that speed.
Controllers cover the rest — CC 1 throttle, 2 RPM, 3 silencing, 4 growl, 5 pipe,
6 turbulence, 7 output — and the first four match Drift's default lane CCs, so
Drift modulates it without configuration. Alternatively **Speed source** locks
the engine cycle to host tempo and drops to idle when the transport stops. A
**Listening position** — Cabin, Front, Rear or Exterior — mixes intake,
engine-block vibration and exhaust to stereo.

### Moka (`moka.vst3`)

Stereo modal hit-object synth with eight damped modes per voice, six instrument tables (xylophone, glockenspiel, woodblock, glass bowl, metal sheet, tube), selectable 1–12 voice polyphony defaulting to 4, and eleven factory presets including a Kalimba. Notes ramp in over the mallet contact time rather than stepping to full amplitude, so there is no click at note onset. Natural sound source for struck percussion and bell parts from DrumGen, Xoxolo, or authored MIDI.

### Gremlin (`gremlin.vst3`)

Chaotic glitch instrument with performance scenes, live and hidden parameters, macros, momentary controls, randomization actions, delay-oriented behaviors, controller feedback, and a master trim. GremlinDriver is its intended automation companion.

### Tuney VST (`tuney_vst.vst3`)

Text-to-music instrument and MIDI generator derived from Tuney 0.3.39. Focused Unicode typing or stored text is mapped through configurable alphabets, scales, and microtonal tunings, then played with seeded free-time phrasing through a simple polyphonic synth and ordinary MIDI output.

### Plank (`plank.vst3`)

Eight-string synthesizer derived from the Plinky touch synth. A polyBLEP oscillator pair morphs into seventeen synthesised band-limited wavetables, feeding Plinky's resonant two-pole filter, two envelopes, tape delay, shimmer reverb and mid/side width. Each Launchpad grid column is one string and each row a scale degree, so the eight-voice polyphony is held rather than allocated.

### Pratt (`pratt.vst3`)

Number-theoretic synthesizer and filter built from Pratt polynomials. **Synth** mode
plays eleven General MIDI voice families (piano, electric piano, organ, pluck, bass,
strings, brass, reed, flute, pad, timpani) whose spectra come from the stable
all-pole filter `H_n` at `n = (note + 1) x base`; sustain pedal, volume, expression,
pan, pitch bend and channel-10 drums are handled. **Filter** mode runs audio through
the Pratt filter, with two cascading **Index** controls (`n = A x B`), **Cutoff** and
**Filter Mix**; **Synth + Filter** does both. It has an audio input, so route a track
into it for Filter mode; in Synth mode the input is ignored.

## Audio effects

### T-Mix (`t_mix.vst3`)

Eight-input mono-to-stereo mixer with level, pan, mute, solo, meters, and a
master fader. Its producer layer accepts Mixgen CC 20-27 as click-smoothed
transient gain multipliers while preserving manual faders as the saved balance.

### P-Mix (`p_mix.vst3`)

Transport-aware probabilistic stereo gate/mix processor. It creates evolving channel switching and rhythmic dropouts, useful for motion in loops, percussion, or textures. The **Oppose** slider uses a sidechain signal on inputs 3–4: when those inputs are loud, P-Mix tends to go quieter, and vice versa.

### E-Mix (`e_mix.vst3`)

Transport-aware Euclidean stereo gate. It applies deterministic Euclidean block rhythms to audio, creating repeatable rhythmic stereo patterns.

### Bassops (`bassops.vst3`)

Four-input sidechain ducker with mid/side EQ. Inputs 3-4 carry a control signal (typically a dry kick-drum send) that drives an envelope follower; the inverted envelope ducks the main stereo signal on inputs 1-2. After ducking, a linear-phase LP/HP FIR pair splits the signal at a shared cutoff: bass stays centred in the mid channel, highs retain stereo width in the side channel. Introduces 64-sample FIR latency reported to the host. Use after bass and kick tracks to tighten low-end separation.

### Guardian (`guardian.vst3`)

Output safety processor intended for the end of autonomous signal chains. An
input gain control (±24 dB) drives the limiter harder or reserves headroom.
Look-ahead limiting with configurable attack and release catches peaks before
they reach the ceiling. A clipper shape parameter sweeps from no saturation
through gentle tanh coloring up to a near-hard clip, with the transfer curve
displayed in real time. DC removal, true-peak guard, silence detection, bypass,
and latched fault diagnostics complete the picture. The ceiling and reported
latency are broadcast to the host.

### Rift (`rift.vst3`)

Transport-locked live/sample buffer disruptor with WAV loading, chop/stutter repeats, reverse, skip, smear, and pitch-slip actions. Best for fills, transitions, breakdown edits, and controlled glitching.

### Orchid (`orchid.vst3`)

Transport-aware voiced freeze/hold effect. It uses autocorrelation capture and grid-synchronized loop holds to turn incoming notes or textures into sustained rhythmic freezes.

### Ambo (`ambo.vst3`)

Stereo ambient processor with four rearrangeable module chains. Its Time, Spectral, Tape, Shimmer, Delay, Drive, Feedback, Mix, and Output controls cover granular smear, frozen-band motion, tape color, bright diffusion, ping-pong echoes, saturation, and dense regenerative ambience.

### PaunchLad (`paunchlad.vst3`)

Launchpad dub performance effect/instrument. Pads trigger echo throws, spring splashes, sirens, alarms, synthetic snare/crash/sub hits, lasers, thunder, rewind, bubbles, risers, horn effects, dropouts, chops, and freezes, with LED feedback and audio pass-through processing.

### HelterSkelter (`helterskelter.vst3`)

Automatic wah pedal. A resonant lowpass (Q 0.5–12) whose cutoff follows the
input envelope like a touch-wah and/or a BBT-synced ADSR cycle (1, 2, 4 or 8
beats, Gate Beats high, Invert for low-then-high); Blend takes whichever opens
further. Sensitivity, Depth, Resonance and Mix read Drift CC 1-4 by default.

### Spliff (`spliff.vst3`)

Adaptive transient processor in the Spiff manner. A 3-band detector finds
transient energy and applies dynamic cuts or boosts only there, leaving the
rest of the spectrum intact. Cut tames clicks, mouth noise and harsh sticks
(last in the chain); Boost lifts drums and piano attacks (early, before
compression). Depth, Sensitivity, Decay and Mix read Drift CC 1-4 by default;
Sharpness, Decay LF/HF tilt, band splits, Trim, Delta monitoring and Bypass
complete the Spiff workflow. The detector is mono, so transients never shift
the stereo image.

### Treatment (`treatment.vst3`)

Physically modelled acoustic treatment panel. A facing sheet of surface mass in
front of a porous fill with an air gap to the wall, which is a mass-air-mass
resonator: the resonance and the absorption come from `w0 = c*sqrt(rho0/(m*D))`
and the impedance match at `r = rho0*c`, so the panel absorbs least when the
fill is too open or too dense. Cavity and Air Gap are in mm, Facing Mass in
kg/m2 and Flow Resist in Rayl/m; Amount scales the absorption itself, so 0 % is
transparent rather than a mix. Read-only readouts report the derived resonance,
Q, peak absorption and diffusion corner. A seeded Randomise builds a new panel
while preserving Amount and Bypass, and CC 1-5 read Amount, Cavity, Air Gap,
Facing Mass and Flow Resist by default.

## Practical combinations

- Techno rhythm: Xoxolo or DrumGen -> DrumKit -> E-Mix/P-Mix -> Rift for fills.
- Acid bass: BassGen or Ground -> Basilico, using Acid model, glide, wobble, and Squelch.
- Anthem harmony: authored chords or Cadence -> Canticle; duplicate into ArpGen -> Canticle/Floozy for motion.
- Glitch lead: GremlinDriver -> Gremlin, optionally followed by Rift or Ambo.
- Breakdown atmosphere: Canticle/Floozy -> Orchid -> Ambo.
- Live dub transitions: a drum or full-mix bus -> PaunchLad.
- Vehicle and machine texture: Magneto -> Lightverb or Ambo for exterior space,
  automating RPM and Throttle together; Guardian at the end of the chain.
- Number-theoretic colour: any synth or loop -> Pratt in Filter mode; try Index A 3 or 5
  with Index B 1 for a gentle slope, then raise B to cascade more poles. Or play it
  directly from a keyboard or generator with Voice set to GM Program.
- Hands-off texture: Drift -> Beards (CC 1-4 are Position, Size, Density, Dry/Wet) on a
  pad or drum loop; Conductor on its own channel thins and thickens it by section.
- Hands-off engine modulation: Drift -> Magneto. Drift's four lanes default to
  CC 1-4, which Magneto reads as Throttle, RPM, Silencing and Growl.
- Hands-off oscillator morphing: Drift -> Trajectory. Drift's lanes default to CC 1-4,
  which Trajectory reads as Sides, Start Position, Launch Angle and Bounce Jitter; point a
  lane at CC 5 or 6 to move X Level or Y Level.
- Hands-off drone timbre: Drift -> Fathom. Drift's lanes default to CC 1-4, which Fathom
  reads as Warp, Span, Morph and Seed; hold a chord with the sustain pedal and let it move.
- Living drums: dry drum bus -> Ghost, Ghost MIDI -> DrumKit for the ghost
  layer; Drift -> Ghost modulates Sensitivity, Density, Velocity and Drag.
- De-click and lift: Spliff in Cut after vocals, in Boost before drums
  compression; audition with Delta, compare with Bypass.
- Wah duties: HelterSkelter in Envelope after funk guitar, in BBT Blend on
  pads; Drift -> HelterSkelter plays Sensitivity, Depth, Resonance and Mix.
- Bus that sounds like the wrong room: Treatment on the mix or drum bus. Sweep
  Air Gap to move the resonance down, then find the Flow Resist sweet spot by
  ear; Drift -> Treatment drives the geometry on CC 1-5.
- Automatic arrangement: route instruments into T-Mix, then route Mixgen MIDI
  to T-Mix and tune Density, Depth, Variation, and Lane Spread.
- Full producer bus: set Mixgen to Full bus and route one MIDI send to a chain
  of T-Mix, Loopdelay, and Lightverb. Match their Control channel and optionally
  require CC 19 ownership. See [Producer Control Bus v1](producer-control-bus.md).
- Conductor-led arrangement: place Conductor on a control track and route its
  MIDI output to both BassGen and DrumGen. Set **Conductor Ch** on each generator
  to match Conductor's output channel (default 16). Conductor then reshapes
  density, variation, and pattern resets on every section boundary automatically.
  See [MIDI Mapping](midi-mapping.md).

## Host notes

Generators and MIDI effects must precede their receiving instrument in the track FX chain or route MIDI to another instrument track. Mixgen normally belongs on a control track whose MIDI send targets the T-Mix track. Transport-aware plugins need valid host tempo/play-state information. Launchpad-focused plugins remain usable without hardware where their UI exposes equivalent controls, but their intended performance feedback depends on Launchpad MIDI I/O. High Ambo feedback, stacked delay/shimmer, Rift repeats, and Gremlin randomization can become dense quickly, so conservative output trims are advisable.
