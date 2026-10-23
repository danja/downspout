---
layout: default
title: Algorithms
description: A summary of the generation and processing methods used by the current Downspout plugins.
permalink: /algorithms/
---

# Downspout Algorithms

This note summarizes the main generation and processing methods used by the
current Downspout plugins.

## MIDI generators

- [`BassGen`]({{ '/plugins/bassgen/' | relative_url }}): transport-synced monophonic bass generation with style and scale
  vocabularies, rhythm regeneration, color-driven harmonic tension, MIDI
  follow/dodge behavior, Jazz-specific ii-V-I / turnaround targeting, and
  Fugue subject/answer bass behavior.
- [`DrumGen`]({{ '/plugins/drumgen/' | relative_url }}): transport-aware drum pattern generation with fill logic, style
  modes, genre vocabularies, pattern mutation around the current cycle, and a
  sparse Fugue pulse mode. An optional automaton layer treats the generated pattern as
  generation 0 and plays the targeted lanes (hats, percussion, toms) as successive rows
  of a 1-D cellular automaton (rules 30, 90, 110, 150, ...), as in `Xoxolo`.
- [`MelGen`]({{ '/plugins/melgen/' | relative_url }}): phrase-aware monophonic melody generation with contour, call and
  answer, structure, range, color, follow controls, and a high-structure
  Fugue-friendly subject/dominant-answer region.
- [`Ground`]({{ '/plugins/ground/' | relative_url }}): longer-horizon bass generation that plans phrase roles, movement,
  cadences, pedal phrases, color, and high-sequence subject/answer forms across
  sections rather than just local bars, with explicit Dub and Jazz styles and
  generated notes folded into a guarded bass register lane.
- [`Cadence`]({{ '/plugins/cadence/' | relative_url }}): learned harmony generation from captured MIDI, with scale-aware
  voicing, chord-size modes, comp scheduling, variation, arpeggiated playback
  phrasing, Jazz ii-V-I bias, and high-color circle-of-fifths/suspended
  dominant support.
- [`Counterpointer`]({{ '/plugins/counterpointer/' | relative_url }}): learns an incoming MIDI line and answers it with a
  monophonic complementary line that can lean into contrary motion, chromatic
  color, rhythmic response, and strict dominant-answer imitation.
- [`Markov`]({{ '/plugins/markov/' | relative_url }}): a walk over the twelve pitch classes from Root through a 12 x 12
  transition matrix (weights 0-8, editable, or replaced by a Style preset). Out-of-scale
  columns are zeroed, Chaos applies the exponent `2^(1-2c)` to the weights, and Order 2
  uses learned second-order pair data where it exists. Learn counts transitions on the
  MIDI input and blends them in row by row. Each phrase restarts from the tonic, seeded,
  so a note is a pure function of its position.
- [`Sprout`]({{ '/plugins/sprout/' | relative_url }}): L-system melody. Seven built-in grammars (Plant, Koch, Dragon, Sierpinski,
  Cantor, Levy, Tree), or a user-typed Custom one, are expanded to a symbol-count cap, falling back to the deepest
  generation that fits; tables are prebuilt, so nothing allocates in the audio thread.
  Symbols map to scale steps or rests on a transport grid; Grow Every adds a generation
  every N bars. Optionally the pitch source is the held input notes.
- [`Worms`]({{ '/plugins/worms/' | relative_url }}) (ToneWorm): Paterson's Worm on a Tonnetz lattice. Six directional rules each
  choose among L120, L60, Straight, R60, R120; the visited lattice points are quantised
  to the scale and register, with loop-boundary variation. Randomize and Mutate make new
  rule sets.
- [`Polymeter`]({{ '/plugins/polymeter/' | relative_url }}): four lanes, each a Euclidean pattern (length, pulses, rotation) with
  ratchets, probability and accent. Coprime lengths drift against each other. A lane
  can instead run a 1-D cellular automaton (rule 1-255) whose row evolves each lane
  cycle, with Pulses setting the initial density.
