# Tilt: design notes

## Requirement

A stereo master-bus tone control whose gain is a straight line in dB against log
frequency, pivoting at **Pivot Freq**. **Angle** sets the slope; **Slope** is
selectable at 6, 12 or 24 dB/oct (default 6). Both a minimum-phase path with no
latency and a linear-phase path that reports latency must be available, and the
panel must show the response against a live spectrum. The two channels share one
tilt.

## Response law

Let `S` be the selected slope in dB/oct and `A = Angle` in -1..1. The target is

    GdB(f) = clamp(A * S * log2(f / Pivot), -24, +24)

so a fresh instance (`A = 0`) is bit-for-bit transparent. The underlying power
is `p = A * S / 6`; a first-order (`p = 1`) tilt is 6.02 dB/oct, hence the factor
of six in `idealDb()` and `idealFrom()`. The clamp is a master-bus guard: at the
extremes a wide-range tilt otherwise asks for more than 100 dB of gain.

## Minimum-phase path

`design()` builds a cascade of prewarped first-order shelf sections between 20 Hz
and `min(20 kHz, 0.49 sr)`:

- The integer part of `|p|` is up to four whole shelves, one per 6 dB/oct. A
  boost puts the zero low and the pole high; a cut swaps them.
- The fractional part is a six-section Oustaloup ladder. Its pole/zero pairs are
  log-spaced between the corners so that their product approximates `f^alpha`
  with much less ripple than interpolating the coefficients of one shelf.
- The product is normalised at the pivot, so the cascade crosses 0 dB exactly
  there. Coefficients and state are double precision; tiny state values are
  flushed per sample to avoid denormals.

Interpolating shelf coefficients was tried first and rejected: it changed the
shelf's transition but not its asymptote, leaving a roughly -26 dB offset at the
pivot for a half-order tilt. The tests sweep the whole slope range and check the
measured magnitude at several frequencies, not just the displayed curve.

## Linear-phase path

Each channel has a 4096-point sqrt-Hann STFT with a hop of 512. A frame is
windowed, transformed with a local radix-2 FFT, multiplied by a real gain per
bin from `idealFrom()`, mirrored to preserve conjugate symmetry, inverse
transformed and overlap-added with the reciprocal of the summed squared windows.
DC has no imaginary part and Nyquist is real. Bin gains are smoothed at the hop
rate with the same 50 ms time constant as the other controls.

The analysis window spans 4096 samples, so `latencySamples()` reports 4096 in
FFT mode. IIR mode reports zero. The host sees `setLatency()` change when Mode
moves past its halfway point.

## Switching, bypass and output

Pivot Freq, Angle, Mode, Bypass and Output are smoothed once per block with a
50 ms time constant; the first block snaps to the patch. While the mode mix is
below 0.1 the IIR path uses live audio; above that it reads from a 4096-sample
ring so it is phase-aligned with the STFT output. The dry path uses the same ring
for the bypass crossfade, except at the very end of the bypass transition where
it goes direct. The result is click-free switching without a comb from the two
paths being time-misaligned. Output gain is smoothed with the same coefficient.

Bypass passes the input through the output gain but does not change the reported
mode latency: a host that compensates 4096 samples remains compensated across a
Mode change, even though the bypassed signal itself is undelayed.

## Panel and analyser

`TiltUI` follows the Magneto look. The response panel draws the target line
faintly, the actual response brightly, and the live spectrum bars behind both.
The spectrum is measurement-only: a separate Hann-windowed 4096-point FFT of the
mono sum, refreshed about 30 times a second, folded into 64 log-spaced bands
from 20 Hz to 20 kHz. A narrow band is interpolated at its centre and a wide one
uses its loudest bin; a full-scale sine reads 0 dB. Levels have instant attack
and a 40 dB/s fall. They reach the panel as read-only output parameters, so no
UI data channel is needed. `MagnetoUI` holds at most 64 values, so the controls
are given to the base and the 64 bins are kept in the derived UI.

Spectrum source is Off / Pre / Post (default Post). Off resets and stops the
analyser. Peak meters read the finished output and fall at 30 dB/s.

## Host mapping and state

- **Audio:** stereo in, stereo out, linked.
- **MIDI / transport:** none. There is no CC map and no time-position use.
- **State:** none. Seven writable settings are ordinary host parameters;
  latency, two peaks and 64 spectrum bars are output-only. `activate()` and
  `sampleRateChanged()` clear only runtime buffers and never reset the patch.
- **Version:** v0.1.0, `tilt.vst3`.

## Tests

`downspout_tilt_core_tests` (112,497 checks, 0 failures) covers the parameter
law and clamps, exact default pass-through, IIR magnitudes against the ideal
line at 100 Hz-10 kHz at all three slopes, pivot and angle audibility, FFT unity
round trip with the reported latency, exact FFT line following, linked stereo,
bypass, output gain, patch persistence across `reset()` and block-size
invariance.

## Not done

- No host listening, no REAPER round trip and no CPU measurement under load.
- No dynamic tilt, per-channel trims, MIDI/CC control or analyser peak hold.
