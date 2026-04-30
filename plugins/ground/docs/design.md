# ground design

## Intent

`ground` exists because `downspout` already has short-loop generators and
transport-aware effects, but it did not have a plugin that thinks in larger
musical spans.

The target use is a bass part that can support a section over time rather than
only produce a local pattern.

## Core idea

The plugin works on three scales at once:

1. `Form`
   The whole 8, 16, 32, or 64-bar span.
2. `Phrase`
   The repeated structural chunk inside that form.
3. `Cell`
   The local note pattern inside a phrase.

That separation matters. The form layer decides where the lift, release, and
cadence happen. The cell layer only supplies note motion inside that plan.

## Phrase roles

The planned phrases use a small role vocabulary:

- `Statement`
- `Answer`
- `Climb`
- `Pedal`
- `Breakdown`
- `Cadence`
- `Release`

Those are not just labels for the UI. They push note density, root-degree
choice, register offset, and motion bias in the core.

## User-facing design choices

The UI is built around structure rather than around every internal random
choice:

- the top band shows the phrase arc for the current form settings;
- the live status cards expose the engine’s current phrase and role;
- the lower panels split movement controls from framing controls;
- the action buttons operate at musically distinct levels: whole form, phrase,
  or local cell.

That keeps the plugin understandable in a DAW even though the engine is working
on several timescales internally.
