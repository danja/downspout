# Auricle-algo review (2026-11-06)

Source: `~/github/auricle-algo` (Python MIDI generator/DAW). Reviewed for
potential improvements to Downspout's `drumgen`, `bassgen`, `melgen`,
`counterpointer` and `cadence`. Portable-core constraints (C++, no DPF in core)
apply; anything adopted must be reimplemented.

## Drumgen

- **Role-grid / rhythm-map / metric engines.** Auricle offers three rhythm
  engines: a classic role grid, a bilinearly-morphed "rhythm map" (4 authored
  16-step skeletons morphed by X/Y coordinates over any meter length, with
  group-boundary preference), and a Metric/Barlow engine. Downspout drumgen has
  CA lanes + euclidean; a morphable groove-map with X/Y macro controls and
  metric-group awareness is a candidate feature.
- **Voice links (Relations).** A relation graph between drum voices: 9
  operations (support/OR, answer/call-response, complement/NOT, AND, XOR, copy,
  gap-fill, interlock, accent) with amount/probability/delay/phase. Downspout
  drumgen has no inter-voice relations; this is a large but high-value addition.
  The immutable-snapshot + canonical-ordering implementation is a good model for
  determinism.
- **Motif states.** Theme/Variation/Answer/Fill/Return phrase states with a
  replayable, seek-safe transition window. Downspout has no phrase-state layer
  in drumgen.
- **Density retention.** `retained()` removes ornaments before support, keeps
  metric anchors; metric anchors immutable. Worth adopting for Downspout's
  density control.
- **Exclusion pass.** Final ownership pass ensures no double hats (closed vs
  open) / snare vs clap. Downspout drumgen may benefit from a similar
  conflict-resolution pass.

## Bassgen

- **Harmony-driven bass.** Auricle's harmonic plan (chord progressions with
  meter, inversions, transitions, voice leading) drives bass. Downspout bassgen
  is more pattern/sequence based; a chord-progression input mode + voice-led
  bass line following the progression (root/fifth/third) is a candidate.
- **Voice leading solver.** `VoiceLeading.resolve_chord` places chord voices
  with register constraints and smoothness; `resolve_pitch` picks the octave
  closest to the previous note. Downspout has local voice-leading in
  harmonic-atlas; a shared helper (needs approval) would benefit bassgen and
  counterpointer.

## Melgen

- **Scale-aware Markov with rests/octaves/leaps.** Markov melody with rest
  probability, octave range, stay probability, leap probability. Downspout
  melgen/markov could adopt the rest + octave-range + leap-probability params.
- **Random walk with return bias, drift, bounded range.** Downspout melgen has
  inertia; adding drift and return-bias knobs is cheap.
- **Scale walker.** Degree-index walker with direction bias, leap probability,
  range in octaves. Candidate generator mode.
- **Weighted scale.** Degree weights + octave spread. Candidate.
- **Arpeggiator.** Pattern-driven arpeggio with direction and octave modes.
  Downspout has arpgen; could share.
- **Chaos/logistic, L-system, cellular automaton, de Bruijn, Thue-Morse,
  Fibonacci, Recamán.** Downspout already has CA lanes and some. L-system and
  logistic chaos are novel candidates.
- **Quantize-to-scale ties resolve down** (matches Downspout convention). OK.

## Counterpointer

- **Voice leading with smoothness and max-leap.** The `resolve_pitch` octave
  selection with smoothness 0..1 and leap limit is a clean model. Downspout
  counterpointer could expose a "smoothness" control.
- **Chord tone selection by shared tones.** `shared_tones` between consecutive
  chords. Useful for counterpointer's note choice.

## Cadence

- **Chord progressions with meter and voice leading.** Auricle's harmonic plan
  is richer than cadence's learned-transition model. Candidate: a
  progression-input mode (user chord list) in addition to learned transitions;
  voice-led realisation of the progression.
- **Gravity bias.** Already noted in TODO; Auricle's harmonic plan could inform
  the bias design.
- **Custom chord qualities / inversions / transitions (smooth vs fixed).**
  Downspout cadence could expose inversion and transition-type controls.

## Cross-cutting

- **Deterministic RNG with explicit seed** (Python `random.Random`) — matches
  Downspout's approach; no change needed.
- **Rests as None vs 0.0** distinction — Downspout uses 0 for rest in some
  places; consider explicit rest sentinel where 0 is a valid note. Low priority.

## Recommendation

Highest-value, lowest-risk: adopt the **voice-leading helper** (shared, needs
approval) and the **Markov rest/octave/leap params**. Largest: voice links for
drumgen. New generator modes (L-system, logistic) are optional extras.

No code changes made by this review; findings recorded here for follow-up.
