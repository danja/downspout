# t-mix implementation notes

`t-mix` is an original plugin rather than an LV2 port.

## Audio and host mapping

- DPF exposes eight separately named mono input buses and one stereo output bus.
- Inputs are summed without automatic normalization; the master fader is the
  final gain stage.
- Channel and master levels range from `-60 dB` to `+12 dB`. The lower endpoint
  is treated as silence.
- Pan ranges from `-1` (left) to `+1` (right) and uses constant-power sine/cosine
  gains, so a centered mono signal is attenuated by approximately `3 dB` in
  each output.
- Mute always silences its channel. If one or more Solo buttons are active,
  only soloed channels that are not muted reach the mix.
- Input meters are pre-fader absolute peaks with a 300 ms release. They remain
  informative while a strip is muted and are exposed as read-only output
  parameters for the UI.

## Producer control

- MIDI CC 20-27 map to channels 1-8, on any MIDI channel. Values map linearly
  from silence at 0 to unity at 127.
- Producer gain multiplies the saved channel fader rather than replacing it.
  Manual balance and host automation therefore remain the upper-level mix.
- Incoming events are applied at their sample offset. `Producer Slew` smooths
  changes from 0 to 500 ms and defaults to 25 ms.
- Effective producer gains are transient read-only output parameters. They are
  visible to the UI but are not serialized.
- CC 19 value 127 acquires producer ownership and value 0 releases it, restoring
  unity producer targets. `Require Producer Gate` defaults off for compatibility.
- Control channel 0 is Omni; 1–16 filters the complete producer contract.
- The DPF wrapper forwards all input MIDI unchanged for downstream effects.

## State

The `parameters` state key stores a versioned text representation of all eight
levels, pans, mute/solo values, the master level, and producer slew. Meter and
effective producer-gain values are transient and are not serialized. Version 1
and 2 states load with the default Omni channel and optional gate disabled.
