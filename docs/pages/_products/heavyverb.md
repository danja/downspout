---
title: HeavyVerb
order: 228
bundle: heavyverb.vst3
kind: Audio effect
category: processor
role: Master-bus reverb
screenshot: /assets/plugins/heavyverb.png
capabilities: [audio input, audio output]
summary: A high-quality reverb for the master bus, built from one to six wall-pair dimensions with their own distance, damping and filter, plus shimmer, swell, duck, gate and freeze and fifteen presets.
---

## Opinion

Not yet auditioned in a host. It is meant to sit on the master bus or a return at a low Mix: the dry signal is
never processed, so there is no latency and nothing to phase-align. The distance-based controls make it
easy to say what you want (a long thin tunnel, a square room, a huge stone nave) rather than hunting
for a decay time, and the special effects set it apart from Lightverb, which is deliberately minimal. The
presets were chosen from the numbers, not by ear, so expect to adjust them.

## Functionality

HeavyVerb is a feedback delay network. Each **dimension** is a pair of parallel walls: two delay lines whose
length is the wall spacing divided by the speed of sound (34.3 m is 100 ms). Up to six dimensions run at once,
so a room can have anything from two delay lines to twelve. A Householder matrix mixes them without
changing the energy, a single **Decay** sets how long every line rings so the tail is smooth, and each dimension has
its own **Damping** (how fast the highs die) and **Filter** (a low-pass or high-pass inside the loop that
changes the character of every repeat). Early reflections are placed from the distances, so a dimension with
far-apart walls gives late, separate echoes.

### Parameters

Seven global shaping controls (Decay, Pre-delay, Mix, Width, Modulation, Diffusion, Early Level), Low Cut, High Cut and Output on the
wet signal, and per dimension Distance (0.5-100 m), Damping and Filter. The full table is in the plugin README.

### Effects

- **Shimmer**: the tail is pitch-shifted (default +12 semitones, from -12 to +24) and fed back into the reverb.
- **Swell**: the reverb fades in after each onset, over 0.1 to 2.5 seconds.
- **Duck**: the wet signal gets out of the way of the input, by up to 24 dB.
- **Gate**: the wet signal is cut a set time (0.05 to 1 s) after the input falls quiet.
- **Freeze**: the tail is held indefinitely and the reverb stops taking input.

### Randomise
**Randomise** (button on the Dimensions panel) draws a new space as ordinary parameter values, like a
preset: dimensions 2-6, distances 1.5-60 m, damping, tilt filters, decay 0.4-14 s, pre-delay, modulation,
diffusion, early level, width and the wet cuts. Mix, Output, Effect, Effect Amount, Shimmer Shift and Freeze
are kept, so a press never gets louder, starts an effect or freezes the tail. The result is Custom, and the
draw is `randomiseParameters(base, seed)` in `heavyverb_presets.hpp` (the panel seeds it from
`std::random_device`, so it is a UI action, not a saved parameter).

### Presets

| # | Preset | Dims | Distances (m) | Decay | Effect | What it is |
|---|---|---|---|---|---|---|
| 1 | Master Glue | 2 | 3.2 / 4.6 | 0.6 s | - | short and quiet: binds a mix without an audible tail |
| 2 | Small Room | 3 | 3.5 / 4.2 / 2.6 | 0.5 s | - | a bedroom or booth: close walls, quick decay |
| 3 | Live Room | 3 | 6.5 / 8.8 / 4.2 | 0.9 s | - | a tracking room: lively, a little bright |
| 4 | Chamber | 3 | 8 / 10 / 5.5 | 1.4 s | - | stone echo chamber, bass kept out of the loop |
| 5 | Bright Plate | 4 | 1.6 / 2.1 / 2.9 / 3.7 | 2 s | - | dense and fast, no early reflections: the plate sound |
| 6 | Warm Hall | 4 | 14 / 19 / 11 / 26 | 2.2 s | Duck 30% | a dark hall that ducks under the vocal |
| 7 | Concert Hall | 3 | 22 / 33 / 15 | 2.8 s | - | a large hall with a clear pre-delay |
| 8 | Cathedral | 4 | 30 / 48 / 24 / 62 | 6.5 s | - | huge stone space: long, dark and slow to build |
| 9 | Tunnel | 3 | 85 / 4.2 / 4 | 4.5 s | - | one very long axis and two narrow ones: flutter and echoes |
| 10 | Canyon | 2 | 95 / 60 | 3.5 s | - | distant walls: sparse, widely spaced echoes that bloom |
| 11 | Shimmer Pad | 4 | 18 / 24 / 30 / 41 | 8 s | Shimmer +12 st, 55% | an octave up fed back through the hall |
| 12 | Abyss | 5 | 20 / 28 / 37 / 47 / 61 | 14 s | Shimmer -12 st, 60% | an octave down: a dark, rumbling swell |
| 13 | Bloom | 4 | 16 / 22 / 29 / 38 | 5 s | Swell 70% | each hit fades the reverb in: the tail swells after the note |
| 14 | Gated 80s | 3 | 5 / 6.5 / 4.2 | 3 s | Gate 45% | a big room cut off abruptly after the hit |
| 15 | Frozen Cloud | 6 | 19 / 27 / 36 / 47 / 59 / 73 | 45 s | Shimmer +7 st, 25% | a near-endless wash with a fifth on top: press Freeze to hold it |

### Status

Core covered by about 520 checks: every control and every preset, reverb time against Decay at three sample rates, pitch
accuracy, the effects, freeze, stability under random sweeps and overload at 44.1 to 192 kHz, block-size independence and
bit-exact dry at Mix 0. Not yet listened to or validated in a host.
