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

The selected chain is an active block lane. Each block is its module's vertical
control, so the adjustable controls and signal order are the same left-to-right
sequence. Feedback, Mix, and Output remain conventional sliders. The explicitly
labelled Mix/Feedback XY pane is an alternate performance control: left-to-right
adjusts Mix, and bottom-to-top adjusts Feedback. Control-click any module block
or slider to restore its default; Control-click the XY pane restores both Mix and
Feedback.

Continuous controls are smoothed in the processor. Chain changes use a short
dry-signal transition while the module topology switches, and host bypass uses
DPF's designated bypass parameter with the same click-safe smoothing.

## Build Notes

`plugins/ambo` is wired into the root CMake build, local install script, and
release packaging as `ambo.vst3`.
