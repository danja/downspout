# downspout meter notes

## Purpose

This document records the current state of meter handling in `downspout` and
the next architectural step needed to support music that is not basically 4/4.

The immediate motivator is not abstract odd-meter support. It is practical
musical use: if the target includes generative Irish folk material, then simple
transport sync is not enough.

## Current state

`downspout` now has an initial shared meter layer in
[include/downspout/meter.hpp](/home/danny/github/downspout/include/downspout/meter.hpp).
That layer gives the repository one common representation for:

- numerator
- denominator
- pulse grouping

This closes the first architectural gap. It does not finish the musical work.

Most transport-aware wrappers now preserve:

- `bar`
- `barBeat`
- `beatsPerBar`
- `beatType`
- `bpm`
- a normalized portable `Meter`

That means the repository can now express:

- `3/4` versus `6/8`
- compound grouping such as `3+3` or `3+3+3`
- odd-meter grouping such as `2+2+3`

What it still does not express well yet is musical intent on top of that data:

- pickup bars / anacrusis
- tune-form cadence shapes
- ornament timing
- idiomatic folk accompaniment behavior

## Plugin impact

The current plugins fall into three rough groups.

### Mostly bar-length aware already

These plugins already derived timing from host bar information before the shared
meter refactor, and now also receive richer meter data:

- `p-mix`
- `e-mix`
- `rift`
- `cadence`

They still may need UI or semantic work for compound meters, but their timing
engines are not deeply tied to `4/4`.

### Structurally meter-aware, but still style-limited

- `bassgen`

`bassgen` now receives richer meter data from the wrapper, persists it in
pattern state, regenerates when meter changes, and shapes accents around pulse
starts for compound meters. That closes the structural gap for `bassgen`, but
its style layer is still generic rather than explicitly jig/reel/hornpipe
aware.

### Structural 4/4 blockers that have now been removed

- `ground`
- `drumgen`

These were the main structural blockers. That first refactor is now done:

- `ground` now derives form and phrase grids from the shared meter model rather
  than a fixed `16 steps per bar`
- `drumgen` now derives `stepsPerBar` from `meter.numerator * stepsPerBeat`
  rather than `stepsPerBeat * 4`, and now has dedicated compound/triple-meter
  pulse accents for bar generation and fill targeting

That does not mean they are now idiomatic jig/hornpipe generators. It means
they no longer silently force every bar back into `4/4` shape.

Examples of the old assumptions that were removed:

- `ground` phrase planning currently assumes `4 steps per beat` and
  `16 steps per bar`
- `drumgen` pattern setup currently derives `stepsPerBar` as
  `stepsPerBeat * 4`

## What Irish folk actually needs

For generative Irish folk material, the missing capabilities are broader than
time signatures alone.

### Meter and grouping

- reliable `3/4`, `6/8`, `9/8`, `12/8`
- grouping-aware pulse models, especially `3+3` and `3+3+3`
- support for tune pickups and phrase endings that do not line up like pop-bar
  loops

### Idiomatic rhythm families

- reels
- jigs
- slip jigs
- hornpipes
- polkas

These should not just be preset names. They need different accent placement,
subdivision feel, and phrase behavior.

### Harmonic and melodic idiom

- modal centers such as Dorian and Mixolydian
- drone-friendly accompaniment
- less rigid functional harmony
- cadential behavior that suits folk tune form rather than only loop turnover

### Arrangement roles still missing

- a real lead-tune generator
- accompaniment patterns for guitar, bouzouki, piano, or drone instruments
- percussion logic closer to bodhran / hand-percussion behavior
- ornament logic: cuts, rolls, trebles, grace clusters

Without those, the project can make useful groove tools, but it does not yet
make a convincing generative Irish-folk setup.

## Recommended shared abstraction

The shared meter model now exists and should remain portable across plugin
cores.

Suggested shape:

- `numerator`
- `denominator`
- `beatUnit`
- `grouping[]`
- `pulseCount`
- `stepsPerBeat`
- `stepsPerPulse`
- `stepsPerBar`

The important field is `grouping[]`. `6/8` should not be treated as just
`beatsPerBar = 6`. The core needs to know whether the musical pulse is grouped
as `3+3`.

## Wrapper implications

The DPF wrappers now preserve richer time-signature data when the host provides
it, but grouping and style semantics still have to be interpreted in the core.

The transport snapshots now aim to preserve:

- bar numerator
- denominator / beat unit
- ticks-per-beat
- any information needed to reconstruct pulse grouping

If the host cannot provide grouping directly, the core should still be able to
derive useful defaults from numerator/denominator and style.

## Recommended implementation order

1. Add folk-specific style logic in `cadence`, `ground`, `drumgen`, and the
   higher-level `bassgen` phrasing choices.
2. Add explicit pickup/anacrusis handling where musically needed.
3. Add a dedicated melody generator if the goal remains full generative
   Irish-folk arrangement.

## Documentation consequence

The root docs should now describe the repository as having an initial shared
meter model, while being explicit that compound-meter style behavior is still
uneven across the musical generators.
