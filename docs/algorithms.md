# Downspout Algorithms

This note summarizes the main generation and processing methods used by the
current Downspout plugins.

## MIDI generators

- `BassGen`: transport-synced monophonic bass generation with style and scale
  vocabularies, rhythm regeneration, color-driven harmonic tension, MIDI
  follow/dodge behavior, Jazz-specific ii-V-I / turnaround targeting, and
  Fugue subject/answer bass behavior.
- `DrumGen`: transport-aware drum pattern generation with fill logic, style
  modes, genre vocabularies, pattern mutation around the current cycle, and a
  sparse Fugue pulse mode.
- `MelGen`: phrase-aware monophonic melody generation with contour, call and
  answer, structure, range, color, follow controls, and a high-structure
  Fugue-friendly subject/dominant-answer region.
- `Ground`: longer-horizon bass generation that plans phrase roles, movement,
  cadences, pedal phrases, color, and high-sequence subject/answer forms across
  sections rather than just local bars, with explicit Dub and Jazz styles and
  generated notes folded into a guarded bass register lane.
- `Cadence`: learned harmony generation from captured MIDI, with scale-aware
  voicing, chord-size modes, comp scheduling, variation, arpeggiated playback
  phrasing, Jazz ii-V-I bias, and high-color circle-of-fifths/suspended
  dominant support.
- `Counterpointer`: learns an incoming MIDI line and answers it with a
  monophonic complementary line that can lean into contrary motion, chromatic
  color, rhythmic response, and strict dominant-answer imitation.
- `Lifeform`: Conway's Game of Life drives the MIDI output, advancing the grid
  one beat at a time and turning active cells into musical events.
- `Luma`: an 8x8 Launchpad grid drives a small set of musical agents where
  cells can act as bass, chord, melody, or drum sources.

## Audio effects

- `P-Mix`: transport-aware probabilistic audio gating and blending with fades.
- `E-Mix`: Euclidean stereo gating that carves audio into repeating patterns
  using density, block, and fade controls.
- `Rift`: short-buffer capture and disruption with chop/stutter repeats,
  reverse, skip, smear, and pitch-slip actions.
- `PaunchLad`: a dub-style performance effect built around delay throws,
  sirens, spring splashes, dropouts, and rhythmic chops.

## MIDI effects

- `M-Mix`: MIDI gating that combines transport-locked probability decisions
  with Euclidean block patterns.
- `Gremlin Driver`: modulation and action sequencing that emits CC movement,
  action notes, and patch-randomization bursts for `Gremlin`.
- `Cadence`: input-aware MIDI harmonizing and comping that learns a cycle,
  rebuilds voicings from captured material, and can thin or break chords with
  `Spread` and `Arpeggio`.

## Instruments

- `Basilico`: a monophonic bass synth with glide, accent, body, drive,
  tempo-aware wobble modulation for amplitude/filter/phase movement, and acid
  squelch across upright, electric, dub, acid, and industrial tones.
- `DrumKit`: a triggered drum synthesizer with a fixed MIDI map, voice
  controls, and mixer-style mute strips.
- `Floozy`: an 8-voice hybrid synth combining distortion, physical-model-style
  excitation, feedback, filtering, modulation, and reverb.
- `Gremlin`: a chaotic glitch synth with sound modes, scenes, fader macros,
  performance actions, hold pads, and MIDI LED feedback.
- `Canticle`: a 12-voice tonal synth for readable keys, reed, pad, pluck, and
  glass parts driven by melody, counterpoint, and harmony generators.

## Appendix: Ground Algorithm Detail

Ground generates a monophonic bass part in three passes: first it plans the
form, then it generates phrase-local note events, then it folds the resulting
notes into a guarded bass register.

### 1. Form And Phrase Planning

The structural grid comes from `Form Shape`, `Form`, `Phrase`, and the current
meter. With `Form Shape = Free`, Ground uses the user-selected `Form` and
`Phrase` lengths directly. A common setting is `Form = 16` and `Phrase = 4`,
which creates four four-bar phrase slots. Named shapes such as blues, fugue,
AABA, dub, ambient, and rondo override those lengths with fixed templates.

Each phrase receives a `PhraseRoleId`:

- `Statement`: introduces or restates the bass idea.
- `Answer`: responds to a previous phrase.
- `Climb`: raises intensity and usually lifts register/root target.
- `Pedal`: stabilizes around a low anchor.
- `Breakdown`: thins the line.
- `Cadence`: aims at a stronger ending.
- `Release`: relaxes or resolves with less force than a cadence.

For `Free` shapes, the first phrase is normally `Statement`, the final phrase is
usually `Cadence` or `Release`, and the middle phrase roles are chosen from
`Tension`, `Cadence`, `Sequence`, phrase position, and deterministic seeded
randomness. Manual phrase-role overrides bypass that role choice for selected
phrase slots while keeping the rest of the phrase generation pipeline intact.

After role selection, each phrase stores:

- a root scale degree target;
- a register offset from the long-form `Register Arc`;
- a role intensity that affects density and velocity;
- a motion bias derived from `Motion`, role, and sometimes `Color`.

