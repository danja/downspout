---
layout: default
title: Plugin Reference
description: A dense per-plugin capability reference for automated agents, with ports, MIDI roles, control maps and wiring conventions.
permalink: /plugins-summary/
---

# Downspout plugins: capability reference for agents

Purpose: let an automated agent decide which plugin provides a capability, what it
consumes and emits, and how to wire it. One paragraph per plugin. For user-facing
prose and example chains see `docs/summary.md`; for scale tables see `docs/scales.md`.

## Conventions that apply to every plugin

- **Format and host.** VST3 bundles built with DPF (maker `danja`, brand `Downspout`).
  All have a custom NanoVG editor. Bundle names use underscores where the plugin name
  has a hyphen (`m_mix.vst3`, `resonance_garden.vst3`).
- **Port fields below.** `audio in/out` is the channel count of the VST3 audio buses
  (`0 in` = pure generator or instrument). `MIDI in/out` is whether the plugin
  declares a MIDI input/output port. `transport` means it reads host tempo, play state
  and bar/beat position (`WANT_TIMEPOS`), and is silent or idle when the transport is
  stopped unless noted. A MIDI *processor or generator* must sit **before** its
  receiving instrument in the FX chain, or its MIDI must be routed to another track.
- **Parameters and persistence.** Settings are host parameters (automatable, saved by
  the host). A few plugins also keep non-parameter data in DPF text state: sample paths
  (mosaic, rift), zone data (campione), patterns (lifeform, luma, plank, bassgen and other
  pattern holders), a text/tuning blob (tuney-vst), the export path (midiscribe), a Scala
  path (retune).
  `scripts/check-plugin-state.sh` audits this.
- **Determinism.** Generators derive random choices from a seed plus absolute musical
  position (not wall clock), so loops, restarts and offline renders reproduce.
- **Control-change conventions.** Drift lanes default to CC 1-4; the audio effects
  chipper, damiano, skream, spliff, helterskelter, treatment, magneto and ghost read those
  by default so Drift needs no configuration. Conductor emits CC 20-24 on channel 16
  (Scene, Density, Energy, Mutation, Reset); bassgen, drumgen, ground, melgen, worms, bubbles,
  floozy, harmonic-atlas, sprout, lifeform, luma, polymeter, xoxolo, markov, counterpointer
  cadence, arpgen and m-mix listen through a **Conductor Ch**
  parameter (0 = off). Mixgen drives T-Mix with CC
  20-27 (strips 1-8), Loopdelay with CC 30/31, Lightverb with CC 32/33; CC 19 claims or
  releases the producer bus. Details: `docs/midi-mapping.md`,
  `docs/producer-control-bus.md`.
- **Per-channel pitch bend.** Retune emits one note per MIDI channel (2-16) with its own
  bend at a configurable range (default 2 semitones). Instruments that follow bend per
  channel and honour that range: canticle, moka, floozy, syrinx, pratt, basilico,
  gremlin, campione (Bend Range control, no RPN), mosaic (Pitch bend range control, only
  when Pitch range = 0). Canticle, moka, floozy, syrinx, basilico and gremlin also accept
  RPN 0 to set the range per channel. Other instruments ignore or globally apply bend.

## Quick reference

