# HeavyVerb design notes

An original reverb (no source plugin). The brief: a master-bus reverb of high quality with up to six
dimensions (default 3), each with a distance in metres, a damping control and a feedback-filter control, a
set of special effects such as shimmer, and fifteen presets (ten typical spaces, five effects).

The portable core is `include/heavyverb_core.hpp` / `src/heavyverb_core.cpp`; the DPF wrapper and the
MagnetoKit panel are thin. Presets are data in `include/heavyverb_presets.hpp`, applied by the panel only.

## Signal path

```
in -> pre-delay -> [swell x (1 - freeze)] --+--> early reflections ------------------------+
                                            |                                              |
                                            +--> 4 all-pass diffusers -> 2N-line FDN -> late
                                                      ^                       |
                                                      +-- shimmer shifter <---+
late + early (+ shimmer) -> low cut -> high cut -> width -> [duck, gate] x output -> sin(mix)
dry --------------------------------------------------------------------------> cos(mix)
```

## What a "dimension" means (assumption)

A dimension is one axis of a room: two parallel walls a **Distance** apart. Sound crossing it takes
distance / 343 m/s, so each dimension contributes **two delay lines** (one per direction) of that length,
and N dimensions give a 2N-line feedback delay network (2 to 12 lines).

- The two lines of a dimension are scaled by fixed ratios (1.000 and 1.137 for dimension 1, and so on, in
  `kRatio`) so that no two lines are the same length, even with equal distances. Distance is therefore
  the delay of the first line; the second is up to 28 % longer or 8 % shorter.
- The lines are coupled by a **Householder reflection** over the active lines, which preserves energy: the
  tail does not change level when Dimensions changes (tested: within 3 dB from one to six, with an input
  and output gain of (2/n)^1/4 each).
- **Decay** is one RT60 for the whole network: each line's loop gain is `10^(-3 L / (sr RT60))`, so every
  line decays at the same rate and the tail is smooth rather than separate echoes. Distance changes the
  pattern of the echoes, not the decay time. Measured broadband RT60 is 0.8 to 1.1 x the control (tested at
  44.1, 48 and 96 kHz); the loop filters make real tails a little shorter at the highs.
- Inactive dimensions are removed from the matrix as well as muted (their weight ramps to zero over 50 ms),
  and their controls are tested to have no effect.

## Damping and Filter (assumptions)

Both are one-pole filters in each line's loop, so their effect compounds with every pass:

- **Damping** is a low-pass whose cutoff runs from 20 kHz (0 %) down to about 450 Hz (100 %), i.e.
  air absorption. It shortens the decay of the highs; the mid-frequency RT60 is untouched.
- **Filter** is bipolar. Negative values add a second low-pass (20 kHz down to about 800 Hz at -100 %),
  which darkens; positive values add a high-pass (20 Hz up to about 1.6 kHz at +100 %), which keeps bass out
  of the build-up. A high-pass in the loop also shortens the decay of the lows. Zero is exactly flat.

## Early reflections

Each dimension gives two taps from a mono copy of the pre-delayed input. The source is assumed to sit 0.4 of
the way across, so the reflections arrive after 0.8 and 1.2 spacings of travel (0.8 d / c and 1.2 d / c),
with opposite signs, opposite pan, level falling with path length (1 / (1 + path / 8 m)) and the dimension's
Damping applied. The six dimensions are panned to different places (`kPan`). A test checks the first tap
against the distance (80 ms at 34.3 m).

## Input diffusion

Four Schroeder all-passes per channel (4.8, 3.6, 12.7, 9.3 ms left; 5.3, 3.1, 11.9, 8.1 ms right), gain
0.7 x Diffusion. At Diffusion 0 an all-pass is a plain delay, so the late reverb starts a further 30.4 ms
late (the early reflections are unaffected). At the default 70 % the direct term arrives at once. Tested.

## Quality measures

- 4-point cubic Hermite interpolation on every delay read, with slow per-line modulation (a sine of
  0.06-0.25 Hz, up to 2.5 ms or 4 % of the line, whichever is smaller) so the tail has no fixed ringing.
- Every control is smoothed: delay lengths and distances over 80 ms (a distance change glides, with a
  small pitch bend, rather than clicking), the rest over 30-50 ms, with linear interpolation across each
  32-sample control block. The control blocks are aligned to the sample count, so output does not depend
  on the host's block size (tested at 1, 37 and 480 frames, bit-close).
- Filter states in double precision, wet filters (Butterworth) in double, denormals flushed in every line,
  filter and tap: the tail ends in exact zeros (tested over 40 s).
