# Bore

Bore is an eight-voice, MIDI-played lip-reed instrument. Each note excites a
pitch-tuned feedback delay, then a one-pole loss in the loop models the
frequency-dependent damping of an air column. This is a deliberately compact
waveguide voice, not a calibrated simulation of a particular brass instrument.
It has no input audio, samples, external dependencies or reported latency.

| Control | Range | Default | Description |
|---|---:|---:|---|
| Pressure | 0-100% | 58% | Raises reed drive and feedback sustain. CC 1. |
| Embouchure | 0.8-1.2x | 1.0x | Moves the lip oscillator against the note-tuned bore. CC 2. |
| Bell | 0-100% | 68% | Opens the bore's feedback low-pass for a brighter tone. CC 3. |
| Breath | 0-100% | 12% | Adds filtered noise to the reed excitation. CC 4. |
| Reed Instability | 0-100% | 18% | Adds turbulent-flow noise and slow lip flutter. CC 5. |
| Register Capture | 0-100% | 0% | Raises the reed's resonance from the bore fundamental toward its fourth mode; the coupled bore may lock to an upper harmonic. CC 6. |
| Attack | 1-1000 ms | 12 ms | Rise time to near-full level. |
| Release | 20-4000 ms | 280 ms | Exponential release time. |
| Velocity | 0-100% | 65% | Sensitivity: zero ignores note velocity, full uses it proportionally. |
| Level | 0-100% | 65% | Output level. CC 7. |
| MIDI In | Off, 1-16, All | All | Channel filter; CC 120 stops all sound and CC 123 releases all notes. |

The interface sketches the air column so the signal path is explicit. The reed's
nonlinear pressure-flow relation is coupled to a damped delay waveguide; turbulent
air and lip flutter can destabilize the tone, while Register Capture biases the
reed resonance toward upper bore modes. These controls describe a physical
mechanism, not a calibrated prediction of when a real instrument will overblow.
The portable engine is in `include/` and `src/`; DPF is only the VST3 shell.

Core tests and the VST3 target build locally. Listening and host validation in a
DAW remain open.
