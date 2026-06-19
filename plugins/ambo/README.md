# Ambo

`ambo` is a stereo ambient effect built around a small chain of host-neutral DSP
modules. It follows the same broad shape as `rift`: a portable core, a thin DPF
wrapper, text state serialization, a custom NanoVG UI, and deterministic core
tests.

The current processor is intentionally dependency-free. The time and spectral
modules are musical approximations rather than a phase-vocoder or FFT engine:
the time module uses modulated multi-tap granular smear, and the spectral module
uses frozen band energy, cross-channel motion, and soft high-band folding. That
keeps the first version portable while leaving room for a true spectral backend
later.

## Signal Flow

The chain can be rearranged with the `Chain` selector:

- `Drift`: Time -> Spectral -> Tape -> Shimmer -> Delay -> Drive
- `Bloom`: Tape -> Time -> Shimmer -> Spectral -> Delay -> Drive
- `Haze`: Spectral -> Shimmer -> Time -> Delay -> Tape -> Drive
- `Fracture`: Drive -> Time -> Spectral -> Delay -> Shimmer -> Tape

The `Feedback` control feeds the previous wet output back into the start of the
chain with a stereo crossfeed. Delay and shimmer also have internal feedback, so
high settings can become dense quickly.

## Controls

- `Chain`: one of the four module orders above.
- `Time`: modulated multi-tap granular smear and slow read instability.
- `Spectral`: frozen-band smear, stereo motion, and softened partial movement.
- `Tape`: saturation, head-bump color, slap, low-pass wear, wow, and flutter.
- `Shimmer`: bright crossfed ambient tank with modulated taps and diffusion.
- `Delay`: ping-pong delay send, smeared repeats, and crossfed diffusion.
- `Drive`: non-linear tanh/fold saturation with mild asymmetry.
- `Feedback`: wet output returned to the chain input.
- `Mix`: dry/wet balance.
- `Output`: final gain trim in dB.

The UI shows the selected chain as one active block lane. The module sliders are
ordered to match that lane, followed by Feedback, Mix, and Output. The Field pane
is an XY control: left-to-right adjusts Mix, and bottom-to-top adjusts Feedback.

## Build Notes

`plugins/ambo` is wired into the root CMake build, local install script, and
release packaging as `ambo.vst3`.
