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
| Attack | 1-1000 ms | 12 ms | Rise time to near-full level. |
| Release | 20-4000 ms | 280 ms | Exponential release time. |
| Velocity | 0-100% | 65% | Sensitivity: zero ignores note velocity, full uses it proportionally. |
| Level | 0-100% | 65% | Output level. CC 7. |
| MIDI In | Off, 1-16, All | All | Channel filter; CC 120 stops all sound and CC 123 releases all notes. |

The interface sketches the air column so the signal path is explicit: MIDI pitch
sets the delay, embouchure drives the reed, and Bell and Breath shape the result.
The portable engine is in `include/` and `src/`; DPF is only the VST3 shell.

Core tests and the VST3 target build locally. Listening and host validation in a
DAW remain open.
