# rift

`rift` is an original `downspout` plugin rather than a `flues` port.

It is a transport-aware stereo buffer effect that captures the incoming audio
into a rolling memory window and, on rhythmic block boundaries, can:

- repeat a recent slice;
- reverse it;
- skip it;
- smear it at a slower replay rate;
- pitch-slip it at a different replay rate.

The goal is not total destruction by default. `rift` is meant to create
repeatable, groove-locked disruption that still feels musically attached to the
source material.

## Controls

- `Grid`
  Blocks per bar. The mutation engine only changes actions on these boundaries.
- `Density`
  How often a block mutates instead of passing through.
- `Damage`
  Bias toward more disruptive actions such as reverse, skip, and smear.
- `Memory`
  How many bars of past audio are eligible as slice source material.
- `Drift`
  How far the engine reaches through memory, and how unstable smear/slip reads
  become.
- `Pitch`
  Semitone offset used by the slip action.
- `Blend`
  Crossfade amount between the end of a slice loop and its next pass.
- `Mix`
  Wet level of the slice playback layer.
- `Hold`
  Freeze the current action and slice choice.
- `Scatter`
  Force the next few blocks into mutation.
- `Recover`
  Clear scatter pressure and force several dry blocks.

## UI

The UI is intentionally product-style rather than dev-style:

- one macro strip for the playable controls;
- large `Hold`, `Scatter`, and `Recover` buttons;
- a bottom `Modes` strip with quick parameter recipes such as `Stutter`,
  `Smear`, and `Ruin`;
- a preview lane that shows the next conceptual block pattern;
- an action-bias panel that makes the musical consequences of the macros legible.

## Transition handling

`rift` now smooths action changes with a short equal-power crossfade at block
boundaries. That keeps the rhythmic hard cuts, but avoids the worst clicks when
the read head jumps to a very different slice. The `Blend` control separately
crossfades each slice wrap so repeated loops can stay aggressive without
spitting a click at every restart.

## Verification status

`rift` has:

- a portable core with deterministic tests;
- text parameter state serialization;
- a first DPF-backed `rift.vst3` wrapper target with a custom UI.

The next step after build verification is real DAW feedback.
