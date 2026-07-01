# rift

`rift` is an original `downspout` plugin rather than a `flues` port.

It is a transport-aware stereo buffer effect that captures source audio into a
rolling memory window and, on rhythmic block boundaries, can:

- repeat a recent slice;
- reverse it;
- skip it;
- smear it at a slower replay rate;
- pitch-slip it at a different replay rate.

The goal is not total destruction by default. `rift` is meant to create
repeatable, groove-locked disruption that still feels musically attached to the
source material.

The source can be live input, a loaded WAV loop, or both. Sample playback is
beat-mapped to the host transport before it enters the same rolling-buffer
engine, so a break can be treated as a four-beat phrase even when the raw file
duration does not match the current DAW tempo.

`rift` also has a 16-cell sequence lane for repeatable edits. Pick a recipe in
the UI, then click cells to store it. For example, select `2 beat` and click a
cell to make that position hold a two-beat stutter across the grid. With the
factory default `Density = 0`, an empty sequence is dry/pass-through. Manual
sequence cells can be added from silence. Raising `Density` above zero enables
the older probabilistic behavior when the sequence is empty. As soon as any
cell is enabled, empty cells are dry/pass and enabled cells force their stored
recipe.

## Controls

- `Grid`
  Blocks per bar. The mutation engine only changes actions on these boundaries.
- `Density`
  How often an empty-sequence block mutates instead of passing through. The
  default is `0` so a fresh instance does not process until cells are added or
  density is raised.
- `Damage`
  Bias toward more disruptive actions such as reverse, skip, and smear.
- `Chop`
  Shortens the repeated source fragment inside each grid block. Raise it for
  tighter beat chops, fast stutters, and sample retriggers without changing the
  main block grid.
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
- `Source`
  Selects `Live`, `Sample`, or `Live + Sample` input. `Sample` uses the loaded
  WAV file, falling back to a built-in four-beat test loop when no file is
  loaded.
- `Load WAV`
  Opens a file picker and stores the selected sample path in plugin state.
- `Sample Beats`
  Host-visible parameter that declares how many beats the loaded sample spans.
  The default is `4`, which is the intended breakbeat-loop case.
- `Hold`
  Freeze the current action and slice choice.
- `Scatter`
  Force the next few blocks into mutation.
- `Recover`
  Clear scatter pressure and force several dry blocks.
- `Sequence`
  Sixteen saved cells that override the random action chooser when any cell is
  enabled. Cell recipes include `Ratchet`, `1/2 beat`, `1 beat`, `2 beat`,
  `Reverse`, `Smear`, and `Slip`.

## UI

The UI is intentionally product-style rather than dev-style:

- one macro strip for the playable controls;
- large `Hold`, `Scatter`, and `Recover` buttons;
- a `Source` selector and `Load WAV` button for sample-backed processing;
- a bottom `Modes` strip with quick parameter recipes such as `Stutter`,
  `Smear`, and `Ruin`;
- a sequence lane where cells store repeatable stutter/replay recipes;
- an action-bias panel that makes the musical consequences of the macros legible.

## Sample loading

Current sample loading is deliberately conservative. `rift` accepts ordinary
RIFF/WAVE PCM files and 32-bit float WAV files. Compressed WAV, AIFF, FLAC, and
MP3 are not supported yet.

Loaded samples are decoded outside the audio callback and then published to the
processor as immutable PCM data. If a selected file cannot be loaded, `Sample`
mode falls back to the built-in test loop rather than breaking the plugin.

Transient detection is not implemented yet. The current sample path maps the
whole file evenly across the declared beat length, then lets the existing
`rift` mutation engine choose and process rhythmic blocks. The `Chop` control
can still make loaded breaks stutter by shortening the repeated fragment inside
each selected block.

## Transition handling

`rift` now smooths action changes with a short equal-power crossfade at block
boundaries. That keeps the rhythmic hard cuts, but avoids the worst clicks when
the read head jumps to a very different slice. The `Blend` control separately
crossfades each slice wrap so repeated loops can stay aggressive without
spitting a click at every restart.

## Verification status

`rift` has:

- a portable core with deterministic tests;
- WAV sample-source loading and beat-mapped sample playback;
- saved 16-cell sequence state for repeatable stutter edits;
- text parameter state serialization;
- a DPF-backed `rift.vst3` wrapper target with a custom UI and file browser.

The next major sample-mode step is transient-aware chop-point detection after
more DAW testing with real breaks.