- [`Xoxolo`]({{ '/plugins/xoxolo/' | relative_url }}): x0x step sequencer, 11 drum lanes, up to 32 steps. An optional Evolve layer
  plays marked lanes as successive rows of a 1-D cellular automaton seeded from the
  programmed grid (the pass number comes from playback position, returning to the grid
  after 64 generations).
- [`Harmonic Atlas`]({{ '/plugins/harmonic-atlas/' | relative_url }}): autonomous chord generator over four movement families (tonal,
  modal, chromatic-mediant, neo-Riemannian). Gravity swaps a chord for a functional
  target (V to I, IV to V, ...) with probability up to 85%; voice-leading picks
  inversion and octave by a sorted-voice-distance search against the previous chord.
- [`Sidecar`]({{ '/plugins/sidecar/' | relative_url }}): phrase player with deterministic local generation, and an optional
  localhost coordinator mode that accepts phrases from an external process.
- [`Tuney-vst`]({{ '/plugins/tuney-vst/' | relative_url }}): text-to-music. Unicode text is mapped through configurable alphabets,
  scales and microtonal tunings to seeded free-time phrasing, played by a small internal
  synth and emitted as MIDI.
- [`Conductor`]({{ '/plugins/conductor/' | relative_url }}): arrangement controller. A fixed form (Intro, Develop, Break, Reprise,
  Coda) or seeded weighted section selection with bounded durations; at each section
  boundary it emits bar-aligned notes and CC 20-24 (Scene, Density, Energy, Mutation,
  Reset) that the other generators read through their Conductor channel.

## Audio effects

- [`P-Mix`]({{ '/plugins/p-mix/' | relative_url }}): transport-aware probabilistic audio gating and blending with fades.
- [`E-Mix`]({{ '/plugins/e-mix/' | relative_url }}): Euclidean stereo gating that carves audio into repeating patterns
  using density, block, and fade controls.
- [`Rift`]({{ '/plugins/rift/' | relative_url }}): short-buffer capture and disruption with chop/stutter repeats,
  reverse, skip, smear, and pitch-slip actions.
- [`Qune`]({{ '/plugins/qune/' | relative_url }}): pitch quantiser. A normalised-square-difference pitch tracker (decimated
  search, full-rate refinement, octave-error checks) picks a target note from a scale or
  from held MIDI notes; the shift is smoothed by an optional glide and applied by one of
  three shifters: a pitch-synchronous splice delay line (Fast), time-domain PSOLA with
  fractional pitch marks (Good), or a peak-locked phase vocoder with cepstral formant
  correction (Best).
- [`Ambo`]({{ '/plugins/ambo/' | relative_url }}): four rearrangeable module chains (granular smear, frozen-band motion, tape
  colour, shimmer, ping-pong echo, saturation, regenerative feedback).
- [`Chipper`]({{ '/plugins/chipper/' | relative_url }}): sample-and-hold rate reduction (divider 1-64, optional jitter) followed by
  midtread bit quantisation (1-16 bits).
- [`Damiano`]({{ '/plugins/damiano/' | relative_url }}): six waveshapers (Soft, Tanh, Fuzz, Overdrive, Tube, Wavefold) behind a
  one-pole high-shelf tone control; the right channel can take its own settings.
- [`Skream`]({{ '/plugins/skream/' | relative_url }}): ADAA2 anti-aliased tanh saturator into an SVF low-pass, with a high-passed,
  saturated feedback path gated by an envelope-follower expander. Morph crossfades the
  forward SVF between low-pass and high-pass by cutoff position.
- [`BassOps`]({{ '/plugins/bassops/' | relative_url }}): sidechain envelope follower ducks the main signal, which is then summed to
  mono; a waveshaper's added harmonics, high-passed at the cutoff, form the side signal.
- [`T-Mix`]({{ '/plugins/t-mix/' | relative_url }}): eight-input mixer with level, pan, mute, solo and meters; Mixgen CCs apply
  click-smoothed transient gain multipliers over the saved faders.
