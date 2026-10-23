# Quartet: design notes

Source read: `~/github/oxide-studio/crates/oxide-dsp/src/operator/synth.rs` (MIT/Apache-2.0). The structure (four operators with
ratio or fixed frequency, eleven algorithms, feedback on D, a pitch envelope and a filter) is kept; the engine was redesigned.

## What the source does (from reading the code; not run)

- **The modulation index is huge.** Operator phases are in cycles and the modulator output is added as
  `phase + 3.0 * out`: three cycles, about 19 radians, at full level. The defaults (levels 0.5, 0.3 and 0.2 for B, C and D) therefore
  sound harsh.
- **"Fine" is a percentage of the frequency.** `1 + 0.01 * fine` over -100..100, so the extremes are 0x and 2x (an octave up), not cents.
- **PolyBLEP under phase modulation is wrong.** The saw and square use PolyBLEP with the nominal `dt = f / sr`, which assumes the phase
  advances by `dt` each sample. Under FM the modulated phase moves unevenly, so the corrections land at the wrong places. The 16-harmonic
  "user" waveform is an unbandlimited sum of sines (aliasing above about 1.5 kHz on the fundamental).
- **No oversampling**, so a high index aliases whatever the waveform.
- Algorithm 4's comment says "(D -> C) -> A and B -> A" but the code is D -> (B + C) -> A; the algorithm list is otherwise consistent.
- Only operator A is scaled by velocity, whatever the algorithm; the feedback depth is tied to operator D's level and envelope; a voice
  ends when operator A's envelope ends; only the first 1024 frames of a buffer are processed.

## What Quartet does

- **Operator waveforms** are 2048-point tables (sine; saw `(2/pi) sum (-1)^(h+1) sin(h theta) / h`; square, odd harmonics `4/(pi h)`) in 11 octave
  mips with 1024 down to 1 harmonic, read with cubic interpolation. The mip is chosen from the highest frequency the read can reach, not just the
  operator's: `allowed = (sr / 2) / (f x slope)` with `slope = 1 + 2 pi depth sum(|amp_m| f_m) / f`, the peak speed-up the modulators give the phase.
- **FM depth in cycles, with a sane default:** a modulator's phase offset is `depth x level x envelope x wave` cycles, depth 1.0 by default, so the index
  in radians is `2 pi x depth x level`. Tests check the sidebands against Bessel functions.
- **Fine in cents** (+-50) and **Fixed Hz** per operator.
- **Algorithms are data:** a bitmask per operator of who modulates it and a bitmask of carriers. The engine evaluates operators D to A each (oversampled)
  sample, so every modulator has a higher index than its target; carriers are averaged (1/n). The panel draws the diagram from the same table, and the
  operator layers come from `operatorLayer()`.
- **Optional 2x oversampling** (Quality): the voice runs at twice the sample rate and a 63-tap Kaiser-window half-band FIR (beta 8, unity DC gain,
  every second tap zero) decimates it. Passband ripple 0.001 dB to 18.2 kHz at 48 kHz, stopband -88.8 dB from 0.32 of the oversampled rate. Latency about
  0.3 ms (not reported).
- **Velocity** scales the whole voice. The voice ends when every carrier's envelope has ended. Feedback is a fixed depth (`0.398 x feedback` cycles of the
  two-sample average of D's output, which includes D's level and envelope).
- Filter (trapezoidal SVF, LP 12 / LP 24 / HP 12 / BP / Notch, key tracking) and pitch envelope as in the other synths; pitch, operator frequencies and
  filter coefficients are recomputed every 32 samples. Envelopes use attack linear in time and one-pole decay and release. A one-pole DC blocker at about 8 Hz
  (its onset transient for a note starting at phase 0 is about 0.007 and dies with a 20 ms time constant) is cleared by all-sound-off.

## Measured (the tests, 493 checks; also under ASan/UBSan)

- Sidebands for B (ratio 1) modulating A (ratio 8) at index 0.5 rad: J1/J0 = 0.2582 and J2/J0 = 0.0326, exactly the Bessel values; doubling FM Depth gives
  the beta = 1.0 ratio 0.5751 within 2 %; the sidebands are symmetric within 2 %.
- Saw and square operators at 1x, notes 60-105: inharmonic power at most -77.3 dB (DC counted as harmonic 0).
- **Oversampling on a deliberately extreme patch** (three stages, about 6 rad each): inharmonic power at note 72 falls from -17.1 dB to -53.2 dB,
  at note 84 from -8.4 to -25.6 dB, at note 96 from -1.7 to -8.2 dB. High-index FM at high pitch aliases whatever you do; 2x helps most where the
  sidebands only just pass Nyquist.
- A 2x sine has the same level as a 1x sine within 0.1 dB at notes 60, 96 and 110.
- Pitch to half a cent for notes 36-100; ratio, fine, fixed frequency and a full pitch bend land on target; the pitch envelope starts an octave up and returns to
  the note.
- Every algorithm: an operator is heard only if it is a carrier, and every declared modulator changes its target; carrier output is averaged (two carriers, half level).
- The filter's measured response matches its model within 0.3 dB for all five types at five pitches. Envelope attack, decay and release times, voice stealing,
  sustain pedal, CC 120 and 123, velocity, sample-accurate events, block-size independence, every one of the 57 controls having an audible effect.
- 300 random patches with random notes at 44.1, 96 and 192 kHz: no NaN or inf, output below 40.
- CPU on a busy machine: 1x 1 % of a core for one note, 5.4 % for eight; 2x 1.7 % and 13.4 %; saw operators in algorithm 4 at 2x, eight notes: 15 %.

## Not done

- No user-drawn waveform (the source's 16-harmonic "user" waveform is not ported), no per-operator velocity or key scaling, no operator-level LFO.
- No higher oversampling (4x) for very bright FM at the top of the keyboard.
- Voice stealing can click; no portamento, mono or legato mode; the output is mono (both channels equal).
- Not listened to; no host validation (REAPER).