| Plugin | Bundle | Kind | Audio in/out | MIDI in/out | Transport |
|---|---|---|---|---|---|
| ambo | ambo.vst3 | audio effect | 2/2 | no/no | no |
| arpgen | arpgen.vst3 | MIDI processor | 0/2 | yes/yes | yes |
| basilico | basilico.vst3 | instrument (mono bass) | 0/2 | yes/no | yes |
| bassgen | bassgen.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| bassops | bassops.vst3 | audio effect (sidechain) | 4/2 | yes/no | no |
| bubbles | bubbles.vst3 | instrument (water) | 0/2 | yes/no | yes |
| cadence | cadence.vst3 | MIDI processor | 0/2 | yes/yes | yes |
| campione | campione.vst3 | instrument (sampler) | 2/2 | yes/no | no |
| canticle | canticle.vst3 | instrument | 0/2 | yes/no | no |
| chipper | chipper.vst3 | audio effect | 2/2 | yes/no | no |
| conductor | conductor.vst3 | MIDI generator/controller | 0/2 | no/yes | yes |
| counterpointer | counterpointer.vst3 | MIDI processor | 0/2 | yes/yes | yes |
| damiano | damiano.vst3 | audio effect | 2/2 | yes/no | no |
| drift | drift.vst3 | CC modulator + audio thru | 2/2 | no/yes | yes |
| drumgen | drumgen.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| drumkit | drumkit.vst3 | instrument (drums) | 0/2 | yes/no | no |
| e-mix | e_mix.vst3 | audio effect | 2/2 | no/no | yes |
| floozy | floozy.vst3 | instrument | 2/2 | yes/no | no |
| flues-synth-driver | flues_synth_driver.vst3 | MIDI controller | 0/2 | yes/yes | no |
| gater | gater.vst3 | audio router | 2/4 | yes/no | declared |
| ghost | ghost.vst3 | audio-to-MIDI | 2/2 | yes/yes | yes |
| gremlin | gremlin.vst3 | instrument (glitch) | 0/2 | yes/yes | no |
| gremlin-driver | gremlin_driver.vst3 | MIDI controller | 0/2 | yes/yes | yes |
| ground | ground.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| guardian | guardian.vst3 | audio utility (limiter) | 2/2 | no/no | no |
| harmonic-atlas | harmonic_atlas.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| helterskelter | helterskelter.vst3 | audio effect (wah) | 2/2 | yes/no | yes |
| keyframe | keyframe.vst3 | audio effect (time stretch) | 2/2 | yes/no | no |
| lifeform | lifeform.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| lightverb | lightverb.vst3 | audio effect (reverb) | 2/2 | yes/yes | no |
| loopdelay | loopdelay.vst3 | audio effect (delay/looper) | 2/2 | yes/yes | yes |
| luma | luma.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| magneto | magneto.vst3 | instrument (engine) | 0/2 | yes/no | yes |
| melgen | melgen.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| midiscribe | midiscribe.vst3 | MIDI utility | 0/0 | yes/yes | yes |
| mixgen | mixgen.vst3 | CC generator + audio thru | 2/2 | no/yes | yes |
| m-mix | m_mix.vst3 | MIDI processor | 0/2 | yes/yes | yes |
| mnemosyne | mnemosyne.vst3 | MIDI processor | 0/2 | yes/yes | yes |
| moka | moka.vst3 | instrument (modal) | 0/2 | yes/no | no |
| mosaic | mosaic.vst3 | instrument (slice sampler) | 0/2 | yes/no | yes |
| oracle | oracle.vst3 | audio analyser + MIDI | 2/2 | yes/yes | yes |
| orbit | orbit.vst3 | audio effect (spatial) | 2/2 | no/no | yes |
| orchid | orchid.vst3 | audio effect (freeze) | 2/2 | yes/no | yes |
| paunchlad | paunchlad.vst3 | effect/instrument (Launchpad) | 2/2 | yes/yes | no |
| plank | plank.vst3 | instrument (Launchpad) | 0/2 | yes/yes | yes |
| p-mix | p_mix.vst3 | audio effect (gate) | 4/2 | no/no | yes |
| polymeter | polymeter.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| pratt | pratt.vst3 | instrument + filter | 2/2 | yes/no | no |
| primefold | primefold.vst3 | audio effect (pitch) | 2/2 | yes/no | no |
| quefrency | quefrency.vst3 | audio effect (formant) | 2/2 | yes/no | no |
| resonance-garden | resonance_garden.vst3 | audio effect (resonators) | 2/2 | yes/no | no |
| retune | retune.vst3 | MIDI processor (tuning) | 0/2 | yes/yes | no |
| rift | rift.vst3 | audio effect (buffer) | 2/2 | yes/no | yes |
| sidecar | sidecar.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| skream | skream.vst3 | audio effect (filter) | 2/2 | yes/no | no |
| spliff | spliff.vst3 | audio effect (transients) | 2/2 | yes/no | no |
| markov | markov.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| sprout | sprout.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| syrinx | syrinx.vst3 | instrument (birdsong) | 0/2 | yes/no | no |
| t-mix | t_mix.vst3 | audio mixer | 8/2 | yes/yes | no |
| treatment | treatment.vst3 | audio effect (acoustic) | 2/2 | yes/no | no |
| tuney-vst | tuney_vst.vst3 | instrument + MIDI | 0/2 | yes/yes | yes |
| voxmod | voxmod.vst3 | audio effect (vocoder) | 4/2 | yes/no | no |
| worms | worms.vst3 | MIDI generator | 0/2 | yes/yes | yes |
| xoxolo | xoxolo.vst3 | MIDI generator | 0/2 | yes/yes | yes |

Some "0/2" generators declare a stereo output they leave silent; that is a host
convenience, not audio content. The gater audio bus counts are 2 in, 4 out (two stereo
pairs).

---

## MIDI generators

### bassgen
Transport- and meter-aware bass line generator with persistent patterns, loop-boundary
variation and style set Auto, Straight, Reel, Waltz, Jig, Slip Jig. Emits bass-register
MIDI (default channel 1) and consumes incoming MIDI as context: configurable Follow or
Dodge, channel/note matching and sensitivity let existing parts guide or suppress
generated notes. **Conductor Ch** (0 = off) maps CC 21 to density, 22 to accent, 23 to
auto-variation rate and CC 24 = 127 to a fresh pattern; CC 20 is ignored. Pair with
basilico or any bass instrument. Requires host transport.

### drumgen
Meter-aware MIDI drum generator with pattern variation and fills; genre vocabulary
includes Breakbeat, Amen, Jungle and Hip Hop. Emits drum MIDI (default channel 10)
aimed at drumkit's note map; MIDI input exists only for control (Conductor CCs).
**Conductor Ch** maps CC 21 to density, 22 to variation, 23 to mutation rate and CC 24 =
127 to a new pattern. Persistent patterns. Requires transport. **Automaton layer** (off by default;
appended parameters `ca_rule` 0-10, `ca_target` 0-4, `ca_every` 1-8 loops per generation): the generated
pattern is generation 0, then on each generation the targeted lanes (hats, percussion, toms, all three, or every lane) play the next row of a
1-D cellular automaton (rules 30, 90, 110, 150, 18, 54, 60, 22, 105, 126; wrapping edges) seeded from the
lane's own hits; the pass comes from the playback position, the stored pattern is untouched, empty lanes
stay silent, and after 64 generations a lane returns to the pattern.

### melgen
Phrase-aware melody generator that shapes contour, question/answer structure and
longer-range form, and reacts to incoming musical context. **Inertia** (default 0 =
unchanged output) biases the next interval Narmour-style: after a leap it favours
reversal, after a step it favours continuation. Accepts MIDI and control MIDI, emits
melody MIDI. Persistent state. Requires transport.

