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
  Base rhythmic attitude: grounded, ostinato, march, pulse, drone, or climb.
- `Channel`
  MIDI output channel.
- `Form`
  Total form length in bars.
- `Phrase`
  Phrase length in bars. Phrases are the structural units inside the form.
- `Density`
  How many note onsets appear inside each phrase.
- `Motion`
  How far the line tends to move away from its current degree.
- `Tension`
  How strongly the form leans toward a later peak and climb behavior.
- `Cadence`
  How strongly the final phrase behaves like a real cadence rather than a soft release.
- `Register`
  Base octave placement.
- `Reg Arc`
  How much the register climbs across the form.
- `Sequence`
  How likely answer/release phrases are to derive their material from the previous phrase.
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

Reference docs:

- `docs/design.md`
- `docs/implementation.md`
