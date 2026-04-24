# e-mix extraction plan

## Phase 1: portable core

Extract the LV2 shell into a small portable library:

- `e_mix_core_types.hpp`
- `e_mix_engine.hpp/.cpp`
- `e_mix_serialization.hpp/.cpp`

This layer owns:

- parameter clamping;
- Euclidean block activation;
- transport start/restart handling;
- fallback transport clock behavior;
- per-sample gain calculation;
- text state serialization.

## Phase 2: wrapper

Wrap the core in a thin DPF VST3 effect:

- map `TimePosition` into the portable transport snapshot;
- expose five automatable integer parameters;
- serialize state through one text key;
- present stereo input/output while keeping the core reusable for more
  channels.

## Phase 3: UI

Design a UI around readability:

- show the full cycle as a block strip;
- show the fade envelope inside one active block;
- surface derived timing values like bars per block and active ratio;
- keep numeric edits possible without reproducing the confusing LV2 layout.
