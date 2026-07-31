# t-mix

`t-mix` is an original eight-input, stereo-output audio mixer. Each mono input
strip has a pre-fader peak meter, level fader, constant-power pan control, Mute,
and Solo. A single master fader controls the stereo output.

MIDI CC 20-27 provide click-smoothed producer gain control for channels 1-8.
The incoming value is a transient multiplier over each saved channel fader, so
`mixgen` can arrange the mix without overwriting the engineer's base balance.
CC 19 provides optional producer ownership/release. Control channel 0 accepts
all MIDI channels; 1–16 isolates a particular mix chain. Incoming MIDI is
forwarded unchanged to downstream effects.

The VST3 wrapper exposes eight named mono input buses and one stereo output bus.
The portable core owns gain, producer control, pan, mute/solo, summing, and meter behavior; the
DPF layer only maps ports, parameters, state, and the custom NanoVG UI.

See [docs/porting.md](docs/porting.md) for parameter and host-mapping details.
The cross-plugin contract is documented in
[Producer Control Bus v1](../../docs/producer-control-bus.md).