### ground
Long-form bass generator with Dub and Jazz orientations, phrase planning (phrase roles
Statement, Climb, Breakdown, Answer, Cadence) and register-guarded output, for evolving
foundation lines rather than short loops. **Conductor Ch**: CC 20 sets `tension` and
overrides the current phrase role (0-16 Statement, 17-48 Climb, 49-80 Breakdown, 81-112
Answer, 113-127 Cadence), CC 21 density, CC 22 motion, CC 23 vary, CC 24 = 127
regenerates the whole arc. Requires transport.

### harmonic-atlas
Autonomous chord generator: emits bounded chords with no input, or treats incoming notes
as pitch-class roots. Four movement families (tonal, modal, chromatic-mediant,
neo-Riemannian). **Gravity** (default 0 = unchanged) replaces a chord with probability up
to 85% by a functional-harmony target (V to I, IV to V, I to IV/V/vi, otherwise drift to
I/IV/V; cadence positions land on the tonic). **Voice-leading** above 50% (default 75%)
chooses inversion and octave by a sorted-voice-distance search against the previous
chord (penalty outside C2-C6, voiced six chords back so a chord depends only on its
number and seed); at or below 50% it uses the old fixed stacking. Voicing spread limits
inversions tried. Output is HarmonyMidi for any chord-capable instrument. Requires transport.

### worms (ToneWorm)
Transport-locked melody generator that walks a Tonnetz pitch lattice with Paterson's Worm
algorithm: six directional rules, each choosing among L120, L60, Straight, R60, R120.
Scale quantisation, register control and loop-boundary variation; Randomize and Mutate
actions make new rule sets. **Conductor Ch** reads CC 21-24 (density, velocity, vary,
mutate). Route its MIDI to melgen or an instrument. Requires transport.

### markov
Markov-chain melody generator, transport-locked, monophonic MIDI out. The walk is over the twelve
pitch classes counted from **Root** through a 12 x 12 transition matrix (weights 0-8 per cell) held
in plugin state, shown on screen and editable by clicking cells; **Style** (Stepwise, Triadic,
Fifths, Pentatonic, Blues, Chromatic, Tonic pull, Uniform) replaces it, Clear zeroes it (an empty row
plays any note in the scale) and Randomise draws one. **Scale** (the 24 canonical scales) zeroes
out-of-scale notes before sampling, **Chaos** (default 50%) is the exponent 2^(1-2c) on the weights,
and **Order** 1-2 uses the two previous notes where learned second-order data exists for the pair.
Notes take the octave nearest the previous one inside **Lowest note** and **Octaves** (1-4);
**Density**, **Gate**, **Velocity**, **Step grid** (as Sprout) and **Channel** shape the rhythm.
Each **Phrase length** (1-8 bars) restarts the walk from the tonic, seeded by Seed, so a note is a
pure function of its position: loops, locates, other block sizes and offline renders repeat exactly.
**Learn** counts note-to-note transitions on the MIDI input (relative to Root, octave-equivalent;
**Learn channel** 0 = all; feed one line), **Learned mix** blends first-order counts into the matrix
row by row where a row has data, **Clear learned** (a counter parameter) forgets them; matrix and
counts are saved as the `model` state. **Conductor ch** (0 = off): CC 21 Density, 22 Velocity, 23 Chaos,
CC 24 = 127 re-rolls the melody at the next bar line. Pair with retune for microtonal melodies.

### sprout
L-system melody generator. MIDI input is optional and off by default (see below); output is
a monophonic line. Seven grammars (Plant, Koch, Dragon,
Sierpinski, Cantor, Levy, Tree), a generation count, scale and root, played as a
monophonic line locked to the transport on a grid from 1/16 up to 1 bar (bar length from
the host time signature). **Grow every** N bars starts at generation 1 and adds one
generation per N bars. Expansion is capped at a fixed symbol count and falls back to the
deepest generation that fits; tables are prebuilt, so no allocation occurs in the audio
thread. Cantor yields rhythm on one pitch, Koch/Tree contour, Plant long self-similar
phrases. Settings are parameters only (steps derive from the preset). Grammars are built
in; there is no rule editor. **MIDI input:** **Pitch source** = Held or Latched (default Scale,
unchanged) maps degrees onto the notes held on the input (degree 0 = lowest held note, spread
over the held tones at about seven degrees per octave; nothing held = rest; **Input channel**
0 = all) so it can follow harmonic-atlas, cadence or arpgen. Latched keeps the chord after the keys
are released and replaces it on the first press after all keys are up. **CC channel** (0 = off) enables the
Drift convention on that channel: CC 1 Probability, 2 Gate, 3 Range, 4 Generations.
**Conductor ch** (0 = off): CC 21 Probability, 22 Velocity, 23 Seed, CC 24 = 127 restarts the
pattern at the next bar line. MIDI is applied at block start; CCs write through to the
parameters; the Generations count is latched once per bar. Appended parameters, so older
projects are unchanged.

