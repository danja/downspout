# Fathom design notes

## Source

The request was a drone-oriented plugin "inspired by" the Vhikk X algorithm reference
(<https://forge-tme.com/vhikk-x/algos_launch.html>), with MIDI in for notes and CC, stereo
out and the Magneto look. The page is documentation for a hardware module, not code, and
gives no numeric ranges or DSP, so **this is an original implementation of the described
signal flow**, not a port. Nothing in it is taken from the module's firmware.

What the page describes: a generator (oscillators), a filter section including a multi-notch
with resonance and shape, a VCA, then a delay-based processor with a frequency shifter in its
feedback loop; nine algorithms from two three-way switches (generators: two analogue-style
oscillators, a waveshaping pair, a multi-sine cluster bank, four-oscillator wavetable
scanning, four-oscillator cluster scanning, one diatonic-quantised; processors: "Dungeon"
metallic delay/reverb and "Reflekta" diffuse delay); and thirteen controls (WARP, SPAN, MIX,
FEED, MORPH, SEED, SCAN, BASIS, TIME, FORM, FUSE, FIELD, CELL).

## What was kept, changed and left out

| Vhikk X | Fathom |
|---------|--------|
| Two switches select 9 generator/processor pairs | Generator and processor are chosen independently: 5 generators x 2 processors |
| Analogue-style pair | **Analog**: saw and pulse, PolyBLEP, exponential cross-FM |
| Dynamic waveshaping pair | **Shaper**: PM carrier, ring-mod blend, wavefold |
| Multi-sine cluster bank | **Sines**: eight sines |
| 4-oscillator wavetable scan | **Table**: four oscillators, eight generated frames |
| 4-oscillator cluster scan, diatonic quantised | **Cluster**: four oscillators on major-scale steps |
| "Dungeon" metallic delay/reverb | **Metallic**: four-line FDN, Hadamard matrix |
| "Reflekta" diffuse delay | **Diffuse**: ping-pong delay through allpass diffusers |
| Frequency shifter in the feedback loop | Single-sideband shifter per line, in the loop |
| WARP, SPAN, MORPH, SEED, FUSE | Warp, Span, Morph, Seed, Fuse (meanings per algorithm below) |
| SCAN (randomised scanning of timbre) | Folded into Seed, which scans smoothly |
| MIX, FEED, TIME, FORM, CELL | Mix, Feed, Time, Form, Shift (CELL is the shift amount) |
| FIELD (filter centre) | Field |
| BASIS (root frequency, 1V/oct) | Basis (transpose); the root comes from MIDI notes |
| Multi-notch filter, resonance and shape | A low-pass plus two notches (**Notch**). No resonance control |
| Stereo audio inputs | Not provided: Fathom is an instrument with no audio input |
| FEED also selects the shifter's mix type | Not reproduced; the shifter is always in the loop |

Drone-specific additions: four voices panned apart, **Hold** (sustain pedal), **Glide**,
long attack and release, pitch bend, a MIDI channel filter.

## Signal flow

```
note -> generator -> notches -> low-pass -> envelope -> pan -> [sum] -> processor -> mix -> level -> DC block -> out
                                                              \_ dry ________________________/
```

Per voice: generator, two notch filters, a low-pass, an attack/release envelope. The four
voices sum, a mono copy feeds the processor, and its stereo wet is mixed with the panned dry.

## Generator controls per algorithm

| Algorithm | Warp | Span | Morph | Seed | Fuse |
|-----------|------|------|-------|------|------|
| Analog | pulse FM-modulates the saw, up to two octaves | pulse detune, up to a fifth, quadratic | saw/pulse to sine | unused | pulse width 50-90% |
| Shaper | PM depth | modulator ratio 0.5-4 | carrier to carrier x modulator | unused | wavefold gain 1-6 |
| Sines | bit depth 16 down to 3 bits | partial detune up to 3%; the near-unison partner up to 1% | tilt exponent 3.3 down to 0.3 | which partials drift where | tanh saturation |
| Table | each oscillator PM'd by its neighbour | tuning fan, +-90 cents at most | frame scan 0-7 | per-oscillator frame offsets | wavefold |
| Cluster | ring modulation of neighbours | step width up to 14 semitones | scan window across the four | which steps | sine to saw |

Controls move through a 20 ms smoother updated every 32 samples, so a stepped CC does not
click. **Algorithm** and **Processor** are not smoothed; changing them while sounding clicks.

At Span 0 every algorithm's fundamental is the played pitch. Tests check that the fundamental
dominates at 110, 220 and 440 Hz for every algorithm, and that an octave up doubles the
frequency.

## Frequency shifter

Two chains of four allpass sections (Olli Niemitalo's Hilbert pair) give outputs a quarter
cycle apart; rotating them by the shift phase moves every component by +-shift Hz. The first
draft used the wrong section sign and gave both sidebands equally (a ring modulator). A
Goertzel probe found it, and the rotation sign was fixed by measuring which way a tone moves.

Measured sideband rejection with a 100 Hz shift: 55 dB at 200 Hz, 49 dB at 1 kHz, 47 dB at
5 kHz. The tests require at least 26 dB.

The shifter runs only while the shift is not zero.

## Processor

**Metallic.** Four delay lines whose lengths are fixed ratios (1, 1.31, 1.73, 2.29) of the
base time. Each line's output is damped, optionally shifted, mixed through a Hadamard
matrix, and fed back with the input. **Form** crossfades the feedback from each line
returning to itself (a bank of echoes) to the full matrix (a diffuse tail), and adds input
allpass diffusion and a slow delay modulation of up to 2 ms. Left is lines 0 and 2, right is
lines 1 and 3.

**Diffuse.** Four series allpass diffusers on the input, then two delay lines (ratio 1 and
1.27) that feed each other, so repeats alternate between the ears. Each loop passes through
its own diffuser. The shifter sits in the loop.

**Stability.** Feedback is capped at 0.97 and every write passes through a soft clip, so the
loop is bounded with Feed at maximum and any Shift. Tests render four seconds at Feed 100%
with and without shift for both structures and require the tail to decay and the peak to
stay bounded. Denormals are flushed on every recursive path; the idle-cost test renders over
five seconds of tail in well under real time.

**Time** is smoothed over 150 ms, so moving it bends the pitch of the tail, as a tape delay
would, instead of clicking. The delay lines are allocated in `activate()` for 2.2 seconds at
the sample rate.

## Voices

Four voices. The same note reuses its voice, then a free voice, then the quietest releasing
voice, then the oldest. The voices are panned at -0.45, +0.45, -0.2 and +0.2, so a chord
opens across the field. Velocity maps to 60-100% of the voice gain, because a drone should
not jump around.

**Hold.** With Hold on, note-offs clear the key but leave the gate open. Turning Hold off
releases voices whose keys are up and leaves keys that are still down. CC 64 drives it
(64 and above is on), and the value appears on the Hold parameter. CC 123 releases
everything, held or not; CC 120 silences all voices and clears the delay network and the DC
blocker so the cut is immediate.

**Glide.** A voice that starts fresh begins at the previous note's pitch and slides to its
own, with a one-pole in the semitone domain. A retriggered voice keeps sliding from where it
is. Basis changes glide the same way.

## MIDI map

CC 1-4 are Warp, Span, Morph and Seed. Drift's four default lanes emit CC 1-4, so Drift
drives them unconfigured; a test reads Drift's lane table and fails if the two drift apart.
CC 5 Fuse, 6 Field, 7 Level, 8 Notch, 9 Mix, 10 Time, 11 Form, 12 Feed, 13 Shift, 14 Basis.
CC 64, 120 and 123 are the usual meanings. Every CC spreads 0-127 across the parameter's
full range, so Shift (bipolar) is centred at 63.5. Incoming CC writes through to the
parameter store, but DPF has no DSP-side path to move the on-screen slider.

## State

No DPF state: `WANT_STATE` is not set. Every setting is an ordinary parameter, which the host
saves with the project. `activate()` and a sample-rate change clear runtime state only (a
test checks the patch survives both).

## Cost

Measured with four sounding voices at -O2 on this machine: 3.6-6.2% of one core, depending
on the algorithm and processor, at 48 kHz.

## Known limits

- Changing Algorithm or Processor while sounding clicks.
- The wavetable algorithm is band-limited in six steps (32, 16, 8, 4, 2, 1 harmonics), so a
  very high note is plainer than a middle one. The aliasing test runs at 3 kHz.
- The sine table is linearly interpolated (4096 points), which is fine for a drone but not a
  measurement-grade sine.
- The panel's sliders are linear; the long times (Attack, Release, Glide) are coarse in
  the short range.
- Not auditioned by ear: levels and the character of each algorithm were judged from
  measurements and tests only. A three-note chord on the default patch peaks around 0.2-0.6 depending on the algorithm.
- Host validation in REAPER has not been done.
