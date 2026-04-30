# downspout meter notes

## Purpose

This document records the current state of meter handling in `downspout` and
the next architectural step needed to support music that is not basically 4/4.

The immediate motivator is not abstract odd-meter support. It is practical
musical use: if the target includes generative Irish folk material, then simple
transport sync is not enough.

## Current state

`downspout` already has several transport-aware plugins, but transport-aware
and meter-aware are not the same thing.

Today, most wrappers pass only:

- `bar`
- `barBeat`
- `beatsPerBar`
- `bpm`

That is enough for bar-relative timing, but not enough to express:

- `3/4` versus `6/8`
- compound grouping such as `3+3` or `3+3+3`
- odd-meter grouping such as `2+2+3`
- pickup bars / anacrusis

## Plugin impact

The current plugins fall into three rough groups.

### Mostly bar-length aware already

These plugins already derive timing from host `beatsPerBar`, so they are less
blocked by meter work:

- `p-mix`
- `e-mix`
- `rift`
- `cadence`

They still may need UI or semantic work for compound meters, but their timing
engines are not deeply tied to `4/4`.

### Partly adaptable, but still musically simple-meter oriented

- `bassgen`

`bassgen` works in beats and subdivisions rather than explicitly multiplying
everything by `16 steps per bar`, so it is in better shape than `ground` or
`drumgen`. The limitation is musical vocabulary: its rhythm generation still
thinks in fairly plain strong-beat/simple-subdivision terms.

### Structurally 4/4 in the core

- `ground`
- `drumgen`

These cores currently encode `4/4` directly enough that true meter support is a
real refactor, not a wrapper tweak.

Examples:

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

The next useful shared abstraction should be a common meter model.

It should live outside DPF and be portable across plugin cores.

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

The DPF wrappers currently pass enough information for bar-relative timing but
not enough for full meter semantics.

The next transport snapshot revision should try to preserve, when the host
provides it:

- bar numerator
- denominator / beat unit
- ticks-per-beat
- any information needed to reconstruct pulse grouping

If the host cannot provide grouping directly, the core should still be able to
derive useful defaults from numerator/denominator and style.

## Recommended implementation order

1. Define a shared portable meter type under common code.
2. Extend transport snapshots so wrappers can pass richer meter data.
3. Refactor `ground` to use the shared meter model.
4. Refactor `drumgen` to use the shared meter model.
5. Expand `bassgen` rhythmic vocabulary for compound-meter styles.
6. Add folk-specific style logic in `cadence`, `ground`, and `drumgen`.
7. Add a dedicated melody generator if the goal remains full generative
   Irish-folk arrangement.

## Documentation consequence

Until this work is done, the root docs should describe the current plugins as
transport-aware, while being explicit that full compound/odd meter support is
still uneven across the musical generators.
