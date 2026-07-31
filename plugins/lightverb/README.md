# Lightverb

Lightverb is a minimal stereo reverb for Transmission and the rest of the
Downspout suite. Its four-line feedback-delay network has a fixed, small DSP
workload and uses no convolution, FFT, modulation bank, or audio-thread
allocation. It favors useful space and predictable performance over a detailed
simulation of a natural room.

Use it directly after T-Mix, after Loopdelay, or on a stereo auxiliary return.
The default 20% wet mix suits an insert; choose 100% wet for a send. A typical
master path is `T-Mix → Loopdelay → Lightverb → Guardian`.

All normal parameters are host-automatable. Producer MIDI uses CC 32 for Wet
mix and CC 33 for Space on any MIDI channel. Incoming CC temporarily takes over
the saved panel value; **Release MIDI** restores the manual settings. CC 19
optionally gates ownership of both controls. A selectable control channel
isolates parallel chains, and transparent MIDI-through keeps later processors
reachable from the same producer send.

See [Producer Control Bus v1](../../docs/producer-control-bus.md).
