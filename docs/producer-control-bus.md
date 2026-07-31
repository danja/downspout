# Producer Control Bus v1

The Producer Control Bus is a loose MIDI CC contract. Senders and receivers do
not link against each other, share project state, or require a particular host.
Every receiver remains usable through its panel and ordinary host automation.

## Default addresses

| CC | Meaning | Receiver |
| ---: | --- | --- |
| 19 | Acquire at 127; release at 0 | All bus receivers |
| 20–27 | Normalized channel gain overlays | T-Mix channels 1–8 |
| 30 | Delay time or synchronized-length selection | Loopdelay |
| 31 | Delay feedback | Loopdelay |
| 32 | Reverb wet mix | Lightverb |
| 33 | Reverb space | Lightverb |

Payload values are absolute 0–127 values. Receivers own smoothing, range
mapping, and safety limits. Producer values are transient overlays and never
replace the receiver's saved manual setting.

## Ownership and compatibility

Mixgen sends CC 19 value 127 before its first payload. Stopping or disabling
the producer sends CC 19 value 0 and unity values for T-Mix CC 20–27. Changing
the output channel releases the old channel before acquiring the new one.

Each receiver has a Control channel setting: 0 is Omni and 1–16 selects one
MIDI channel. **Require CC 19 gate** rejects payload until ownership has been
acquired. It defaults off, preserving direct control from older projects and
ordinary hardware. The receiver's Release action always returns to saved
manual values.

T-Mix, Loopdelay, and Lightverb expose MIDI output and forward incoming events
unchanged. This permits one stream to travel through a serial effect chain in
hosts that model MIDI as a plugin-to-plugin pipeline.

## Mixgen routing

Mixgen provides T-Mix, FX-only, and Full-bus profiles. Its four effect macros
default to CC 30–33. Each macro chooses one of the eight musical pattern lanes,
a destination CC, minimum and maximum normalized values, and optional
inversion. These are data-driven mappings: changing a destination allows the
same producer to control a third-party effect without changing its engine.

The default profile remains T-Mix-only for existing projects. Choose Full bus
to emit CC 20–33 together. Inverted default FX lanes make delay and reverb rise
when their source mix lane recedes.
