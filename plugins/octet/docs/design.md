# Octet: design notes

Source read: `~/github/oxide-studio/crates/oxide-fx/src/eq8.rs` (MIT/Apache-2.0). The biquad maths
(RBJ cookbook bell, shelves, notch, Butterworth cuts) is standard and was kept; the structure
around it was redesigned.

## What the source does (from reading the code; not run)

- **The 48 dB cuts are wrong.** `LowCut48` and `HighCut48` run four identical second-order Butterworth
  sections (Q = 0.707). That has the right slope but puts the -3 dB point of each section at the cutoff, so
  the cascade is -12 dB there. A proper 8th-order Butterworth uses four sections with
  Q = 1 / (2 cos((2k-1) pi / 16)) = 0.510, 0.601, 0.900, 2.563 and is -3 dB at the cutoff.
- **Fixed band-to-channel split.** In Left/Right and Mid/Side modes bands 1-4 act on one channel and 5-8 on
  the other, with no choice.
- Processes only the first 1024 frames of a buffer (`frame_count.min(1024)`).
- A 64-bin naive DFT spectrum (512 samples x 64 bins x sin and cos, about 65,000 trig calls per block) on the
  audio thread.
- Single-precision filters; at a 20 Hz cut and high sample rates the poles sit very near z = 1.
- All bands enabled, band 1 a 48 dB low cut at 30 Hz: the defaults are not transparent.

## What Octet does

- Double-precision state and coefficients (the response check at a 20 Hz 48 dB cut at 192 kHz holds to 0.3 dB).
- Proper Butterworth cascades for HP 48 and LP 48; HP 12 and LP 12 are one section at Q = 0.7071.
- Per-band channel assignment (Both / A / B) in L/R and M/S modes; Stereo mode ignores it. M/S uses the
  orthonormal matrix and is skipped (so the signal is bit-exact) when no band is active.
- A band that does nothing (off, or a bell or shelf at 0 dB) is skipped entirely. A fresh instance is
  bit-exact transparent: the test compares the output to the input and requires no difference.
- A band that is switched off drops its filter memory, so enabling it again does not replay old state as a click.
- Output gain is interpolated across each block; coefficients are recomputed only when a band's type,
  frequency, gain or Q changed. Tiny state is flushed once per block. Non-finite input is treated as silence.
- `bandResponseDb()` and `responseDb()` evaluate the exact digital response from the coefficients in use,
  and the panel draws those, so the plot cannot disagree with the filter.

## Measured (the tests, 380 checks)

- The plotted response matches the audio to 0.15 dB for every band type at 48 and 96 kHz.
- Bell: exactly its gain at the centre; higher Q is narrower. Shelves reach their gain on the far side.
  A notch removes its centre by more than 40 dB.
- 48 dB cuts: -3.01 dB at the cutoff and 48 dB (+-2) per octave; 12 dB cuts: -3.01 dB and 12 dB (+-1) per octave.
- L/R and M/S routing: a Mid band acts on a mono signal and not on L = -R; a Side band the reverse; a mono
  signal stays exactly mono.
- Stability is that the ringing decays: over 40 random settings of all eight bands at 44.1, 96 and 192 kHz the
  impulse response falls below 1e-3 of its early peak within 4 s. (An absolute output bound is the wrong test:
  stacked boosts legitimately amplify by over 100 dB.)

## Not done

- Frequency, gain and Q glide to a new target in steps of 32 samples (about 4 ms time constant), rebuilding the
  sections from the gliding parameters so every step is a stable filter; a type change or a band coming on is
  immediate. A gliding band therefore lags a fast automation lane by a few milliseconds.
- No linear-phase mode, no per-band solo or listen, no match-EQ, no auto-gain.
- Not listened to; no host validation (REAPER).
- **Spectrum analyser.** A Hann-windowed 4096-point FFT of the mono sum (L+R)/2, refreshed about 30 times a second on the audio thread (a few hundred microseconds each; the upstream 64-bin naive DFT was about 65,000 trig calls per block), folded into 64 log-spaced bands from 20 Hz to 20 kHz. A narrow low band is interpolated at its centre, a wide one takes its loudest bin, and a full-scale sine peaks at 0 dB. Instant attack, 40 dB/s fall. The **Spectrum** parameter (parameter 52: Off, Pre, Post, default Post) picks the tap: Pre reads the inputs before they are processed (so it is safe for in-place hosts), Post reads the outputs; Off costs nothing and lets the bars fall away. The levels reach the panel as 64 read-only output parameters (53 to 116, `spectrum_01`...`spectrum_64`, -90 to +6 dB), the same route as the other plugins' meters, so there is no UI data channel to invent. They are not part of the patch (`field()` does not store them). `MagnetoUI` holds at most 64 values, so the panel passes it the 53 controls and keeps the bins in its own array. The bars sit behind the curves on their own scale (floor to 0 dB over the plot height; the plot grid is the EQ's dB).
- **Drift CC.** CC 1-4 on the CC channel drive one band, chosen by the CC band parameter (default 4): Frequency, Gain, Q and the Output. There are no four natural live controls across eight bands, so the band is the choice: a sweepable, boostable bell is what an LFO or a performance lane does to an EQ. `ccBand` and `ccChannel` are appended after the band controls (parameters 50 and 51). Moving a held control on the panel releases it; changing the CC band releases all four.
