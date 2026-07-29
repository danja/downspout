# t-mix

`t-mix` is an original eight-input, stereo-output audio mixer. Each mono input
strip has a pre-fader peak meter, level fader, constant-power pan control, Mute,
and Solo. A single master fader controls the stereo output.

The VST3 wrapper exposes eight named mono input buses and one stereo output bus.
The portable core owns gain, pan, mute/solo, summing, and meter behavior; the
DPF layer only maps ports, parameters, state, and the custom NanoVG UI.

See [docs/porting.md](docs/porting.md) for parameter and host-mapping details.
