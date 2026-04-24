# gremlin-driver audit

## Source

- LV2 source: `/home/danny/github/flues/lv2/gremlin-driver`
- main shell: `src/gremlin_driver_plugin.cpp`
- core modulation engine: `src/GremlinDriverEngine.hpp`

## Portable core identified

The existing engine already covered:

- four modulation lanes
- two probabilistic trigger lanes
- transport/free-clock behavior
- deterministic lane and trigger state updates

The LV2 shell mostly handled:

- transport parsing
- MIDI pass-through and emission
- one-shot randomisation burst generation
- status-port publishing

Those shell concerns were re-hosted into a portable `gremlin_driver_processor`
layer plus a thin DPF wrapper.

## Main VST3 assumptions

- keep the plugin MIDI-only with no audio ports
- preserve pass-through behavior
- emit Gremlin-facing CCs and note triggers on the same MIDI output stream