### polymeter
Four Euclidean lanes, each with independent length, rotation, pulses, ratchets,
probability, accent, note and channel; lengths can be coprime so lanes drift against each
other. Each lane has an **Automaton rule** (default 0 = Euclidean; 1-255 selects a
one-dimensional cellular automaton such as 30, 90, 110, 150) whose row evolves every
lane cycle, with Pulses setting the initial row density. Events are scheduled at sample
offsets within the host block on a single MIDI bus separated by note and channel. Requires
transport. **Conductor ch** (0 = off; MIDI input is used only for this) maps CC 21 to a master
**Density** (multiplies every lane's probability), 22 to a master **Energy** (scales velocity), 23
to the seed, and CC 24 = 127 restarts every lane at the next bar line. Both masters default to 1
(no change).

### xoxolo
x0x-style drum step sequencer with 11 drumkit-oriented lanes and patterns up to 32 steps;
use when explicit programmed patterns are wanted rather than generation. MIDI input is used
only for Conductor CCs. Requires transport. **Evolve:** any lane can be marked (per-lane flag in the saved pattern,
written only when used); with **Evolve rule** (`ca_rule` 0-10, 0 = off) a marked lane plays the
programmed grid on pass 0 and then successive rows of a 1-D cellular automaton seeded from it, each
held for **Evolve every** (`ca_every` 1-8) passes; unmarked lanes always play their grid, the grid
itself is never altered, empty lanes stay silent, and the lane returns to the grid after 64
generations. The pass number comes from the playback position, so it is repeatable.
**Conductor Ch** (0 = off): CC 21 sets `density` (probability a programmed hit plays, seeded so it is
repeatable), 22 sets `energy` (velocity 100 down to 40), 23 adds 0-63 generations to the Evolve
layer, and CC 24 = 127 restarts the pattern and evolve generation at the next bar line. Defaults
leave the pattern as programmed.

### lifeform
Conway's Game of Life sequencer for Launchpad (or UI-only use). Each beat advances a
generation and living or newly born cells become melodic or drum notes, with scale
mapping, velocity, gate, density, seed pattern and randomise/clear/step actions. The live
64-cell pattern is saved as a bitmap in plugin state; restoring a seed does not reseed
over it. LED feedback is sent back to the controller. Clock mode is transport or free
running. Requires transport in transport mode. **Conductor Ch** (0 = off) maps CC 21 to density, 22 to
velocity, 23 to mutation and CC 24 = 127 to a fresh random pattern; Conductor CCs still pass through
when Pass Input is on.

### luma
Launchpad performance generator: lit grid cells drive coordinated bass, chord, melody and
drum agents, with transport-clocked or free-running steps, multi-part MIDI output and LED
feedback. Pattern state is saved. **Conductor Ch** (`conductor_ch` 0-16, default 0 = off) maps CC 21 to
density, CC 22 to energy and CC 24 = 127 to scatter; CC 20 and 23 are unused.

### sidecar
Phrase player for generated solo material. Deterministic local generation, with an
optional localhost coordinator mode for phrases supplied by an external process
(`docs/sidecar-plan.md`, `docs/ai-api.md`). Emits melody MIDI; requires transport.

### tuney-vst
Text-to-music instrument and MIDI generator derived from Tuney 0.3.39. Typed or stored
Unicode text is mapped through configurable alphabets, scales and microtonal tunings and
played as seeded free-time phrasing through a small polyphonic internal synth, while
also emitting ordinary MIDI. Text and tuning data are persistent state.

### conductor
Arrangement controller. Emits bar-aligned section notes plus scene, density, energy,
mutation and reset CC values on a configurable channel (default 16) at each section
boundary, from a fixed musical form (Intro, Develop, Break, Reprise, Coda) or seeded
weighted section selection with bounded durations. CC 20 Scene (0/32/64/96/127), 21
Density, 22 Energy, 23 Mutation, 24 Reset (127 at Intro and Coda). MIDI out only; it never
alters the host graph. Targets: bassgen, drumgen, ground, worms, bubbles.

## MIDI processors and utilities

### arpgen
Transport-synced arpeggiator. **Chord** mode captures incoming chord slices; **Scale**
mode derives scale runs, triads or sevenths from held register anchors. Rates quarter
through thirty-second, straight and triplet; one to four octaves; up, down and alternating
orders. Latch and capture state are internal. Requires transport. **Conductor Ch** (0 = off):
CC 21 Density sets Octaves (1-4), CC 22 Energy sets Rate (1/4 up to 1/32).

### cadence
Transport-aware harmoniser and comping generator that learns harmony from incoming MIDI
(learned transition model) and emits chord/comping MIDI. No gravity control yet; a
progression chosen by learned transitions, not a functional-harmony graph. Persistent state.
**Conductor Ch** (0 = off): CC 21 Complexity, 22 Movement, 23 Variation, CC 24 = 127 relearns.

### counterpointer
Learns incoming MIDI and emits a monophonic counter-melody. Counterpoint mode and Bass
Descend mode; **Inertia** (default 0 = unchanged) is a scoring term in Counterpoint mode
only. Routing column provides Freeze and channel selectors. Persistent state. **Conductor** (0 = off):
CC 21 Density, 22 Embellish, 23 Short Rnd, CC 24 = 127 relearns.

### mnemosyne
Phrase memory. Stores eight bounded phrases, accompanies incoming MIDI, or generates from
recalled and transformed material; an internal motif covers an empty reservoir.
Persistent motif state; requires transport.

### m-mix
Transport-aware MIDI gate combining probabilistic P-Mix-style transitions with E-Mix-style
Euclidean blocks: removes or passes notes before they reach an instrument. **Conductor Ch**
(0 = off): CC 21 Density sets Open Bias, CC 22 Energy sets Maintain inverted (busier gating).

### retune
MIDI effect that retunes a note stream to a Scala `.scl` scale. For each held note it
sends a pitch bend that moves the nearest 12-TET key onto the scale degree, on its own
channel (2-16), so the downstream instrument must follow per-channel bend at the same
range (**Bend range**, 1-24 semitones, default 2, must equal the synth's). CC and program
change reach every channel; incoming pitch bend and channel aftertouch are dropped. With
no file loaded it passes 12-TET unchanged. Only the scale file path is saved, not its
contents; `.kbm` keyboard maps are not supported. Place between a generator and a
per-channel-bend instrument.

### midiscribe
MIDI capture utility, no audio. Passes all MIDI through unchanged; while **Armed**
(default off) it records into a rolling window (**Capture Beats** 8/16/32/64) and writing
(**Write** momentary trigger) exports a Type 0 Standard MIDI File at 480 PPQ with the host
tempo. The export path is state (key `exportPath`, default `/tmp/midiscribe.mid`).

### flues-synth-driver (FlueSynthDriver)
MIDI controller for the external Flues vocal/physical-model synthesizer. Converts plugin
parameters (Disyn algorithm and three Disyn parameters, noise/DC level, interface type,
formants F1-F4, nasal/sing/shout/fry modes, attack, release, tuning, delays, filter
feedback, gain, trajectory controls) into CCs, with 31 selectable programs (MIDI program
change 0-30) that remap nine sliders to CCs. Only changed parameters are sent (dirty
tracking), and the first block after activation resends the sliders. Output channel and a
Conductor channel are selectable; input MIDI passes through unless blocked, and Panic and
Randomize actions are provided. No transport use.

### gremlin-driver
Automation companion for gremlin. Several modulation lanes (shape, rate, depth, centre,
target) and trigger slots (action, rate, chance) emit CC, scene/action commands and
randomisation bursts, clocked by transport or an internal clock (BPM 40-220). Optional
input MIDI pass-through (default on).

### drift
Four-lane transport-synchronised MIDI CC modulator that also passes audio unchanged. Each
lane is an LFO, sample-and-hold, bounded random walk, deterministic chaos source or
audio envelope follower, with a target CC, depth and centre. Default CCs 1-4 so the
effects that read those need no setup (see conventions).

### mixgen
Transport-synchronised automatic producer for t-mix. Eight repeatable gain lanes use
random, low-discrepancy or Euclidean patterns and send CC 20-27 to strips 1-8. T-Mix,
FX-only and Full-bus profiles add four macros that route to loopdelay (CC 30, 31) and
lightverb (CC 32, 33); CC 19 claims or releases the producer bus. Audio passes through.

### oracle
Self-influence analyser: bounded analysis of level, onset, brightness, density,
pitch class and incoming MIDI, mapped to rate-limited, guarded CC and note responses.
Fixed analysis storage and a refractory interval after each response keep feedback
bounded. Audio in and out, MIDI in and out.

### ghost
Audio-triggered ghost-note generator: listens to audio (output silent unless **Audio
Thru** is on) and emits MIDI quantised to the transport 16th grid. Onset accents jump to
the next slot; quieter fills land on off-16th slots with probability **Density**, pushed
late by **Drag**. **Drums** mode emits kick 36 and ghost snare 38 on channel 10; **Notes**
mode walks a pentatonic-minor ladder from **Base Note** on a selectable channel.
Sensitivity, Density, Velocity and Drag read Drift CC 1-4. Fires only while the
transport runs. Natural chain: dry drum bus into ghost, ghost MIDI into drumkit.

## Instruments

### drumkit
Stereo synthesised drum instrument with one MIDI input and a mixer-style panel, one
instrument per note: eleven instruments (Kick, Clap, Snare, Crash, Closed HH, Tom 1,
Open HH, Tom 2, Bash, Cowbell, Clave), each with a mute. Natural target of drumgen,
xoxolo, luma drum output and GM-style drum MIDI. Notes select unpitched drums, so
pitch bend has no effect. Settings are parameters only.

### basilico
Monophonic bass with five core models Upright, Electric, Dub, Acid and Industrial plus
Reese and Hoover profiles; note priority (last-held), glide with legato behaviour,
velocity accent, per-model filter and drive, free or tempo-synced wobble (amplitude,
filter and stereo phase/flange targets with 0-360 degree start offset), and a
**Squelch** acid macro. Follows per-channel pitch bend (sounding note's channel, RPN 0
range, applied instantly rather than gliding). Host tempo used for wobble sync. Bypass
silences it.

### canticle
Twelve-voice instrument with keys, reed, pad, pluck and glass timbres for melody,
counterpoint and chords. Per-channel pitch bend with RPN 0 range, voices keyed by channel
and note.

### floozy
Eight-voice hybrid physical/modulation synthesiser derived from floozy-poly; expressive
leads, plucks and unusual textures. Declares audio input. Per-channel pitch bend with
RPN 0.

### moka
Modal "hit object" synth: eight damped modes per voice, six instrument tables (xylophone,
glockenspiel, woodblock, glass bowl, metal sheet, tube), polyphony 1-12 (default 4), eleven
presets including Kalimba. Note onset ramps over the mallet contact time (no click).
Per-channel pitch bend with RPN 0, voices keyed by channel and note.

### syrinx
Polyphonic birdsong synthesiser: Mindlin-Laje syrinx ODE solved per voice at audio rate
with optional oversampling, pitch-tracking vocal-tract bandpass, formant bank, respiration
and noise/roughness modifiers. Up to eight voices, oldest-completed voice stealing, ten
presets (Wren to Custom). Controls: Level, Noise, Roughness, Timbre, Vibrato Rate and
Depth, Contour Bend, Harmonic, AM Rate, Distance, Gain. CCs 1 vibrato depth, 7 volume,
71 roughness, 74 timbre, 76 vibrato rate, 77 noise, 91 distance, 94 contour bend.
Per-channel pitch bend with RPN 0.

### pratt
Number-theoretic synthesiser and filter built on Pratt polynomials. **Synth** mode plays
eleven General MIDI voice families (piano, electric piano, organ, pluck, bass, strings,
brass, reed, flute, pad, timpani) with spectra from the stable all-pole filter H_n at
n = (note + 1) x base; handles sustain pedal, volume, expression, pan, pitch bend (own
Bend Range control, per channel) and channel-10 drums. **Filter** mode filters the audio
input through cascaded Index controls (n = A x B), Cutoff and Filter Mix; **Synth +
Filter** does both. Timbre Index (separate fixed chain) darkens voices. Audio input is
ignored in Synth mode. Persistent state.

### magneto
Physically informed four-stroke engine synthesiser (Baldan et al., SIVE'15): per-cylinder
digital waveguides feed intake runners, extractors, a straight pipe, a four-element
muffler and a tailpipe; outputs intake, block vibration and tailpipe mixed to stereo for
a Cabin/Front/Rear/Exterior listening position. Controls: RPM, Throttle, cylinders,
displacement, compression, ignition width, Growl, pipe lengths, Silencing, Backfire,
Inertia (flywheel). A MIDI note sets RPM (transposed down two octaves; C4 hits the 9000
rpm limit) and velocity sets Throttle; note-off is ignored. CC 1 throttle, 2 RPM, 3
silencing, 4 growl, 5 pipe, 6 turbulence, 7 output. **Speed source** Host Sync locks the
engine cycle to tempo and idles when the transport stops. Sounds immediately at 850 rpm
idle. Not a pitched voice: pitch bend is not applicable.

### bubbles
Water sound generator, stereo, continuous without MIDI (note-on/off optionally gates and
modulates). Seven modes (Stream, River, Ocean, Bubbles, Drips, Rain, Custom) over
subtractive noise layers plus physical models (up to 36 bubble voices, 16 drip voices,
four modal body resonances), with Flow, Turbulence, Size, Density, Heat, Depth,
Brightness, Resonance, Randomness, Space, Drive and Output. **Conductor Ch** is accepted.

### gremlin
Chaotic glitch instrument, monophonic source with performance scenes (Manual plus
Splinter, Melt, Rust, Tunnel), 17 live and eight hidden parameters, eight macros, eight
momentary controls,
randomisation actions (reseed, burst, random source/delay/all, panic), delay behaviours,
LED/controller feedback for a Launchpad-style surface, and master trim. Also accepts
CC 16-31, 46-62 and button notes as a hardware surface. Plays pitch from MIDI notes
above the controller button range and follows per-channel pitch bend for the last-played
channel. Companion: gremlin-driver. Keeps its patch across activation and sample-rate
change.

### campione
Multi-zone sampler: each zone maps a WAV to a note range, pitch-shifts to the target pitch,
fills gaps by nearest root, with per-zone ADSR, biquad filter (LP/BP/HP/notch), pan,
zero-crossing-snapped and crossfaded loops, and mute. Loads WAV PCM and float32, REX2 slices
(consecutive notes from C1), and Serum `clm` wavetables (first cycle extracted); records
live input into a new zone with auto root-note detection; beat slicing; embedded MCP HTTP
server (default on) for programmatic zone control (`docs/mcp.md`). Global MIDI channel
filter (0 = all). Voices keyed by note and channel; per-channel pitch bend with the Bend
Range control (0-24, default 2, no RPN). Eight voices, oldest stolen. A zone pinned to one channel will not sound for
Retune's other channels. Zone data and sample paths are state. Loop end is treated as
exclusive (the WAV `smpl` spec is inclusive, so the last loop frame is skipped).

### mosaic
Four-slot WAV sampler with MIDI and autonomous triggering (**Trigger source** MIDI,
Autonomous, Both). Seeded slice selection, grain size, slice size, reverse chance,
stereo spread, attack/release, 16 voices, zero-crossing boundary snapping. **Pitch range**
(default 7) gives each slice a random transposition within the range and ignores the
note; at 0 the note sets pitch (note - 60) and **Pitch bend range** (1-24, default 2)
applies per channel. Sample paths are state. Requires transport for autonomous mode.

### plank
Eight-string touch-synth derived from Plinky, driven from a Launchpad grid (each column a
string, each row a scale degree; polyphony held rather than allocated). PolyBLEP oscillator
pair morphing across 17 band-limited wavetables, resonant two-pole filter, two envelopes,
tape delay, shimmer reverb, mid/side width, and several resonator engines. Emits LED and
note MIDI (note-on/off on a base channel, with a MIDI thru switch). 24 scales, ordering
governed by `docs/scales.md`. Input notes are grid presses, not pitches, so pitch bend is
not applicable. Persistent state; host validation pending.

## Audio effects

### ambo
Stereo ambient processor with four rearrangeable module chains over Time, Spectral, Tape,
Shimmer, Delay, Drive, Feedback, Mix and Output controls: granular smear, frozen-band
motion, tape colour, shimmer diffusion, ping-pong echo, saturation and regenerative
ambience. High Feedback and stacked shimmer become dense quickly; keep Output trimmed.

### chipper
Lo-fi crusher: sample-and-hold rate reduction (Rate Div 1-64, optional Jitter 0-100%)
followed by midtread bit quantisation (Bit Depth 1-16), then Mix and Output Gain
(-12..+12 dB). CC overrides (default CC 1 bit depth, 2 rate div, 3 jitter, 4 mix on the
selectable channel; CC 0 disables) so Drift drives it directly.

### damiano
Six-mode stereo distortion (Soft, Tanh, Fuzz, Overdrive, Tube, Wavefold) with Drive 1-10,
Tone (one-pole high shelf before the shaper, about 3 kHz, 50% = flat), Folds 1-8
(Wavefold only), Mix and Output Gain (+-24 dB). **Stereo Linked/Split** (default Linked,
identical to before): Split gives the right channel its own Mode, Drive, Tone and Folds
for a binaural effect. Selectable CC numbers (all on one CC channel, 0 = off): CC Drive and
CC Shape (waveshaper mode in six steps) for the left or both channels, and CC Drive R and CC Shape R
for the right channel (Split only), so two Drift lanes can modulate the channels independently.

### skream
Scream filter: input through an ADAA2 anti-aliased tanh saturator and an SVF low-pass,
with a high-passed, saturated feedback path gated by an envelope-follower expander. Ten
presets. Controls Input Gain, Cutoff, Scream (feedback high-pass cutoff, clamped to
Cutoff), Resonance (HP Q and feedback gain), Mix, Output Gain. **Morph** (-100..+100 %, default 0 = off, appended parameter) crossfades the forward SVF between
low-pass and high-pass by a weight that follows the effective cutoff position (+: LP at low
cutoff to HP at high; -: reverse; weight 0.5 is a notch), so Drift/CC sweeps of Cutoff also
sweep the shape; 0 leaves output bit-identical to the plain low-pass. Selectable CC numbers
override Cutoff and Scream.

### bassops
Sidechain ducker plus mid/side bass management. Inputs 1-2 are the main signal, 3-4 a
control signal (typically a dry kick send) driving an envelope follower whose inverse ducks
the main signal. After ducking a linear-phase LP/HP FIR pair splits at a shared cutoff:
bass stays in the mid channel, highs keep stereo width in the side. Reports 64-sample
latency to the host.

### e-mix
Transport-aware Euclidean stereo gate: deterministic block rhythms applied to audio.

### p-mix
Transport-aware probabilistic stereo gate/mixer producing evolving channel switching and
rhythmic dropouts. **Oppose** uses a sidechain on inputs 3-4: loud sidechain tends to make
it quieter and vice versa.

### t-mix
Eight-input mono-to-stereo mixer with level, pan, mute, solo, meters and master fader.
Accepts Mixgen CC 20-27 as click-smoothed transient gain multipliers while the manual
faders stay as the saved balance. Passes producer MIDI through.

### loopdelay
Stereo delay and capture looper. Time is free (20-4000 ms) or follows host BBT from a
quarter beat to four bars. Fixed CC 30 time and CC 31 feedback temporarily override the
panel until **Release MIDI**; optional CC 19 gating, per-chain channel filter and MIDI
through. Intended order `t-mix -> loopdelay -> lightverb -> guardian`.

### lightverb
Fixed-cost stereo feedback-delay-network reverb, low CPU, usable as insert or 100% wet
send. Fixed CC 32 Wet mix and CC 33 Space; same CC 19 lifecycle, channel isolation and
MIDI-through behaviour as loopdelay.

### guardian
Output safety processor for the end of autonomous chains: input gain (+-24 dB),
look-ahead limiter with attack/release, clipper shape (none through gentle tanh to near
hard clip), DC removal, true-peak guard, silence detection, bypass and latched fault
diagnostics. Reports ceiling and latency to the host. No MIDI. Place last.

### rift
Transport-locked live/sample buffer disruptor: WAV loading, chop and stutter repeats,
reverse, skip, smear and pitch-slip actions, for fills, transitions and glitches. Sample
path is state. Repeats can get loud; trim output.

### orchid
Transport-aware voiced freeze/hold: autocorrelation capture and grid-synchronised loop
holds turn incoming notes or textures into sustained rhythmic freezes.

### keyframe
Extrema-sampling time stretch (Nielsen, DAFx26-26). Input reduced to timestamped local
extrema whose spacing sets content-adaptive overlap-add splices; no transient detector.
**Time** is a rate (1.00x down to slower; never faster, since input is live), **Pitch** is
independent playhead rate (pitch ratio = Pitch / Time), **Splice** leash length K,
**Max Splice** cap, **Threshold** (-60 dB default), **Hold** freezes the reference
playhead. CC 1 time, CC 2 pitch, CC 7 output. Constant 8192-sample reported latency with
the dry path matched; slow-downs reach an internal 65536-sample depth limit and then loop
the held material. Default (unity, 60% wet) is nearly transparent.

### quefrency
Cepstral formant and harmonic shifter ported from JigDAW. Each frame (2048 samples up to
50 kHz, 4096 above, hop a quarter) is split via the cepstrum into formant envelope and
harmonic excitation, shifted and reshaped independently, and multiplied back: Formant
shift (+-12 st), depth (0-200%), tilt, Pitch shift (+-24 st) and fine, Freq shift (+-1000
Hz), Harmonic depth, Lifter (0.5-5 ms), Estimator (Cepstral or True envelope), Mix,
Output. CC 70-80 drive the eleven parameters in order. Reports 2047 or 4095 samples of
latency.

### primefold
Prime-harmonic feedback shifter: direct 2x, 3x, 5x pitch voices (time-domain dual-tap
resamplers sharing a grain window of 512, 1024 or 2048 samples) recirculated all-to-all
through a 256-sample delay and per-pass damping low-pass, so 4x, 6x, 8x, 9x, 10x emerge
from feedback. Feedback is capped at 0.90, the loop contractive, with NaN guard. Dry,
2x/3x/5x levels, Feedback (CC 1), Damp, Grain, Mix (CC 2), Width, Output (CC 7).
Feedforward latency equals the grain window and is reported; in-loop latency is not
compensated, so composites arrive late.

### voxmod
Vocoder and band-limited ring modulator in parallel, blended by Mix. Inputs 1-2 carry the
carrier (selectable: inputs 1/2 or internal oscillator) and inputs 3-4 the modulator.
Ring modulator frequency = Ring Freq x Ring Ratio, ratio quantised to 16 steps from -12
to +35 semitones, Ring Depth, polyBLEP saw/square. Vocoder: logarithmic filter bank across
Spread octaves with asymmetric envelope followers; Sync feeds the ring-modulated signal
back as the vocoder carrier. MIDI CC input. A panel XY pad maps X to Mix, Y to ring
frequency. Needs a modulator with harmonics.

### resonance-garden
Eight bounded damped resonators tuned by held MIDI notes (HarmonyMidi) or an internal
scale, turning arbitrary audio into pitched evolving material: damping, inharmonicity,
freeze, bounded feedback and deterministic voice replacement.

### orbit
Ordinary stereo spatial processor (no HRTF or binaural claim): seeded orbit, pendulum,
random-walk and figure-eight trajectories with distance filtering and conservative
Doppler. Transport-aware and deterministic.

### helterskelter
Automatic wah: resonant low-pass (Q 0.5-12) whose cutoff follows the input envelope
(touch wah) and/or a BBT-synced ADSR cycle (1, 2, 4 or 8 beats, Gate Beats, Invert),
Blend takes whichever opens further. Sensitivity, Depth, Resonance, Mix read Drift CC 1-4.

### spliff
Adaptive transient processor in the Spiff manner: 3-band transient detector applies
dynamic cuts or boosts only at transients. **Cut** mode tames clicks and harsh sticks
(late in a chain); **Boost** lifts drum and piano attacks (before compression). Depth,
Sensitivity, Decay and Mix read Drift CC 1-4; Sharpness, Decay LF/HF tilt, band splits,
Trim, Delta monitoring and Bypass. Detector is mono, so the stereo image is preserved.

### treatment
Physically modelled acoustic treatment panel: facing sheet (mass), porous fill (flow
resistance) and air gap to the wall form a mass-air-mass resonator, with resonance
w0 = c * sqrt(rho0 / (m D)) and an impedance match at r = rho0 c. Cavity and Air Gap in
mm, Facing Mass in kg/m2, Flow Resist in Rayl/m; **Amount** scales absorption itself
(0% is transparent). Read-only readouts report resonance, Q, peak absorption and
diffusion corner. Seeded Randomise preserves Amount and Bypass. CC 1-5 read Amount,
Cavity, Air Gap, Facing Mass, Flow Resist.

### paunchlad
Launchpad dub performance effect and instrument: pads trigger echo throws, spring
splashes, sirens, alarms, synthetic snare/crash/sub hits, lasers, thunder, rewind,
bubbles, risers, horn effects, dropouts, chops and freezes over a processed audio
pass-through, with LED feedback. Pad cells are momentary, so only the nine settings
are saved.

### gater
Routes one stereo input to one of two stereo output pairs by incoming MIDI: **even note
numbers** to Output 1, **odd** to Output 2. It has no parameters and no saved state. It
reads only note-on status byte 0x90 on channel 1 (a note-on with velocity 0 is treated as
a note-on), so it does not follow other channels.

---

## Choosing between plugins

- **Need MIDI from nothing:** harmonic-atlas (chords), melgen / worms / sprout / markov / sidecar
  (melody), bassgen / ground (bass), drumgen / xoxolo / polymeter (rhythm), lifeform /
  luma (grid-driven), conductor (structure only), tuney-vst (text).
- **Need MIDI from MIDI:** arpgen, cadence, counterpointer, mnemosyne, m-mix, retune (tuning).
- **Need MIDI from audio:** ghost (transient-triggered ghost notes), oracle (analysis).
- **Need automation CCs:** drift (four lanes), mixgen (mix lanes), gremlin-driver and
  flues-synth-driver (specific targets), conductor.
- **Need a pitched sound:** canticle (general), basilico (bass), moka (mallets), syrinx
  (birds), pratt (GM voices), floozy, gremlin (glitch), campione / mosaic (samples),
  tuney-vst. Bend-aware: see the per-channel list at the top.
- **Need an unpitched sound:** drumkit, magneto, bubbles.
- **Need audio processing:** distortion (damiano, skream, chipper), time/pitch (keyframe,
  primefold, quefrency), space (lightverb, loopdelay, orbit, treatment, ambo),
  rhythm/gating (e-mix, p-mix, orchid, rift), tone/dynamics (bassops, spliff,
  helterskelter, guardian), spectral (voxmod, resonance-garden).