- [`Loopdelay`]({{ '/plugins/loopdelay/' | relative_url }}): stereo delay and capture looper, time free (20-4000 ms) or BBT-synced.
- [`Lightverb`]({{ '/plugins/lightverb/' | relative_url }}): fixed-cost stereo feedback-delay-network reverb.
- [`Guardian`]({{ '/plugins/guardian/' | relative_url }}): output safety chain: gain, look-ahead limiter, selectable clipper, DC
  removal, true-peak guard, silence detection and latched fault diagnostics.
- [`Beards`]({{ '/plugins/beards/' | relative_url }}): granular processor after Mutable Instruments Beads. Input is recorded into
  a 4 s buffer and replayed by up to 32 overlapping grains (position, size, shape,
  pitch, density, scatter), with feedback, a Griesinger reverb, freeze, and optional
  host-grid grain triggering.
- [`Trio`]({{ '/plugins/trio/' | relative_url }}): two 4th-order Linkwitz-Riley splits in double precision (each LR4 is two identical Butterworth cells).
  Low = AP_H(LP_L(x)), mid = LP_H(HP_L(x)), high = HP_H(HP_L(x)), where AP_H = LP_H + HP_H of the high crossover; the sum is
  AP_L AP_H, an exact all-pass. Each band's detector is a peak hold, d = max(max(|L|, |R|), d e^(-1/tau)) with tau about two periods of
  the band's lowest frequency. The gain computer is -(L - Td)(1 - 1/Rd) above the down threshold and +min((Tu - L)(1 - 1/Ru), limit) below
  the up threshold; the gain in dB is smoothed with the attack coefficient when the target is lower and the release coefficient otherwise.
- [`Octet`]({{ '/plugins/octet/' | relative_url }}): eight RBJ-cookbook biquads in double precision, transposed direct form II. Bell, shelf, notch and
  12 dB cut cells take the band's own Q (the cut uses 1/sqrt 2); a 48 dB cut is an 8th-order Butterworth made of four cells with
  Q = 1 / (2 cos((2k - 1) pi / 16)) for k = 1..4, so it is -3 dB at the cutoff. Mid/Side uses the orthonormal matrix
  (l + r, l - r) / sqrt 2 and back. The plot multiplies each cell's |H(e^jw)| from the same coefficients the audio uses.
- [`Glue`]({{ '/plugins/glue/' | relative_url }}): feed-forward compressor. Each channel is high-passed (Butterworth, detector only), then
  measured as |x| or a 10 ms mean square; the louder channel's level in dB goes through the soft-knee computer of
  Giannoulis et al., GR = -(1 - 1/R)(L - T + W/2)^2 / 2W inside the knee and -(1 - 1/R)(L - T) above it, capped at
  Range. The gain is one-pole smoothed in dB, with the attack coefficient when the target is lower than the current gain
  and the release coefficient otherwise. Auto release sets the release to 40 + 760 clamp(d / 8 dB) ms, where d is a
  400 ms average of the demanded reduction (not of the smoothed gain, which would lengthen its own release). Soft clip is
  x up to 0.5, then 0.5 + 0.5 tanh((|x| - 0.5) / 0.5).
- [`Ladder`]({{ '/plugins/ladder/' | relative_url }}): four-pole zero-delay-feedback low-pass ladder after Mystran, run at
  2x with a tanh at each integrator and the cutoff prewarped for the doubled rate. The tanh(x)/x
  secants come from a Pade [4/4] rational (exact asymptote beyond |x| = 4.5), so the stage gains
  are solved in closed form with no iteration. Resonance is the feedback amount k, mapped
  k = 4.2 (1 - 10^-u) / 0.9 so the peak height rises roughly evenly in dB (k >= 4 rings). Bass Comp
  multiplies the output by 1 + c k, which undoes the 1 / (1 + k) low-frequency loss. The plotted
  curve is H = G^4 / (1 + k G^4) with G = 1 / (1 + jf/fc), scaled by Bass Comp, Mix and Output.
