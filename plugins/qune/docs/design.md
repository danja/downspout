# Qune design notes

An original pitch quantiser in the Magneto look. Not a port; the algorithms are textbook
(McLeod NSDF, PSOLA, Laroche-Dolson phase locking, cepstral envelopes) written for this
repository.

## Layout

| File | Role |
|---|---|
| `include/qune_params.hpp` | Parameter ids and specs, the 24 scale tables, enums, range table, helpers. Portable |
| `include/qune_pitch.hpp`, `src/qune_pitch.cpp` | Pitch tracker |
| `include/qune_history.hpp` | Stereo input ring by absolute index, cubic reads, `ShiftControl` |
| `include/qune_shifters.hpp`, `src/qune_fast.cpp`, `src/qune_psola.cpp`, `src/qune_spectral.cpp` | The three shifters |
| `include/qune_fft.hpp` | Radix-2 FFT, used by the spectral shifter |
| `include/qune_engine.hpp`, `src/qune_engine.cpp` | Target selection, MIDI, glide, tier switching, mixing |
| `src/dpf/` | Thin DPF wrapper and NanoVG panel |
| `tests/qune_core_tests.cpp` | Deterministic core tests (about 190 000 checks) |

The wrapper holds a `Parameters` struct and an `EngineState`. Nothing in the core knows about
DPF.

## Pitch tracker

1. Mono sum, DC blocker (25 Hz).
2. Two cascaded biquad low-passes, then decimation to between 8x and 16x the range's top
   pitch (about 3 kHz for Bass, 8 kHz for Voice, 16 kHz for Wide at 48 kHz).
3. Every 4 ms, a normalised square difference function (McLeod) over a window of 1.25
   periods of the lowest pitch. Peaks are found by *prominence* (0.1) rather than by zero
   crossings: a bright waveform can keep the NSDF positive from lag zero through its first
   period peak, and a zero-crossing search then reports an octave low. This was found with a
   test, not by reading about it. The first peak within 0.95 of the best (parabola-corrected
   height) wins.
4. The winning lag is refined on the full-rate signal over +-(decimation+1) samples with a
   second NSDF, then parabolically interpolated. The decimated NSDF samples a short period
   coarsely, so a period that falls between decimated samples scores lower than its own
   double; the full-rate NSDF is therefore also asked about the half-period (twice), and the
   half wins if it scores within 0.88 of the full period. This is what fixed an octave error
   at 1.2 kHz on 96 kHz input.
5. A small state machine: clarity above a threshold set by **Voicing**, a level gate of
   about -62 dBFS, a 3-hop hold across dropouts, and a jump of more than 2.5 semitones must
   repeat within 0.7 semitones on two consecutive hops before it is believed.

Measured on additive vowels at 44.1, 48 and 96 kHz, every range: within 6 cents in the test,
better than 1 cent below 700 Hz, worst 3.4 cents at 1.2 kHz and 44.1 kHz.

The synthetic vowels are additive (exact period) rather than an impulse train through
resonators. A sampled impulse train jitters by up to a sample, which at 650 Hz is 1.4 % of
the period; that tested the detector against noise the signal should not have, and cost an
afternoon.

## Target selection and glide

Per hop: detected MIDI note `m`; target `t` is one of

- the nearest note whose pitch class is in the rotated scale mask (ties go down);
- the nearest held pitch class, any octave (MIDI chord);
- the last held note (MIDI note), plus pitch bend.

Stickiness keeps the previous target while it is a neighbour of the best note and less than
the stickiness further from `m`. The shift is `amount * (t - m) + transpose`. A one-pole
filter on the shift implements **Glide**; because it smooths the shift rather than the target,
a long glide lets part of the natural vibrato through (the same model as other tuners' retune
speed). Unvoiced input releases the shift in 5 ms; a new phrase starts at its target rather
than sliding in from the last one.

Transpose is an integer parameter by design: the target is `t + transpose` and `t` is a whole
note, so the shift is the only fractional part. (A probe at +0.5 semitone showed the
parameter rounding; that is the contract, not a bug.)

## Shifters

All three read the same stereo history ring, take one `ShiftControl` (ratio, formant ratio,
period, voiced) per 32-sample sub-block, and write wet output at their own latency.

### Fast: pitch-synchronous splice delay line

One read head moves through the history at the ratio. When it has drifted past a span of
whole periods (at least 3 ms) it jumps by that span with a raised-cosine crossfade, after
sliding the landing point (+-T0/8) to maximise correlation with the waveform just played.
Pitching up leaves room before the newest sample for the outgoing head, which keeps moving
during the crossfade; without that margin the outgoing head reads the future.

At ratio 1 the head stands still and the signal is a plain delay: no comb filter, no
modulation. Latency is the head's position (4 ms nominal at 48 kHz).

### Good: PSOLA

Hann grains are cut from the input at pitch marks and overlap-added at the new period. The
grain's own spectrum is untouched, so formants are preserved; resampling the grain contents
by `formantRatio` moves them. Three details matter, each found by measuring:

