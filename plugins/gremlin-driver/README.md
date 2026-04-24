# gremlin-driver

`gremlin-driver` is the `downspout` VST3 port of the `flues` LV2 MIDI utility
that drives `gremlin` with tempo-aware macro modulation, action-note triggers,
and one-shot patch randomisation bursts.

## Current VST3 shape

- MIDI in / MIDI out utility plugin
- passes incoming MIDI through unchanged
- emits Gremlin-compatible macro/master CCs and action notes
- custom UI for four continuous lanes and two trigger lanes
- host-following clock by default, with explicit manual BPM mode when needed

## Typical chain

```text
[sequencer or keyboard] -> [gremlin_driver.vst3] -> [gremlin.vst3]
```

That lets notes flow through while the driver adds controller movement and
discrete Gremlin actions.