- [`Grist`]({{ '/plugins/grist/' | relative_url }}): granular grain delay. Input history in a 7 s ring buffer; one grain per
  Window / Density, each a four-point Hermite read with a Hann/percussive window of equal mean level.
  A grain starts Blur x Window x u samples back, plus (ratio - 1) x length when pitched up so it never
  catches the write position, and reads forwards or backwards (Reverse). Its pitch is Pitch plus a draw
  within Range, restricted to Scale (the total shift's pitch class counted from the input), its
  length is shortened by Variance, and its pan comes from Spread; all draws use a seeded xorshift
  generator. Feedback passes the grain output through a delay, an optional state-variable filter and a
  tanh limiter back into the history.
- [`Sam`]({{ '/plugins/sam/' | relative_url }}): speech synthesis by SAM (Software Automatic Mouth, 1982). Text goes through
  a rule-based reciter to phonemes, which become 10 ms frames of three formant frequencies and amplitudes and a glottal
  period; each glottal pulse restarts two sine formants and a rectangular third, summed into an 8-bit, 22.05 kHz buffer.
  In singing mode the pitch byte is the glottal period, so f0 = 6805.56 / byte; a key is rendered at the nearest whole
  period and resampled into tune. Words are scheduled against the host BBT at the first frame at or after each step.
- [`Orchid`]({{ '/plugins/orchid/' | relative_url }}): voiced freeze. Autocorrelation capture and grid-synchronised loop holds turn
  incoming material into sustained rhythmic freezes.
- [`Keyframe`]({{ '/plugins/keyframe/' | relative_url }}): extrema-sampling time stretch (Nielsen, DAFx26). Input becomes
  timestamped local extrema whose spacing sets content-adaptive overlap-add splices;
  no transient detector.
- [`Quefrency`]({{ '/plugins/quefrency/' | relative_url }}): cepstral formant and harmonic shifter. Each frame is split via the
  cepstrum into formant envelope and harmonic excitation, shifted and reshaped
  independently, then multiplied back.
- [`Primefold`]({{ '/plugins/primefold/' | relative_url }}): direct 2x, 3x, 5x pitch voices (dual-tap resamplers) recirculated
  all-to-all through a short delay and damping low-pass, so 4x, 6x, 8x, 9x, 10x emerge
  from feedback. Feedback is capped at 0.90.
- [`Voxmod`]({{ '/plugins/voxmod/' | relative_url }}): a filter-bank vocoder and a band-limited ring modulator in parallel, blended
  by Mix; Sync feeds the ring-modulated signal back as the vocoder carrier.
- [`Sinevox`]({{ '/plugins/sinevox/' | relative_url }}): LPC analysis of the speech input finds up to eight formants; these drive a
  parallel band-pass vocoder bank on the carrier, one sine per formant (sine-wave
  speech), or both.
- [`Resonance Garden`]({{ '/plugins/resonance-garden/' | relative_url }}): up to twelve bounded damped resonators tuned by held notes or an internal
  scale (23 to choose from), with inharmonicity, freeze and bounded feedback.
- [`Orbit`]({{ '/plugins/orbit/' | relative_url }}): seeded orbit, pendulum, random-walk and figure-eight trajectories applied as
  stereo placement, distance filtering and conservative Doppler. No HRTF.
- [`Helterskelter`]({{ '/plugins/helterskelter/' | relative_url }}): automatic wah; a resonant low-pass whose cutoff follows the input
  envelope and/or a BBT-synced ADSR cycle.
- [`Spliff`]({{ '/plugins/spliff/' | relative_url }}): 3-band transient detector applies dynamic cuts or boosts only at
  transients (mono detector, so the image is preserved).
- [`Treatment`]({{ '/plugins/treatment/' | relative_url }}): acoustic panel model: facing mass, porous fill and air gap form a
  mass-air-mass resonator, `w0 = c * sqrt(rho0 / (m D))`, with an impedance match at
  `r = rho0 c`.
- [`Gater`]({{ '/plugins/gater/' | relative_url }}): routes a stereo input to one of two output pairs by incoming note parity
  (even to Output 1, odd to Output 2).

## MIDI effects

- [`M-Mix`]({{ '/plugins/m-mix/' | relative_url }}): MIDI gating that combines transport-locked probability decisions
  with Euclidean block patterns.
- [`Gremlin Driver`]({{ '/plugins/gremlin-driver/' | relative_url }}): modulation and action sequencing that emits CC movement,
  action notes, and patch-randomization bursts for `Gremlin`.
- [`Cadence`]({{ '/plugins/cadence/' | relative_url }}): input-aware MIDI harmonizing and comping that learns a cycle,
  rebuilds voicings from captured material, and can thin or break chords with
  `Spread` and `Arpeggio`.
- [`ArpGen`]({{ '/plugins/arpgen/' | relative_url }}): transport-synced arpeggiator. Chord mode captures incoming chord slices;
  Scale mode derives runs, triads or sevenths from held register anchors; up, down and
  alternating orders over one to four octaves.
- [`Mnemosyne`]({{ '/plugins/mnemosyne/' | relative_url }}): phrase memory of eight bounded phrases; accompanies input or generates
  from recalled and transformed material, with an internal motif for an empty reservoir.
- [`Retune`]({{ '/plugins/retune/' | relative_url }}): sends one note per MIDI channel (2-16) with a pitch bend that moves the
  nearest 12-TET key onto the Scala scale degree.
- [`Midiscribe`]({{ '/plugins/midiscribe/' | relative_url }}): records MIDI into a rolling window and exports a Type 0 SMF at 480 PPQ.
- [`Flues Synth Driver`]({{ '/plugins/flues-synth-driver/' | relative_url }}): converts plugin parameters to CCs for the Flues synth with 31
  programs; only changed parameters are sent.
- [`Drift`]({{ '/plugins/drift/' | relative_url }}): four lanes, each an LFO, sample-and-hold, bounded random walk, deterministic
  chaos source or audio envelope follower, emitting CC (default 1-4).
- [`Mixgen`]({{ '/plugins/mixgen/' | relative_url }}): eight repeatable gain lanes (random, low-discrepancy or Euclidean) sending
  CC 20-27 to T-Mix, plus macros for Loopdelay and Lightverb.
- [`Oracle`]({{ '/plugins/oracle/' | relative_url }}): bounded analysis of level, onset, brightness, density and pitch class,
  mapped to rate-limited CC and note responses with a refractory interval.
- [`Ghost`]({{ '/plugins/ghost/' | relative_url }}): audio-triggered ghost notes quantised to the transport 16th grid; onsets
  accent, quieter fills land on off-16th slots by Density and Drag.

## Instruments

- [`Basilico`]({{ '/plugins/basilico/' | relative_url }}): a monophonic bass synth with glide, accent, body, drive,
  tempo-aware wobble modulation for amplitude/filter/phase movement, and acid
  squelch across upright, electric, dub, acid, and industrial tones.
- [`DrumKit`]({{ '/plugins/drumkit/' | relative_url }}): a triggered drum synthesizer with a fixed MIDI map, voice
  controls, and mixer-style mute strips.
- [`Floozy`]({{ '/plugins/floozy/' | relative_url }}): an 8-voice hybrid synth combining distortion, physical-model-style
  excitation, feedback, filtering, modulation, and reverb.
- [`Gremlin`]({{ '/plugins/gremlin/' | relative_url }}): a chaotic glitch synth with sound modes, scenes, fader macros,
  performance actions, hold pads, and MIDI LED feedback.
- [`Magneto`]({{ '/plugins/magneto/' | relative_url }}): a physically informed four-stroke engine. A phasor at `RPM/120`
  Hz drives intake-valve, exhaust-valve, piston and ignition functions; each
  cylinder is a digital waveguide whose feedback follows its valves and whose
  length follows its piston; cylinders feed intake runners and extractors, and
  the extractors feed a straight pipe, a four-element muffler and a tailpipe.
  Intake, block vibration and tailpipe are mixed for a listening position.
- [`Canticle`]({{ '/plugins/canticle/' | relative_url }}): a 12-voice tonal synth for readable keys, reed, pad, pluck, and
  glass parts driven by melody, counterpoint, and harmony generators.
- [`Pratt`]({{ '/plugins/pratt/' | relative_url }}): Pratt polynomials `f_1 = 1`, `f_2 = x`, `f_p = 1 + f_{p-1}` (odd prime p),
  `f_n = prod f_p^v`, so `f_n(2) = n` and `f_mn = f_m f_n`. The filter
  `H_n(s) = n / f_n(2 + s/w0)` is all-pole, unity at DC and strictly stable (every
  pole has `Re(s/w0) < -0.997` for n up to 8192; degree at most 13). Cascading two
  filters is the product of indices, `H_m H_n = H_mn`. Voices sum partial k with
  weight `k^-roll * H_n(i xi k)` for `n = (note + 1) x base` into a band-limited
  table; the filter mode factors the roots into biquads (bilinear transform, no
  prewarp) and crossfades old and new cascades when index or cutoff changes. An
  optional Timbre Index `t` multiplies one more fixed response `H_t` onto every
  voice (cascade, not a combined index), applied while the table is built and
  skipped at `t = 1`.

## Launchpad plugins

Plugins driven by, or giving LED feedback to, a Launchpad grid.

- [`Lifeform`]({{ '/plugins/lifeform/' | relative_url }}): Conway's Game of Life drives the MIDI output, advancing the grid
  one beat at a time and turning active cells into musical events.
- [`Luma`]({{ '/plugins/luma/' | relative_url }}): an 8x8 Launchpad grid drives a small set of musical agents where
  cells can act as bass, chord, melody, or drum sources.
- [`Plank`]({{ '/plugins/plank/' | relative_url }}): eight strings, one per Launchpad grid column. Each column's eight
  rows are one octave of the current scale. Two 32-bit phase oscillators are
  summed as a polyBLEP saw pair with the second inverted; a morph axis
  crossfades that into a band-limited wavetable pair read at a quarter-cycle
  offset, and the result feeds a two-pole resonant filter whose coefficient is
  modulated by a second envelope.
- [`Moka`]({{ '/plugins/moka/' | relative_url }}): modal "hit object" synth; eight damped modes per voice from six instrument
  tables, with the onset ramped over the mallet contact time.
- [`Syrinx`]({{ '/plugins/syrinx/' | relative_url }}): Mindlin-Laje syrinx ODE solved per voice at audio rate (optional
  oversampling), a pitch-tracking vocal-tract band-pass, formant bank, respiration and
  roughness modifiers.
- [`Bubbles`]({{ '/plugins/bubbles/' | relative_url }}): subtractive noise layers plus physical models (up to 36 bubble voices, 16
  drip voices, modal body resonances) in seven water modes.
- [`Campione`]({{ '/plugins/campione/' | relative_url }}): multi-zone sampler with pitch-shifting to target, per-zone ADSR and
  biquad, crossfaded loops, REX2 slices and wavetable import.
- [`Mosaic`]({{ '/plugins/mosaic/' | relative_url }}): four-slot WAV sampler with seeded slice selection, zero-crossing snapping,
  reverse chance and autonomous triggering.
- [`Trajectory`]({{ '/plugins/trajectory/' | relative_url }}): the oscillator is a point bouncing inside a regular polygon (3-12
  sides); the waveform is its x and y position, mixed.
- [`Fathom`]({{ '/plugins/fathom/' | relative_url }}): four-voice drone: a generator (Analog, Shaper, Sines, Table, Cluster), a
  per-voice notch, then a delay network with a frequency shifter in its feedback loop.
- [`PaunchLad`]({{ '/plugins/paunchlad/' | relative_url }}): a dub-style performance effect built around delay throws,
  sirens, spring splashes, dropouts, and rhythmic chops.

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