- The dry path is `dry x cos(mix)` and the wet `sin(mix)`: Mix 0 is the input bit for bit (tested with
  every preset), and the blend is equal-power.
- No allocation in `processBlock`; buffers are allocated in `prepareEngine` (constructor and sample-rate
  change). `activate()` clears memory only.
- Delay buffers scale with the sample rate (tested 44.1 to 192 kHz).

## Effects

- **Shimmer** uses a two-head rotating delay pitch shifter (50 ms window, Hann crossfade) on the late tail.
  The shifted signal goes through `tanh` and back into the reverb input at 0.4 x level, and 0.5 x level goes
  straight to the output. A stationary tone comes out at full level and the source tone is at least 30 dB down
  (tested at +7, +12, +19 and -12 semitones; measured 56 dB or more); on transient material a crossfading shifter has the usual
  modulation artefacts, which the reverb diffusion hides. The loop always decays (tested at 100 % with a
  4 s decay: 25 dB down in ten seconds), because the shifted signal passes through the same lossy loop.
- **Swell** detects onsets (a 3 ms envelope more than 2.2 x a 100 ms one, 120 ms refractory), drops the reverb
  input to zero in 3 ms, then raises it linearly over the Amount time.
- **Duck** follows the input envelope (5 ms attack, 250 ms release) and turns the wet down by up to 24 x Amount
  dB as the envelope goes from -40 to -10 dBFS.
- **Gate** opens when the input envelope passes -40 dBFS, holds for Amount (0.05 to 1 s) after it falls below,
  then closes in about 20 ms (2 ms to open). Thresholds are absolute, so they assume a normal mix level.
- **Freeze** raises every loop gain to 1 and removes the loop filters (over 50 ms) and closes the input. A
  lossless Householder network neither grows nor decays (tested: within 1 dB over 8 s; a frozen reverb takes no new input).

Amount means different things per effect: it is the size of the effect for Shimmer and Duck, and the *time*
for Swell and Gate (these are always fully on when selected).

## Parameters and state

34 ordinary automatable parameters in a stable order (`heavyverb_params.hpp`): Dimensions, Distance 1-6,
Damping 1-6, Filter 1-6, then the global ones, Effect, Effect Amount, Shift, Freeze and Preset. `Preset` is
stored by the host but ignored by the engine (tested): the panel applies a preset by sending its values as
parameters, so a reloaded project keeps what was saved. No plugin state, no MIDI, no transport, no
latency. The engine is created in the plugin constructor and re-prepared in `sampleRateChanged()`.

## Randomise

**Randomise** (button on the Dimensions panel) draws a new space as ordinary parameter values, like a
preset: dimensions 2-6, distances 1.5-60 m, damping, tilt filters, decay 0.4-14 s, pre-delay, modulation,
diffusion, early level, width and the wet cuts. Mix, Output, Effect, Effect Amount, Shimmer Shift and Freeze
are kept, so a press never gets louder, starts an effect or freezes the tail. The result is Custom, and the
draw is `randomiseParameters(base, seed)` in `heavyverb_presets.hpp` (the panel seeds it from
`std::random_device`, so it is a UI action, not a saved parameter).

## Cost

About 2 to 5 % of one core at 48 kHz for 3 to 6 dimensions with shimmer (measured on a busy machine, so an
upper bound).

## Tests (`tests/heavyverb_core_tests.cpp`, about 525 checks)

Parameter table against the core; the 15 presets (ranges, names, no duplicates, ten typical and five special,
coverage of the effects, each renders finite, audible and decaying); Mix 0 is bit-exact dry; Distance sets
the delay (line and early reflection); pre-delay holds back the reverb; wet level across dimension counts;
RT60 against Decay at three sample rates; Damping removes highs; Filter tilts the tail both ways; inactive
dimensions are inert and each added dimension changes the sound; every control (all 34) changes the output
except Preset, which does not; pitch-shifter accuracy; shimmer adds the octave above and below and always
decays; swell, duck, gate and freeze behave as described; Width and the wet cuts; stability under random
sweeps, freezes and overload at 44.1 to 192 kHz; block-size independence; in-place and missing-right-input
processing; no denormals after 40 s of silence; reset clears the tail only; the first block is not faded in.

## Not done

Listening and host validation (REAPER). The pitch shifter is a time-domain design, not a phase vocoder: a
spectral shifter would be cleaner on sustained harmonic material at a much higher cost and latency. There
is no oversampling (the reverb is linear apart from the shimmer limiter), and no true-peak or level
metering. Presets were chosen by their numbers, not by ear.
