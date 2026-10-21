# Grist design notes

## Layout

| File | Role |
|---|---|
| `include/grist_core.hpp`, `src/grist_core.cpp` | Portable core: parameters, grain engine, feedback loop. No framework code |
| `src/dpf/GristPlugin.cpp` | Thin DPF wrapper: parameters, tempo from the host, status outputs |
| `src/dpf/GristUI.cpp` | Magneto-style panel. Window and Delay use logarithmic sliders drawn in the plugin |
| `tests/grist_core_tests.cpp` | Deterministic core tests |

## Signal flow

Input plus filtered feedback goes into a seven-second stereo ring. Grains read it with a
four-point Hermite interpolator; their sum is the wet signal, mixed with the dry input and also
written to the feedback delay line.

A grain starts every `Window / Density` samples. When it starts it draws, in this order: its
length (shortened by Variance), its pitch offset, whether it is reversed, its start offset
(Blur) and its pan. The draws come from a xorshift generator seeded from the Seed parameter.

**Start position.** A forward grain starts `gap` samples behind the write position, where
`gap = blur x u x window + 2`, plus `(ratio - 1) x length` when pitched up. The head start
exists because a grain that reads faster than real time closes the gap by `(ratio - 1)` samples
per sample; without it the grain would reach the write position and read audio that has not
arrived (in a ring, audio from seven seconds earlier). The minimum of 2 keeps the four-point
read behind the write position. A reversed grain reads backwards from `gap`, so the gap only
widens.

**Latency.** The average is `Blur x Window / 2 + max(0, ratio - 1) x Window`. The plugin does not report it to the host, because it is a creative delay that
changes with the controls.

**Level.** Every window curve averages 0.5, and each grain is scaled by `2 / Density`, so the
average level of the wet signal does not change with Density or Shape. Hann grains at Density 2
sum to exactly one, so with Blur 0, Pitch 0 and no randomness the wet signal is the input
delayed by two samples (tested).

**Scale.** A grain's total shift (Pitch rounded, plus its offset) must have a pitch class in
the scale, counted from the input's own pitch (Octaves: 0; Fifths + octaves: 0 and 7; Major,
Minor and Pentatonic from 0). The draw picks uniformly from the allowed whole-semitone offsets
inside Range and Direction, plus "stay on Pitch". Free draws a continuous value.

**Feedback.** The wet output goes into a delay line (free, or from a tempo division), through an
optional state-variable filter (lowpass, highpass or bandpass) and a `tanh` soft limiter, scaled
by Feedback (at most 0.95), and is added to the input before it enters the ring. Tiny values
are flushed so the recirculating tail cannot become denormal.

## Not supported

MIDI control, freeze, reverb, host latency reporting.

## Pairing with a resonator

Grist is a grain stage with no resonance of its own. This section records which resonator in the
repo suits it and what each would need.

**Moka does not fit.** It is a MIDI-triggered synth: each voice is a bank of eight damped sine
modes excited by an internal mallet burst, and it has no audio input (`processStereo()` takes
none). To use it as a resonator the mallet would be replaced by an input stage and the voice
allocator by something tuned from the input or from MIDI, which is a different plugin that
shares Moka's mode tables.

**Resonance Garden already is the resonator.** It takes stereo audio in, runs eight damped
delay-line resonators (each with a feedback loop, damping filter and decay) tuned by held MIDI
notes or an internal scale, and has Excitation, Inharmonicity, Damping, Freeze and Feedback. A
grain stream going into it behaves like one going into a plucked or bowed string bank.

**What would make the pair better**, if listening shows the need:

1. A **Strike** mode in Resonance Garden: an envelope follower on its input that retunes and
   re-excites the voices on each grain, so the bank is played rather than just coloured.
2. Moka's mode tables (bars, glass, plate, tube) as extra Resonance Garden voicings, giving
   inharmonic, struck-object resonances in place of strings.
3. Grist can emit nothing to tell the resonator when grains start. If that matters, a trigger
   output as MIDI (one note per grain) would let Moka-style voices play from the grain clock;
   it is not implemented.
