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

## Assumptions

- Transport is irrelevant: Moka is a pure MIDI-triggered synth with no
  clock, meter, or state serialization beyond host parameters.
- Voice stealing prefers releasing voices, then the oldest serial; retrigger
  of a held note restarts that voice.
- Rendering is deterministic: per-voice xorshift noise is seeded from
  note, velocity, and serial, so identical MIDI renders identical audio.
- Glass Bowl beating comes from a close 1.000/1.006 mode pair, not from a
  chorus effect, so it tracks pitch correctly.