### 2. Scale Degrees And Root Movement

Ground works in scale degrees first, not raw MIDI notes. For F harmonic minor,
the pitch collection is:

```text
degree: 0  1  2   3   4  5   6
pitch:  F  G  Ab  Bb  C  Db  E
```

Degrees can move outside `0..6`. For example, degree `-1` is the E below F,
degree `7` is the next F, and degree `8` is the next G. Only after the degree
is chosen does Ground convert it to a MIDI note using the selected root,
register, phrase register offset, and octave.

Phrase roles choose different root targets. In the generic planner:

- `Statement` tends to target degree `0` or `2` (F or Ab in F harmonic minor).
- `Answer` tends toward degree `3` or `4` (Bb or C).
- `Climb` tends toward degree `4` or `5` (C or Db).
- `Pedal` favors degree `0` or `4` (F or C).
- `Cadence` tends toward degree `4` or `5` (C or Db).
- `Release` tends toward degree `0` or `2` (F or Ab).

The exact choice is seeded, so the same saved state reproduces the same result.

### 3. Rhythm And Onset Generation

Each phrase builds a small boolean onset grid. Ground uses four internal steps
per beat, so a normal 4/4 bar has 16 steps. The `Style` control lays down the
basic rhythmic feel, then the role adds pickups, cadential hits, or extra
off-beat material.

For `Style = Descend`, each bar starts with anchors at:

```text
step 0  : bar start
step 8  : half-bar
step 12 : late-bar anchor when Motion is high enough, or for cadences
step 4  : optional beat-2 anchor, based on Density and Motion
step 14 : optional late pickup when Color or cadence behavior asks for it
```

`Statement`, `Climb`, `Cadence`, and `Release` then adjust that grid. For
example, `Climb` may add a mid-bar push at step 6 or 14, while `Release` may
keep the phrase more open.

### 4. Degree Choice Inside A Descending Phrase

For the Descend style, the pitch contour is explicitly falling through the
phrase. Ground computes phrase progress from the local step, then derives a
`descent` amount from `Motion`, `Color`, and elapsed phrase position.

In simplified terms:

```text
descent ~= floor(progress * (3 + Motion * 3 + Color * 1.5))
```

The role then modifies that falling line:

- `Statement`: starts at the phrase root target and descends from it.
- `Answer`: starts about two scale degrees above the phrase root target, then
  descends.
- `Climb`: starts with a lift, then descends more slowly; the role still feels
  energized even though the style is descending.
- `Pedal`: mostly repeats the phrase root target, with occasional lower motion.
- `Breakdown`: limits descent to a smaller range.
- `Cadence`: approaches stronger ending tones and can resolve to degree `0`.
- `Release`: descends more gently and may continue relaxing every few notes.

This is why a Descending style does not simply override every role into the
same falling scale. Role still matters; Descend changes how each role moves.

### 5. Duration, Legato, And Register Guard

Once an onset has a degree, Ground chooses a duration up to the next onset.
`Note Length` sets the cap, `Note Length Variation` adds seeded variation, and
style/role modify the preferred value. Descend leans longer than ostinato,
march, jazz, or rock. `Release`, `Pedal`, and `Cadence` also get extra legato.

Adjacent repeated notes can be merged into a longer event. After all events are
generated, notes are folded into the selected bass lane:

- `Register` chooses the base octave lane.
- `Register Arc` permits phrase-level lift across the form.
- `Clamp` octave-folds notes into a fixed semitone range above the selected
  root in that register.

This final pass is why a high computed degree may still sound as a controlled
bass-register pitch.

### Example: F Harmonic Minor, Free Shape, Descending Style

Assume:

```text
Root: F
Scale: Harmonic Minor
Style: Descend
Shape: Free
Form: 16 bars
Phrase: 4 bars
Phrase roles: Statement, Climb, Climb, Release
```

With four-bar phrases, the 16-bar form has four phrase slots:

```text
bars  1-4  : Statement
bars  5-8  : Climb
bars  9-12 : Climb
bars 13-16 : Release
```

One seeded form might choose root degree targets like this:

```text
Phrase 1 Statement: degree 0  -> F
Phrase 2 Climb    : degree 4  -> C
Phrase 3 Climb    : degree 5  -> Db
Phrase 4 Release  : degree 0  -> F
```

The exact targets can vary by seed, but the role pools stay the same. In F
harmonic minor, the useful degrees around those targets are:

```text
around F : F, E, Db, C, Bb, Ab
around C : C, Bb, Ab, G, F, E
around Db: Db, C, Bb, Ab, G, F
```

So the example has this larger shape:

- Phrase 1 states a falling F-minor bass idea.
- Phrase 2 raises the target area to C, then lets Descend pull the line down.
- Phrase 3 raises the target area again to Db, creating more tension before
  falling.
- Phrase 4 releases back toward F and relaxes the motion.

At a phrase level, this can sound like a bass that keeps falling locally while
the form itself still rises into the two Climb phrases before settling. That is
the intended interaction between `Role = Climb` and `Style = Descend`: the role
sets phrase energy and target area, while the style determines the local
contour.
