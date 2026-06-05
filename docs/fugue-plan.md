# Fugue generation plan

This plan tracks the first steps toward Bach-like contrapuntal behavior in the
plugins that can use it musically.

## Goals

- Append enum values only, preserving existing saved-state IDs.
- Treat `Fugue` as subject/answer/episode/cadence behavior, not just another
  scale preset.
- Keep drum behavior deliberately sparse when a drum plugin exposes the genre.
- Prefer portable core changes with focused deterministic tests.

## Checklist

- [x] Add appended `Fugue` genre values to `bassgen` and `drumgen`.
- [x] Add `Fugue` to the BassGen and DrumGen custom UIs.
- [x] Give BassGen a first subject/answer model with tonic subject, dominant
      answer, episode, and tonic pedal/cadence behavior.
- [x] Give DrumGen a sparse metrical Fugue pulse that removes normal fills,
      crashes, and kit excess.
- [x] Add deterministic tests for appended genre IDs and core Fugue behavior.
- [x] Add subject-answer behavior to `counterpointer` using existing strict
      counterpoint controls instead of a new genre selector.
- [x] Add Fugue-friendly phrase behavior to `melgen` using existing `Period`,
      `Structure`, `Leap`, `Rest`, and `Color` controls.
- [x] Add Fugue-friendly long-form behavior to `ground` using existing
      `Sequence` and `Cadence` controls.
- [x] Add Cadence support for circle-of-fifths and suspension-heavy harmonic
      backing using existing `Color`, scale, and chord-size controls.

## First Implementation Slice

`bassgen` now treats Fugue as a continuo-like bass mode. It reinforces flowing
subject steps, starts the subject on tonic, answers on dominant, uses a short
episode bar, and resolves into a tonic pedal/cadence bar. `Color` can add
leading-tone pickup behavior without changing the selected scale enum.

`drumgen` exposes the same appended genre for UI consistency, but does not try
to make a drum style out of Bach. Its Fugue output is a sparse pulse: downbeat
kick, light closed-hat beat markers, and optional quiet clave on the middle
beat.

`counterpointer` now treats high `Regularity`, high `Counter`, and low
randomness as a strict fugal answering region. In that state the incoming MIDI
cycle is the subject, and the generated line preserves subject intervals around
the dominant. High color on Jazz-capable scales can invert those intervals.

`melgen` now treats `Call Answer` period, high `Structure`, and low `Leap`/`Rest`
as a strict subject/answer region. It generates a compact scalar subject and
derives the answer at the dominant while leaving the visible control set
unchanged.

`ground` now treats high `Sequence` plus high `Cadence` as a Fugue-friendly
long-form region. It plans a stable subject entry, dominant answer, middle
entries, a penultimate pedal phrase, and a cadence while keeping the existing
form controls.

`cadence` now uses high `Color` on major/minor-style scales to support more
classical harmonic motion. Ambiguous learned material can resolve through
descending fifths, including vi-ii-V-I support, and a suspended dominant can
prepare the final tonic.

## Next Steps

- Validate BassGen Fugue in a DAW with major, minor, and harmonic-minor scales.
- Validate Counterpointer's strict fugal answering in a DAW, especially with
  BassGen Fugue driving its input.
- Validate MelGen's Fugue-friendly control region in a DAW with BassGen Fugue
  and Counterpointer.
- Validate Ground's high-Sequence/high-Cadence Fugue-friendly form in a DAW,
  especially with MelGen and Counterpointer layered above it after the bass
  register guard.
- Validate Cadence's high-Color circle-of-fifths and suspended-dominant support
  against sparse MelGen, BassGen, and Ground input.
