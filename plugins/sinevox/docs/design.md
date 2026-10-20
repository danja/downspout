# Sinevox design

## Signal flow

```
in 1/2 ─► mono ─► 4th-order LP + decimate (~16 kHz) ─► pre-emphasis ─► 25 ms Hann window
                                                                              │ every 5 ms
                                                          autocorrelation + lag window
                                                          Levinson-Durbin (order 2N+2)
                                                          Durand-Kerner roots (warm start)
                                                                              │
                                  candidates: freq, bandwidth, amplitude ─► slot tracker
                                                                              │
                                    per-sample smoothing: log-freq, Q, amplitude
                                                │                      │
                                  Shift / note offset           MIDI out (CC / notes)
                               ┌──────────┴──────────┐
                  vocoder: SVF band-pass on 3/4        sines
                               └──────────┬──────────┘
                                       Level, Speech Mix ─► out 1/2
```

## Analysis

LPC rather than FFT peak-picking because it models the vocal tract envelope
directly: an all-pole fit puts poles at resonances, and the harmonics of the voice
mostly fall between them. The input is decimated to about 16 kHz (`round(sr/16000)`,
so 44.1 k and 48 k both give 3, 96 k gives 6) so that `2N+2` poles are spent on
0–8 kHz. The model order is `2N+2`: N formants plus one spare pole pair so a real
formant is not displaced by a spurious one.

- **Pre-emphasis 0.97.** Speech falls roughly 6 dB/oct once glottal slope and lip
  radiation are combined; pre-emphasis flattens that so the poles go on
  resonances. A first version of the test used a bare impulse train through
  resonators, which is flat, and the tracker read F1 12 % high. The test source is
  now given the speech-like tilt. A flat source (e.g. a synthesiser's pulse wave)
  will see the same upward bias on the lowest formant.
- **Lag window** (Gaussian, 40 Hz) keeps poles off the unit circle.
- **Roots** by Durand-Kerner on the order-`P` polynomial, warm-started from the
  previous frame (usually a handful of iterations), with a cold restart if the warm
  start does not converge. Fixed-size arrays, no allocation, so it runs on the
  audio thread; about 70× real time with eight formants and both engines.
- **Candidates**: roots with positive imaginary part, 90 Hz up to 0.46× the analysis
  rate, bandwidth under BW Limit. Frequency `= arg(z)·fs/2π`; bandwidth
  `= −ln|z|·fs/π`; Q `= f/bw`.
- **Amplitude**: the all-pole envelope at the peak, `sqrt(err/Σw²)/|A(e^jω)|`, with
  the pre-emphasis response divided out, times `sqrt(2γ)` with `γ = π·bw/fs`. That
  is the amplitude of a sine carrying the same power as the resonance's Lorentzian
  (`power = |H|²γ`), so a sine bank at the tracked values has roughly the speech's
  level.

## Tracking

Eight persistent slots. Each frame, candidates are taken strongest first and claim
the nearest unclaimed slot within 0.75 octave of where that slot is (or of its
nominal centre 500, 1500, 2500… Hz if it is silent). Unclaimed slots fade out. A
slot that was inaudible snaps to its new frequency instead of gliding from a stale
one. Frame level below Gate marks every slot silent.

Slots are *not* sorted by frequency; identity is more useful than order when a
formant is being turned into an oscillator or a MIDI note.

## Synthesis

- **Smoothing** is per sample: log₂ frequency and Q on Glide, amplitude on Attack
  (rising) and Release (falling).
- **Vocoder**: Simper/TPT state-variable band-pass (unity peak gain), so modulating
  frequency and Q does not click. Coefficients refresh every 16 samples. Band gain
  is `amp·tilt / (carrierRMS^norm · sqrt(2π·bw/fs))` — the denominator is the power
  a white carrier would put through a band that wide, so with Carrier Norm 100 %
  the output has the speech's formant power whatever the carrier level. The
  carrier RMS follower rises in 5 ms and falls in 400 ms so a note starting does not
  produce a burst. Gain is capped at 200.
- **Sine speech**: one sine per slot with continuous phase; frequency changes only
  alter the increment.
- **Idle slots** (amplitude below −100 dB and no target) are skipped and have
  their filter state zeroed, and remaining states are flushed per block, so a
  released bank does not decay into denormals.

## MIDI

One primary MIDI input carries controllers and notes; both are filtered by the
In Channel. CC numbers are parameters (0 = off). The held-note stack is last-note
priority. Output is generated in the portable core (`collectMidiOut`) and the
wrapper only translates it, so the CC/note logic is tested. MIDI out is quantised to
the block (events at frame 0) and rate-limited, which is adequate for control but
not sample-accurate.

## Assumptions

- **State.** Every setting is an ordinary parameter, which DPF saves by symbol, so
  there is no `WANT_STATE`. `scripts/check-plugin-state.sh` lists it under
  "parameters only". `activate()` clears runtime state only; the patch lives in the
  parameter array.
- **CC feedback to the panel.** CC overrides write the processor's parameter array
  but DPF has no plugin→host notification for input parameters, so the panel does
  not move. Output parameters carry the status (formant frequency, level, Q, input
  level), since those are measurements.
- **Status is pre-shift.** The panel plots what the tracker hears; the shifted
  positions are marked with triangles on the axis. MIDI output is post-shift.
- **Bypass** passes speech 1/2 straight to the outputs; the analyser keeps running
  so MIDI out and the panel stay live.
- **Stereo.** Speech is summed to mono for analysis. The vocoder processes 3 and 4
  separately with shared coefficients, so a stereo carrier stays stereo.
- **Latency.** None reported. The analysis window adds about 12 ms of lag to the
  *control* signal only; the carrier is not delayed.

## Tests

`tests/sinevox_core_tests.cpp`: formant accuracy at 44.1/48/96 kHz, spectra of both
engines measured with Goertzel, silence and gate, every control's audible effect
(mode, formants, bypass, gate, BW limit, glide, attack, release, shift, Q scale,
tilt, level, speech mix, carrier norm, spread), CC and note input, MIDI out
(CCs, notes, hanging-note release), activate behaviour, denormals and run time.
