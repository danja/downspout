# Moka porting notes

Moka is an original instrument, not a port, so there is no LV2 source to
preserve. The design follows the repository working pattern: portable core,
deterministic tests, thin DPF wrapper, custom NanoVG UI.

## DSP mapping

Modal synthesis maps to struck objects as follows:

- the **resonator** is a bank of eight sine modes per voice; each mode has a
  frequency ratio, a relative level, and a decay multiplier taken from the
  instrument table in `moka_params.hpp`;
- the **exciter** is a short filtered-noise transient (3–12 ms depending on
  Mallet) plus initial mode amplitudes weighted by Mallet (spectral slope)
  and Position (strike point);
- **Decay** scales every mode T60 by 0.15x–3x around the instrument base;
- **Spread** stretches partial ratios away from the table values for
  detuned-plate or bell-like inharmonicity;
- **Tone** is a global one-pole lowpass after the modal sum;
- stereo comes from alternating partials left/right with a note-dependent
  jitter, so the "2-channel" output is genuinely wide on metal and glass.

## Onset

The modal bank and the mallet burst both start at full amplitude. That is what
an idealised strike does and what a digital resonator cannot: the first sample
out of silence is a step of several tenths, and a chord sums those steps into
full scale, so every note opened with a broadband impulse.

The fix is an onset ramp over the mallet contact time — 1.5 ms for a hard
beater up to 6 ms for a soft one, raised cosine so both the value and its
slope start at zero. The ramp blends from the voice's previous output rather
than from zero, so retriggering a held note crossfades its tail rather than
cutting it. The tone filters keep their state across a retrigger for the same
reason.

The mallet burst noise is seeded per note from note, velocity and serial
(`noiseSeed`). It used to be left running from voice construction, so every
strike on a given voice replayed one identical burst waveform.

## Shared table maths

`computeModeWeights`, `decayScaleFor`, `modelRingTimeSeconds` and
`toneCutoffHz` live in `moka_params.hpp` rather than inside the engine. The
engine renders from them and the panel's partial ladder and ring-time dial
plot them, so the drawn spectrum and the reported decay time cannot drift away
from the sound. Duplicating that arithmetic per consumer is how a panel starts
lying about what the plugin is doing.

## Assumptions

- Transport is irrelevant: Moka is a pure MIDI-triggered synth with no
  clock, meter, or state serialization beyond host parameters.
- Voice stealing prefers releasing voices, then the oldest serial; retrigger
  of a held note restarts that voice and crossfades its tail.
- Rendering is deterministic: per-voice xorshift noise is seeded from
  note, velocity, and serial, so identical MIDI renders identical audio.
- Glass Bowl beating comes from a close 1.000/1.006 mode pair, not from a
  chorus effect, so it tracks pitch correctly.
