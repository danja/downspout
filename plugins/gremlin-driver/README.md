# gremlin-driver

`gremlin-driver` is the `downspout` VST3 port of the `flues` LV2 MIDI utility
that drives `gremlin` with tempo-aware macro modulation, action-note triggers,
and one-shot patch randomisation bursts.

## Current VST3 shape

- MIDI in / MIDI out utility plugin
- `Pass Input` switch controls whether incoming MIDI is forwarded unchanged
- emits Gremlin-compatible macro/master CCs and action notes
- custom UI for four continuous lanes and two trigger lanes
- host-following clock by default, with explicit manual BPM mode when needed
- lane shapes include sine, triangle, ramp, reverse ramp, pulse, exponential,
  sample-and-hold, random walk, and logistic motion

## Typical chain

```text
[sequencer or keyboard] -> [gremlin_driver.vst3] -> [gremlin.vst3]
```

That lets notes flow through while the driver adds controller movement and
discrete Gremlin actions. Leave `Pass Input` on for that normal chain, or turn
it off when the driver should emit only its own CCs and action notes.

## Host compatibility

The DPF/VST3 wrapper exposes a silent stereo output bus for compatibility with
hosts that reject event-only plugins with no audio outputs. The portable core
remains MIDI-only, and the wrapper clears both output channels every block.
