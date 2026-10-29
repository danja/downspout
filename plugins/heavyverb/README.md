# HeavyVerb

High-quality stereo reverb for the master bus. The room is built from up to six **dimensions**, each a pair
of parallel walls: **Distance** (in metres) sets how far apart they are, **Damping** how quickly the highs
die in that dimension, and **Filter** what each reflection loses (darker or thinner) as it goes round. On
top sit pre-delay, early reflections, modulation, wet low and high cuts, width, and special effects: shimmer,
swell, duck, gate and freeze. Fifteen presets cover ten typical spaces and five effects.

The dry signal is never processed: no latency, and Mix 0 returns the input bit for bit. See `docs/design.md`
for how it works and the assumptions made.

| Control | Range | Default | Notes |
|---|---|---|---|
| Dimensions | 1-6 | 3 | Active wall pairs; each is two delay lines (2 to 12 in all). Rows above the count are greyed and do nothing |
| Distance 1-6 | 0.5-100 m (logarithmic) | 7, 11, 17, 23, 31, 43 | Wall spacing; delay is distance / 343 m/s (1 m = 2.9 ms), so 34.3 m is 100 ms |
| Damping 1-6 | 0-100 % | 35 % | Air absorption: shortens the decay of the highs (100 % closes the loop at about 450 Hz) |
| Filter 1-6 | -100 to +100 % | 0 (flat) | Tone filter inside the loop, applied every pass: negative is low-pass (dark), positive is high-pass (thin, keeps bass out of the build-up) |
| Decay | 0.1-60 s (logarithmic) | 2.5 s | RT60, shared by every dimension so the tail stays even |
| Pre-delay | 0-250 ms | 12 ms | |
| Mix | 0-100 % | 25 % | Equal-power dry/wet |
| Width | 0-150 % | 100 % | 0 is mono wet |
| Modulation | 0-100 % | 35 % | Slow movement of the delay lengths |
| Diffusion | 0-100 % | 70 % | Input smearing; at 0 the four input all-passes are a plain 30.4 ms delay |
| Early Level | 0-100 % | 40 % | Early reflections placed from the dimensions' distances |
| Low Cut / High Cut | 20-500 Hz / 1-20 kHz | 60 Hz / 14 kHz | On the wet signal (2nd-order Butterworth) |
| Output | -24 to +12 dB | 0 dB | Wet level |
| Effect | Off, Shimmer, Swell, Duck, Gate | Off | |
| Effect Amount | 0-100 % | 50 % | Shimmer: level. Swell: fade-in time 0.1-2.5 s. Duck: depth up to 24 dB. Gate: hold time 0.05-1 s |
| Shimmer Shift | -12 to +24 st | +12 | Pitch shift in the shimmer loop |
| Freeze | Live / Freeze | Live | Holds the current tail indefinitely and stops the reverb taking input |
| Preset | Custom, 1-15 | Custom | A one-shot: the panel sends the preset's values as ordinary parameters. Moving a control afterwards returns to Custom |

**Randomise** (button on the Dimensions panel) draws a new space as ordinary parameter values, like a
preset: dimensions 2-6, distances 1.5-60 m, damping, tilt filters, decay 0.4-14 s, pre-delay, modulation,
diffusion, early level, width and the wet cuts. Mix, Output, Effect, Effect Amount, Shimmer Shift and Freeze
are kept, so a press never gets louder, starts an effect or freezes the tail. The result is Custom, and the
draw is `randomiseParameters(base, seed)` in `heavyverb_presets.hpp` (the panel seeds it from
`std::random_device`, so it is a UI action, not a saved parameter).

## Presets

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

Frozen Cloud does not start frozen (a frozen reverb takes no input, so it would be silent): play something
into it, then press Freeze.

## Effects

- **Shimmer.** The late tail is pitch-shifted (default an octave up) and fed back into the reverb, and a little of it goes straight to the output. Negative shifts give a dark swell (Abyss).
- **Swell.** After each onset in the input the reverb is faded in over the Amount time, so the tail blooms after the note.
- **Duck.** The wet signal is turned down while the input is loud and released over about 250 ms.
- **Gate.** The wet signal is cut abruptly Amount seconds after the input falls quiet (below about -40 dBFS).
- **Freeze.** The loop becomes lossless and the input is shut off: the tail holds its level indefinitely.

Swell, Duck and Gate follow the dry input level, so they are most obvious on material with gaps; on a dense mix
the gate stays open and the swell rarely retriggers.

Settings are ordinary automatable parameters, saved by the host; there is no plugin state and no MIDI.
Status: core covered by tests (see `docs/design.md`). Not yet listened to or validated in a host.