- **Marks are fractional.** Rounding grain centres to whole samples put half a sample of
  timing error on each mark: 7 cents of pitch error at 550 Hz. With fractional centres and a
  parabolic-interpolated correlation slide the error is 0.0 cents.
- **The first mark sits on a waveform peak.** A mark that lands mid-cycle makes neighbouring
  harmonics inside the grain interfere, which shows as notches (15-25 dB) in the output
  spectrum whenever pitch and formants move apart. Snapping the first mark of a run to the
  largest peak near the ideal position, and letting the slide preserve that alignment, brings
  the formant-shifted envelope to within 4 dB.
- **Overlap is normalised by the window weight actually present**, and the grain length is
  chosen so neighbours overlap at least 1.6x (and under 2.5 periods so the output half fits
  in the latency). Uneven overlap otherwise becomes amplitude ripple.

Latency is 2.4 periods of the range's lowest pitch plus 64 samples: a grain reaches one
period past its centre into the input, and its output half-window has to be finished before
the first sample of it is emitted. That is inherent to symmetric two-period grains, which is
why the Lead range (160 Hz floor) is 16 ms and the Bass range 61 ms.

### Best: phase vocoder with peak locking

N = 2048 (4096 above 50 kHz, 8192 above 100 kHz), hop N/4, Hann analysis and synthesis. Both
channels ride one complex FFT. Spectral peaks (local maxima over five bins) and their regions
of influence are moved to `round(f * ratio)`, rotated so the peak's phase advances at the new
frequency, and the region keeps its internal phase relationships (Laroche and Dolson's
identity phase locking). Bins below the first peak are dropped: moved with the first region,
DC and window leakage reappeared as a tone at the old pitch.

Formants: the log-magnitude envelope is the low quefrency part of the cepstrum (cut at 0.6 of
the pitch period, 24-240 samples, with a cosine taper), computed for both channels with two
more FFTs. The shifted spectrum carries the original envelope scaled by the pitch ratio; the
wanted one is scaled by the formant ratio, so each bin is multiplied by `E(k / formant) /
E(k / ratio)` (clamped to +-20 dB). At ratio 1 and formant ratio 1 the frame is passed
through untouched, which is an exact reconstruction.

Latency is N: an output sample needs every frame that covers it.

## Engine

- Sub-blocks lie on an absolute 32-sample grid, split at host block edges and at MIDI event
  times, so MIDI is sample-accurate.
- Quality or range changes fade the whole output out over 5 ms, reset the new shifter (and
  the tracker, for a range change), then fade back in. The latency of the dry and wet paths
  both change at that moment, so fading only the wet signal clicked (measured: a 0.21 step
  against a 0.02 input step).
- The dry path reads the same history at the active tier's latency.
- Denormals: the tracker's biquads and DC blocker are flushed each block. The shifters have
  no recursion. Tested: silence after a note costs no more than the note.

## Measurements

48 kHz, Voice range, one core, Release (-O3), a held vowel, as a fraction of real time:

| | Tracker | Engine, scale mode | Engine, 2-octave MIDI shift |
|---|---|---|---|
| Fast | 0.6 % | 0.7 % | 1.6 % |
| Good | 0.6 % | 1.4 % | 1.8 % |
| Best | 0.6 % | 3.1 % | 3.1 % |

The tracker alone is 0.4 % (Lead), 0.8 % (Bass) and 4.7 % (Wide). The worst combination the
test suite runs, Wide range at 96 kHz with a 5-semitone MIDI shift: Fast 7 %, Good 6 %,
Best 9 %.

Accuracy of the corrected pitch, vowels at 150-550 Hz shifted by -1 to +7 semitones:
within 0.5 cent on all three tiers. Level through the shifters stays within 3 dB.

Formant tests (harmonic ratio to the true vowel envelope, standard deviation across
harmonics): Good preserve 0.7 dB, Best preserve 3.5 dB; Good formant +7 st 1.6 dB, Best 4.3 dB.
The comparison against the *other* hypothesis is 6 to 7 dB, so each mode puts the envelope
where it says.

## Not tested, not done

- **Listening.** Everything above is numerical. No vocal or instrument recording has been
  through it. Artefacts the numbers do not show (smearing, a "tube" sound in Best, the
  character of Fast's splices on real voice) are unjudged.
- **A real host.** Latency reporting, quality switching under automation, MIDI input and
  parameter persistence are unexercised in REAPER or any other host. The core tests never
  touch the wrapper.
- **Tracker robustness on real voice.** Tests use clean synthetic vowels, noise, silence and a
  short glitch. Breathy, creaky or strongly vibrato'd singing, and noisy rooms, are the next
  thing to try; Voicing and Stickiness exist to tune for them.
- **Polyphonic and unpitched material** is out of scope by design.
- Ideas, not commitments: a manual per-note enable mask beyond the 24 scales; a lookahead
  option for Good that trades latency for earlier note decisions; PSOLA grains that follow
  rather than assume a constant period within the grain.

## Assumptions for DPF/VST3

See the README: no state, no transport, one MIDI input, status through output parameters,
latency reported with `setLatency` when the tier or range changes.
