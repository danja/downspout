# Strata: design notes

Source read: `~/github/oxide-studio/crates/oxide-dsp/src/wavetable/` (`synth.rs`, `tables.rs`, `mod_matrix.rs`; MIT/Apache-2.0).
The ideas (two table oscillators with a morphing position, warp, unison, sub, filter, envelopes, LFO) are kept; the
engine was redesigned.

## What the source does (from reading the code; not run)

- **No band-limiting.** One 2048-sample frame per shape, including a naive saw and square (hard edges), read with
  linear interpolation at every pitch. At C7 (2093 Hz) the read pointer moves about 89 samples per output sample, so
  harmonics far above Nyquist fold back.
- **Unison cancels the fundamental at Amount 0.** Unison voices start at evenly spaced phases `u / n`. Copies of one
  waveform shifted by 1/n of a cycle sum to a waveform with a period of 1/n, so at Amount 0 four voices lose the
  fundamental and sound two octaves up. The weight `1/sqrt(n)` also makes the level depend on the voice count.
- "Fold" is applied after the table read as `sin(g x)` with no antialiasing; the warp modes listed in its README (Sync,
  Bend, PWM, Asym, FM) are four in the code (None, Pwm, Fold, Sync).
- Per-sample trig for the pan law and `powf` for every frequency of every unison voice; only the first 1024 frames of a
  buffer are processed.

## What Strata does

- **Tables:** each frame is defined by a harmonic spectrum (Basic, Harmonics and Formant analytically; Digital and Vintage
  analysed by FFT from a smooth waveform), then **21 mips** are synthesised by inverse FFT with the harmonics above
  `1024 * 2^(-k/2)` removed (half-octave steps down to 1 harmonic). Every frame is normalised to a 0.8 peak so the level does
  not move with Position. The bank is 21 x 8 x 5 tables of 2052 floats (about 7 MB), built in 122 ms on first use (the plugin
  constructors do it, off the audio thread).
- **Reading:** the mip is the finest whose harmonic limit does not exceed `(sr / 2) / (f x slope)`, where slope is the
  warp's local speed-up (the Sync multiplier, the Bend slope), so nothing that could alias is read. Cubic Hermite along the
  phase, linear between frames.
- **Unison:** voices start in step at Amount 0 and spread as Amount rises; the weight goes from `1/n` (coherent) to
  `1/sqrt(n)` (decorrelated), so the level stays within 1-2 dB of one voice. Classic spreads the detune (+-50 cents at
  full Amount); Shimmer lifts odd voices up to an octave; Noise adds a slow random drift per voice. Detune and pan are
  recomputed every 32 samples, as are pitch and the filter coefficients.
- **Fold** uses first-order antiderivative antialiasing, `(F(s) - F(prev)) / (s - prev)` with `F = -cos(a s) / a`.
- **Filter:** a trapezoidal (zero-delay-feedback) SVF, one per channel per voice; LP 24 is two stages (the second at
  Butterworth Q). `filterResponseDb()` gives the plotted/modelled response from the same coefficients.
- **Envelopes:** attack is linear in time; decay and release are one-pole exponentials with the stated time constant; a
  voice ends when the release falls below 1e-4.
- **Output:** a one-pole DC blocker at about 4 Hz (a warp can leave an offset that moves with the envelope), cleared by
  all-sound-off. Voices pan at centre with an equal-power law, so a single centred voice is 3 dB down in each channel.
- Parameters that would step at a block edge (position, levels, cutoff, master, pitch bend) are smoothed per sample.
  Events are sample-accurate, and every block size gives the same output.

## Measured (the tests, 300 checks; also under ASan/UBSan)

- Every note from 36 to 100 is within half a cent; transpose, fine and a full-range bend land on their targets.
- **Aliasing, a square at position 1.0 (power away from the harmonics, DC counted as harmonic 0):**
  -77 dB at note 60, -81 at 72, -78 at 84, -79 at 96, -89 at 105. The same table read the naive way (mip 0, linear
  interpolation) measures -24.5, -21.0, -17.6, -14.8 and -12.6 dB: the metric sees aliasing, and the mips remove it.
- **Warp aliasing at note 60, amount 0.7:** Bend -42.6 dB, Sync -38.5 dB, Fold -33.1 dB (it was -22.0 dB before the
  antialiasing). Warping a band-limited table creates content past Nyquist that cannot be removed afterwards.
- Unison at Amount 0 keeps the fundamental within 1 dB of one voice for 2, 4 and 8 voices; at full Amount the level is within
  2 dB. A warp leaves no DC at the output.
- The filter's measured response matches the model within 0.3 dB for all five types at five pitches; the 24 dB low-pass is
  -6 dB at its cutoff.
- Envelope attack time, 63 % decay and release constants, LFO rate and range, voice stealing, sustain pedal, CC 120 and 123,
  velocity scaling, sample-accurate events, every one of the 41 controls having an audible effect.
- 300 random patches with random notes at 44.1, 96 and 192 kHz: no NaN or inf, output below 40.

## Not done

- No look-ahead or oversampling in the warps; Bend and Sync alias at the levels above. Oversampling the oscillator reads 2x
  would help, at about twice the cost.
- No arbitrary-table import (WAV or Serum `.wav` tables, as campione loads `clm`); the five families are generated.
- No general modulation matrix: a filter envelope (to cutoff and position), an LFO (to position, pitch, cutoff) and
  key tracking are fixed routes. One filter, not two.
- Voice stealing can click (the stolen voice stops at once); no portamento, no mono or legato mode, no mod wheel mapping.
- Not listened to; no host validation (REAPER). The Fold worst case is heavy (up to about 90 % of a core).
