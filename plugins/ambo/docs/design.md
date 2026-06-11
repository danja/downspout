# Ambo Design Notes

`ambo` is an original Downspout stereo effect, not an LV2 port. The source
reference is therefore architectural rather than behavioral: preserve the repo's
portable-core/DPF-shell pattern used by `rift`, while taking visual cues from
`ground`'s structured dark panel layout.

## Module Mapping

- Time stretching maps to a modulated multi-tap granular smear. It is
  deterministic and bounded, but it is not yet a phase-vocoder.
- Spectral processing maps to frozen band energy, slow stereo motion, and soft
  high-band folding. This gives an ambient spectral-freeze feel without adding
  FFT dependencies.
- Tape simulation combines soft saturation, bias, head-bump color, slap, slow
  wow/flutter, and low-pass wear.
- Shimmer reverb is a stereo crossfed tank with modulated taps and a separate
  diffusion buffer for wider tails.
- Delay is a smeared ping-pong network with multiple taps and crossfed
  diffusion.
- Non-linear distortion uses tanh saturation, mild foldback, asymmetry, and
  internal DC rejection.
- Feedback is an explicit crossfed wet return into the start of the whole chain.

## UI Mapping

Only the selected route is shown as a block lane, so module controls are not
presented as duplicate controls across inactive chains. Module sliders are
reordered to match the active lane. The Field pane is an XY pad for performance
control: X maps to Mix and Y maps to Feedback.

## State Contract

State is serialized as newline-separated `key=value` text with `version=1`.
Unknown keys or malformed values reject the state block. Values are clamped by
the same processor ranges used by automation.

## Future Work

- Replace the spectral approximation with a plugin-local FFT implementation if
  Ambo needs bin-level freezing, masking, or formant movement.
- Add a modulation page once the base module chain has been validated in hosts.
- Add host-tempo delay divisions only after deciding how transport should affect
  existing free-running tails.
