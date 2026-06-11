# ground

`ground` is an original `downspout` MIDI generator rather than a `flues` port.

It is meant to sit above riff-level mutation. Instead of only changing a short
loop, it plans a longer bass role across phrases and sections, then emits a
monophonic line that can move through statement, answer, climb, pedal,
breakdown, cadence, and release roles, with more syncopated pickups and longer
ties than the first `ground` build.

## Controls

- `Root`
  MIDI root note for the generated line.
- `Scale`
  Pitch collection used for note selection.
- `Style`
  Base rhythmic attitude: grounded, ostinato, march, pulse, drone, climb, dub,
  or jazz. Dub stays sparse and heavy with long low notes and pickups; Jazz
  leans toward walking-bass quarter motion with extra approach tones.
- `Form Shape`
  Structure template. `Free` uses the generic long-form planner. Named shapes
  force their own form and phrase grid, including 12-bar blues variants,
  classical and fugue arcs, jazz AABA/rhythm changes, techno, dub, ambient, and
  rondo layouts.
- `Channel`
  MIDI output channel.
- `Form`
  Total form length in bars. Named shapes override this with their fixed
  template length.
- `Phrase`
  Phrase length in bars. Phrases are the structural units inside the form.
  Named shapes override this with their template phrase grid.
- `Density`
  How many note onsets appear inside each phrase.
- `Motion`
  How far the line tends to move away from its current degree.
- `Tension`
  How strongly the form leans toward a later peak and climb behavior.
- `Color`
  Adds harmonic and melodic tension. Higher values make phrase roles less
  static, increase motion on Jazz-capable scales, and allow occasional
  chromatic pickup notes.
- `Cadence`
  How strongly the final phrase behaves like a real cadence rather than a soft release.
- `Register`
  Base octave placement. Ground folds generated notes into a lane around this
  register so bass parts do not jump across several octaves.
- `Reg Arc`
  How much the register climbs across the form. This still creates phrase lift,
  but the generated MIDI remains pinned to a bass-sized register lane.
- `Sequence`
  How likely answer/release phrases are to derive their material from the previous phrase.
  High `Sequence` with high `Cadence` enters a Fugue-friendly long-form region:
  Ground plans subject, dominant-answer, pedal, and cadence phrases while
  keeping the same visible controls.
- `Note Length`
  Caps generated note holds as a fraction of the space before the next onset.
- `Note Length Variation`
  Controls how much generated holds vary around the current style and role.
- `Seed`
  Deterministic random seed.
- `Vary`
  Form-loop mutation amount.
- `New Form`
  Regenerate the whole long-form plan.
- `New Phrase`
  Refresh the current phrase role and material.
- `Mutate Cell`
  Keep the current phrase role but rewrite its local note pattern.

## UI

The UI is structure-first:

- a top form-preview lane that shows the predicted phrase-role arc;
- editable role cells in that lane, with Auto plus concrete phrase-role choices
  for each supported phrase slot;
- status cards for the current phrase and current role;
- motion sliders on the left;
- framing selectors and one-shot actions on the right.

It is supposed to help users think in sections rather than in parameter soup.

## Current status

`ground` currently has:

- a portable form/planning core;
- text state serialization;
- deterministic core tests;
- a first DPF-backed `ground.vst3` wrapper target with a custom UI.
- scale choices now include Lydian, Melodic Minor, Whole Tone, Altered,
  diminished, and bebop colors, appended after the original scale IDs so
  saved-state scale values remain stable.
- the `Color` control is serialized and exposed in the UI, defaulting to zero
  so the original long-form behavior remains the baseline.
- high `Sequence` plus high `Cadence` gives a deterministic Fugue-friendly
  subject/answer/pedal/cadence arc without adding a dedicated genre selector.
- `Form Shape` adds named structure templates while keeping `Style` focused on
  local rhythmic behavior.
- generated notes are constrained after form generation, phrase refresh, cell
  mutation, and loop variation so Ground stays usable as a bass source even
  with high Motion, Color, Register Arc, or Vary settings.

Reference docs:

- `docs/design.md`
- `docs/implementation.md`
