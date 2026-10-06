# Plank design notes

Plank re-implements the synthesis engine of the open-source
[Plinky](https://plinkysynth.com) touch synth as a portable C++ core, then adds a
Launchpad grid playing surface and a NanoVG UI. It is an eight-voice polyphonic
synthesizer, one monophonic voice per grid column ("string" is Plinky's word),
with a second family of Plonk-style modal resonator engines alongside the
Plinky wavetable voice. This file records what was
taken from Plinky, what was deliberately left behind, and where Plank departs
from the original.

## Why a rewrite rather than a port

`plinky_public/sw/Core/Src/plinky.c` is 3400 lines built around the STM32L4
runtime: it is a single translation unit including HAL headers, DMA handles,
touch-controller calibration, an OLED UI, WebUSB and a bootloader check. Its
`tables.h` alone is 2189 lines of precomputed lookup data.

Porting that file would have dragged the hardware layer into the plugin and
pushed the code away from every other plugin in this repository. So the DSP is
reimplemented in the portable-core / thin-DPF-wrapper shape that `p-mix`, `e-mix`
and `rift` established, and the shared code contract is:

```
include/plank_params.hpp   parameters, scales, Launchpad map  (no DSP)
include/plank_voice.hpp    oscillators, filter, envelopes, LFOs
include/plank_core.hpp     Processor: grid, voice allocation, effects
src/plank_core.cpp         the core
src/plank_voice.cpp        voice primitives
src/plank_wavetables.cpp   band-limited table synthesis
                           (modal resonators and the exciter live in plank_voice)
src/dpf/                   DPF wrapper and NanoVG UI only
tests/                     deterministic core tests
```

Plinky's software is MIT licensed (`LICENSE.md`), which permits this. Attribution
is recorded in `README.md` and here.

## What came across

| Plinky | Plank |
|---|---|
| `RunVoice` two-oscillator pair | `Processor::renderVoice` |
| polyBLEP saw edges | `polyBlep` in `plank_core.cpp` |
| y1/y2 resonant filter with 0.999 leak | `ResonantFilter` |
| drive compensated by `2 / (resonance + 2)` | `driveGain` in `renderVoice` |
| two ADS envelopes | two `Envelope` instances |
| Cytomic dynamic knob smoothing | `Smoother` |
| 11 LFO shapes | `Processor::lfoSample` |
| scale quantised pitch lookup | `kScaleIntervals` plus `noteForCell` |
| tape delay, shimmer reverb, compressor, M/S width | `renderDelay`, `renderReverb`, `applyOutputStage` |
| eight fingers as eight voices | eight grid columns as eight strings |
| strings tuned apart, like a guitar | `Spread` separates the columns |

## What was left behind, and why

- **Granular sampler.** Needs sample loading and storage; out of scope for a
  grid instrument and would have dominated the parameter surface.
- **Arpeggiator and step sequencer.** Deliberate: the brief was the voice engine
  and the grid. Plinky's `arp.h` is a good candidate for a later addition.
- **Touch input, accelerometers, ADC/CV.** These are how Plinky gets its gestures;
  the Launchpad grid replaces them.
- **Oscillator, OLED and LED UI, WebUSB, flash presets, recording.** Host and UI
  concerns that this repository already handles.

## Deliberate departures

**Filter coefficient is separate from VCA gain.** In Plinky the envelope drives
both at once: `y1 += (... - y1) * vol`, so the filter opens as the note decays.
Plank keeps Plinky's recurrence but feeds it a dedicated cutoff coefficient and
applies the envelope as output gain. Driving both from the amplitude made the
cutoff control nearly inaudible, which is a bug rather than a feature.

**Wavetables are synthesised, not copied.** Plinky ships seventeen band-limited
Miunau tables as generated `int16` data in `wavetable.h`, with no generator in the
repository. Rather than vendor 17527 numbers, `plank_wavetables.cpp` builds the
same seventeen-table sweep by additive synthesis on first use. The classic shapes
(saw, three pulse widths, square, triangle, and sparse two/three/five-partial
forms) are generated from their exact Fourier series; the remaining nine step a
harmonic series down toward a near-sine. Phase scatter uses a fixed LCG so the
tables are bit-identical on every run and in every host.

**`Morph` is one continuous axis.** In Plinky `P_PWM` selects a table and offsets
the oscillator pair. Here a single control both crossfades away from the
polyBLEP oscillators and walks the table pair, so the sweep from buzzy saw to
round harmonic tone is one gesture rather than two coupled ones.

**String stride is ported, not approximated.** Plinky's `stride()` pushes string
*n* up by *n* × `Stride` semitones and snaps each push to the nearest scale
degree, penalising degrees already used so the strings fan out. `strideSteps()`
in `plank_params.hpp` is that algorithm, and `Spread = Stride` is the default,
which with Plinky's default stride of 7 gives strings roughly a fifth apart that
never leave the key. Earlier versions added the stride as a flat semitone offset,
which was out of key and identical for every column. A separate bug made
`setDegree()` ask for column 0 whatever string was played, so Spread had no
audible effect at all; the grid now sounds exactly what each pad's label says.
Chromatic is also a twelve-degree scale now, so stride and rotate do not wrap an
octave early.

**Scale tables are the textbook ones.** The two Neapolitan scales had wrong notes
(the minor was simply Phrygian), and the five eight-note scales (half-whole and
whole-half diminished, three bebop) were stored as seven notes, so their last
degree was dropped and the top row of a column played an out-of-scale pitch. All are
corrected, and the same Neapolitan fix was applied to every plugin that carries the
scale; `docs/scales.md` holds the pitch sets. `testEveryPadStaysInTheChosenScale`
checks every pad against independent pitch-class sets for all 24 scales, in Unison,
Scale and Stride spreads, across several strides and rotations.

**Resonator engines come from Plonk, not Plinky.** `Engine` adds six modal models
(beam, marimba, drumhead, membrane, plate, string) after the Intellijel/AAS Plonk
Eurorack module: an exciter (`Mallet` half-sine pulse or `Noise` burst) drives
eight two-pole resonators whose T60 follows `Damping` and `Material`, with
`Strike` weighting modes by `|sin(k·pi·position)|`. Plonk's DSP is not public;
the mode ratios are textbook values (Euler-Bernoulli bar, Bessel zeros, square
membrane, stiff string) and the plate ratios are illustrative. Glide, Interval and
Detune do not apply to the resonator engines.

**Grid rows are a scale ladder, not a keyboard.** Plinky maps a finger's position
along a string to pitch, continuously. A Launchpad has discrete pads, so each
column gets one octave of the current scale across its eight rows, with `Rotate`
shifting which degree sits under which row. `Spread` then separates the columns
the way Plinky's strings are separated, which is what makes the eight voices sound
like eight strings rather than one string played eight times. The full mapping is
in "Pitch mapping" below.

**Microtune is cents.** Plinky's `P_MICROTUNE` is a fine pitch offset in cents.
Plank keeps that meaning and applies it as a fractional-semitone detune, so the
8-cent default is a subtle chorus rather than a transposition.

## Pitch mapping

`Processor::noteForCell(row, col)` is the single definition of what a pad plays.
The UI's `noteForRow()` mirrors it for the pad labels, and both call the shared
`scaleStepAt()` and `strideSteps()` from `plank_params.hpp`, so the display cannot
drift from the sound.

```
degree   = row + rotate + columnDegrees(spread, col)
semitone = scaleStepAt(scale, degree) + columnSemitones(spread, col)
note     = root + 12 * octave + semitone + microtune / 100        (rounded, 0..127)
```

| Spread | `columnDegrees` | `columnSemitones` |
|---|---|---|
| Unison | 0 | 0 |
| Scale | `col` | 0 |
| Fourths | 0 | `5 * col` |
| Fifths | 0 | `7 * col` |
| Stride | `strideSteps(scale, stride, col)` | 0 |

The result is folded into MIDI range by whole octaves, never clamped. A clamp pins
everything past the top to 127, which is usually not in the scale, so a large
`Stride` put wrong notes on the right-hand columns; folding keeps the pitch class.

`scaleStepAt()` wraps per octave, so a 5-note pentatonic and a 7-note diatonic
both climb across all eight rows. Chromatic is special-cased as 12 degrees
(`degree == semitone`); its table is only eight entries wide, and treating it as
an 8-note scale made degrees wrap an octave early.

### `strideSteps`

Port of `stride()` in `plinky.c`. For string `col` it walks strings `0..col-1`:

1. advance a running pitch `pos` by `stride` semitones;
2. find the scale degree nearest to `pos mod 12`, with the candidate shifted by
   an octave when it is more than six semitones away, scoring each as
   `|distance| * 16 + timesUsed` so degrees already chosen are penalised;
3. snap `pos` to that degree, and record `degree + (pos / 12) * degreesPerOctave`.

The result is in scale steps, so it adds to `row` and `rotate`. Stride 0 or
column 0 returns 0. Plinky memoises this in static tables; Plank recomputes it,
which is at most seven iterations over at most twelve degrees and only runs on a
note start or a tuning change.

### Where the column used to get lost

`startNote()` and the retune loop in `setParameter()` pitch a voice through
`setDegree(voice, row, col)`. It previously took only the row and called
`noteForCell(row, 0)`, so any Spread other than Unison was inaudible: the pads were
different on paper and every string played column 0's ladder. The column is now an
explicit argument, and `testStridePitchReachesTheVoice` plucks each column and
checks the emitted MIDI note against `noteForCell`.

## Resonator engines

`EngineId` selects the sound source. `plinky` (0) is the original oscillator pair,
the rest are modal models in the spirit of the Intellijel/AAS Plonk module.
Ordinals are saved in host state and are append-only, and the default must stay
`plinky` so existing patches are unchanged.

```
pluck -> Exciter::trigger        (one per string, on every startNote)
frame -> Exciter::next -> ModalState::process (8 resonators) -> * 0.5
      -> drive + noise -> ResonantFilter -> amplitude -> pan
```

The resonator stage replaces only the oscillator and wavetable block in
`renderVoice`; the filter, envelopes, LFO routing, delay and reverb are shared
with the Plinky engine. The oscillator phases are still advanced so switching
engine mid-note does not glitch the other path.

**Modes.** Each of the eight modes is the recurrence
`y = x + b1*y1 + b2*y2` with `b1 = 2 r cos(w)` and `b2 = -r^2`, where
`w = 2 pi f / sr`. `r = exp(-ln(1000) / (T60 * sr))`, so a mode's `T60` is exactly
its ring time. Coefficients are recomputed once per block by
`computeModalCoefficients()`, which is cheap enough not to need smoothing at these
block sizes. A mode above 0.45 x sample rate is muted rather than aliased.

**Ring time.** The fundamental's `T60` is `0.05 * 80^damping` seconds (50 ms to
about 4 s). Mode *k* is scaled by `ratio_k^-tilt` with `tilt = 2.2 - 1.9 * material`:
at 0 the upper modes die quickly (wood, skin), at 1 they ring nearly as long as
the fundamental (glass, metal).

**Strike position.** Mode gain is `sin(w) * max(0.04, |sin(k * pi * strike)|)`.
The `|sin|` term is the standing-wave shape of an idealised string or bar, so
striking at 0.5 nulls the even modes. It is applied to every engine as a useful
approximation, not as the true mode shape of a membrane. The `sin(w)` factor
normalises the resonator's unit-impulse response to unit amplitude, so loudness
does not depend on pitch; the 0.04 floor keeps a mode from vanishing entirely.

**Mode ratios** (`plank_voice.cpp`):

| Engine | Ratios | Source |
|---|---|---|
| Beam | 1, 2.756, 5.404, 8.933, 13.344, 18.638, 24.812, 31.87 | free-free bar |
| Marimba | 1, 3.99, 9.92, 17.9, 27.5, 38.5, 51.0, 64.0 | bar tuned 1 : 4 : 10 |
| Drumhead | 1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501 | Bessel zeros |
| Membrane | 1, 1.581, 2.0, 2.236, 2.55, 2.915, 3.0, 3.162 | square membrane, sqrt(m^2 + n^2) normalised |
| Plate | 1, 1.60, 2.32, 2.93, 3.74, 4.29, 5.05, 5.90 | illustrative, not derived |
| String | `k * sqrt(1 + B k^2)`, B = 0.0004 | stiff string |

The Marimba and Plate rows beyond the first few partials are rounded estimates.
Plonk's own ratios are not published, so none of these claim to be a copy.

**Exciter.** One `Exciter` per voice, triggered by `startNote()` using the current
`Morph` as hardness.

- `Mallet`: a half-sine pulse of unit area, peak `pi / (2 * width)`. The width is a
  fraction of the note's period, from half a period (soft) down to 8% (hard),
  clamped to 0.15 to 6 ms. It has to scale with pitch: a half-sine of width *W* has
  its first spectral null at 1 / *W*, so the original fixed 6 ms mallet put the
  null (about 170 Hz) below nearly every mode and the default soft strike was
  close to silent above the lowest rows. Unit area keeps the loudness independent
  of hardness.
- `Noise`: a 12 ms white-noise burst with a linear decay, through a one-pole
  lowpass whose coefficient is `0.04 + 0.9 * hardness`.

A noise generator per voice (`xorshift32`) keeps the output deterministic.

**What does not apply.** Glide, Interval and Detune act on the oscillator pair and
are ignored by the resonator engines. LFO pitch modulation does reach them (it
feeds the fundamental), but modal state is not retuned smoothly between blocks, so
fast pitch LFOs will sound steppy.

## Launchpad contract

Taken from the `lifeform` plugin, which is the reference for this repository:

- programmer mode on entry, SysEx `F0 00 20 29 02 0D 0E 01 F7`;
- note-on channel 1 for static grid LEDs, channel 1 CC for side and top buttons;
- pad and control input is consumed, never echoed, because a release echoed back
  reads as an LED-off command;
- musical MIDI defaults to channel 4 to stay clear of the LED channels;
- a 0.75 second periodic LED refresh with change detection, so a Launchpad that
  missed an update recovers without flooding the port;
- panic sends programmer-mode SysEx, a bulk LED clear, and ordinary LED-off
  messages, the last because some hosts filter SysEx.

## Parameters

127 total: 53 host-writable controls, 64 grid cells, 10 read-only status.

The five resonator parameters (`engine`, `exciter`, `strike`, `damping`,
`material`) sit after `level` and before the Launchpad group. Inserting them shifted
every later index, which matters to host automation but not to saved state, because
state is keyed by symbol. `stride` changed meaning and range (0 to 12, default 7;
it was -24 to 24 and a flat offset) and `spread` gained a fifth value, `stride`,
which is now the default. An old project's out-of-range stride is clamped on load.

The grid cell parameters exist so the UI and host automation can pluck a string
without a MIDI port. They are edge triggered: writing the same value twice does
not re-articulate, so a host restoring state cannot retrigger every note. MIDI
generated by an out-of-band parameter change is queued and flushed at the start of
the next block rather than discarded, because `setParameter` can be called from a
thread other than the audio one.

## User interface

`PlankUI.cpp` is a custom NanoVG panel, 752 x 700. The height was raised from 612
when the resonator controls were added; `layoutPanel()` drops a lane rather than
let it overflow the button strip, so a new control that does not fit disappears
silently, and the screenshot is the check.

- **Pads** are drawn from `gridRect()` with row 0 at the bottom. Each carries its
  note name, computed by `noteForRow()`, the display twin of `noteForCell()`. The
  old column-letter ruler was dropped: it spelled out one string's ladder under
  columns that, once spread, each played a different one.
- **Selectors** all live in one block at the top and ignore their `lane` field.
  Clicking one opens a drop-down (`drawDropdown`), drawn last so it floats over the
  panel and the pad grid. Geometry comes from `dropdownLayout()`, which the draw
  and hit-test (`dropdownItemAt`) share so they cannot disagree. Lists of more than
  12 items flow into columns of 12 (22 for the 88-note root list), and the box is
  clamped inside the window and flips above the selector when it would run off the
  bottom. While a list is open it consumes the next click, so dismissing it never
  plucks a pad underneath. Items are `spec.minimum + i`; labels come from
  `formatValue()`, with note names for `root`.
- **Sliders** are laid out by lane, five per row. `kSliderCount` must equal the
  number of entries in `kSliders`. It was once one too high, and the zero-initialised
  extra entry drew a blank slider in the Oscillators lane.
- Selector text comes from `formatValue()`, which maps enum parameters to the same
  name tables the DPF wrapper exposes as host enumerations.

## Known limitations

- Grid events are quantised to the processing block, matching the reference
  plugins here. At a 64 to 512 frame block that is at most a few milliseconds,
  under the 8 ms default attack.
- The wavetable tables are synthesised with a fixed harmonic count rather than
  mip-mapped per pitch, so very high notes can alias slightly. Plinky has the
  same behaviour at the top of its range.
- The resonator engines retune once per block. Fast pitch modulation steps
  audibly, and glide is ignored.
- Mode ratios are approximations of the named objects, not Plonk's.
- All eight modes use the same string-style strike weighting, which is only
  correct for the bar and string models.
- The reverb is a four-comb tank with a damped, shimmer-modulated feedback path,
  not Plinky's full implementation. It is a reasonable tail, not an exact match.

## Testing

`tests/plank_core_tests.cpp` uses a hand-rolled `require()` rather than
`assert()`, matching `lifeform`. The Release build adds `-DNDEBUG`, which compiles
every `assert()` out and lets a suite pass while testing nothing; `require()`
cannot be compiled away, so no `-UNDEBUG` block is needed in `CMakeLists.txt`.

The suite covers the grid note mapping, the scale ladder including rotate and
stride, one-string-per-column allocation, latch behaviour, the filter actually
moving the filter, morph crossfading between two different waveforms, wavetable
band-limiting at the loop point, the Launchpad LED and MIDI channel contract,
panic, and determinism.

Added with the layout fix and the resonator engines:

- `testStrideSpreadFollowsPlinky`: stride 7 on A major gives A, E, B; stride 0 is
  unison; stride 6 never leaves the scale.
- `testStridePitchReachesTheVoice`: plucks every column and compares the emitted
  note with `noteForCell`. This is the regression for the dropped column.
- chromatic stays a twelve-degree ladder past row 7, and the scale-table test no
  longer assumes chromatic has eight entries.
- `testEngineIdsAreStable`: pins engine ordinals and that the default is Plinky.
- `testEveryResonatorEngineSounds`: each engine is audible, finite, under 1.5 peak,
  and differs from the Plinky engine.
- `testResonatorEnginesDifferFromEachOther`, `testStrikePositionChangesTheTone`,
  `testExcitersAreBothUsable`.
- `testDampingSetsTheRingTime`: with sustain at 1 so the envelope does not hide the
  resonator, high damping rings at least four times as much late energy as low.
- `testResonatorStateRoundTrips`.

These assert on audio and MIDI rather than on tables, because the column bug was
invisible to table-level assertions.
