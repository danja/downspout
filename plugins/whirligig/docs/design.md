# Whirligig design notes

## Provenance
Clean-room from the geometry of a rotating directional source. The JSFX `Dizzy` (b_whirl) port in `DSP-Ports-JSFX` is GPL and was
used only to confirm what a rotary effect has to do; no code or constants were taken from it. Radii, lobe shapes and the speed table are
typical values chosen by ear, not measurements of a particular cabinet.

## Signal flow
mono sum -> LR4 crossover (two cascaded Butterworth biquads per band) -> drum (low) and horn (high) rotors -> two microphones -> L and R,
plus a delay-matched dry path for Mix.

## Rotor model
- Speed: exponential approach to the target rate, time constant Accel (horn quick, drum slow).
- Amplitude: horn gain `1 - d + d * lobe^2`, drum gain `1 - d + d * lobe`, where `lobe` is 1 facing the microphone, 0 facing away.
- Doppler: delay `base - swing * cos(theta - mic)`, `swing = radius / c * amount`, read by a 4-point Hermite interpolator. The base delay is an
  integer equal to the dry delay, so with Doppler off wet and dry are aligned and the response is flat (a fractional base lost ~1 dB at 12 kHz).
- Microphones sit at +-Mic Angle/2 around the front.

## Beat sync
- Target rate is `bpm / 60 / beatsPerTurn`. Beats-per-turn comes from a fixed 12-entry table (append-only).
- Once a rotor is within 2 % of the target rate, `turn += wrapped_error * k` pulls the angle to `lockedTurn(beat, bpt, phase)` with a 0.5 s
  time constant; before that it only changes speed. So a relocate or loop glides back into phase, never jumps.
- `Locked` is true while both rotors are in phase. Stopped or invalid transport: rotors keep their rate at the fallback tempo and free-run.
- Beat position from DPF BBT: `(bar-1)*beatsPerBar + (beat-1) + tick/ticksPerBeat`.

## Assumptions (LV2 -> DPF/VST3)
- State: all settings are ordinary parameters (no `WANT_STATE`). CC takeover of Speed is runtime only and released when the host or panel sets Speed.
- Activation and sample-rate changes clear the running state (rotors at angle 0), never the settings.
- One MIDI input; CC 1 and CC 2 on the chosen channel.

## Not built
Cabinet resonance, tube overdrive, horn/drum reflections, stereo input handling (input is summed), latency reporting.

## Tests
`tests/whirligig_core_tests.cpp`, 104 checks: flat response, dry path, band levels, AM depth, lobe shapes, Doppler against the formula, speed
changes and stop, microphones, controllers, determinism and block-size independence, idle and stability, meters and reset, beat rates,
lock convergence and smoothness, pinned parameter ids. Mutation-checked.
