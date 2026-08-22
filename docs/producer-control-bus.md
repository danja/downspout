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
| 34 | Duck depth overlay | Bassops (MIDI input not yet enabled) |
| 35 | Wet/dry blend | Bassops (MIDI input not yet enabled) |

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

## Audio-sidechain receivers

Not all bus-adjacent plugins use MIDI CC as their primary control input.
**Bassops** accepts a dedicated audio sidechain on inputs 3–4 (typically a dry
kick-drum send): an internal envelope follower drives a VCA over the main signal
on inputs 1–2. Its core ducking behaviour therefore works without any bus
connection.

However, Bassops parameters can be overlaid by the bus once MIDI input is
enabled (requires `DISTRHO_PLUGIN_WANT_MIDI_INPUT 1` in the build):

| CC | Parameter | Effect |
| ---: | --- | --- |
| 34 | Duck Depth | Deepen or soften the sidechain duck in real time |
| 35 | Wet | Blend the processed signal in or out |

Adding Bassops after a T-Mix or Loopdelay in the signal chain keeps the CC 19
gate and audio MIDI-through working as usual.

## Vocabulary

Profile files (`.ttl`) use the `trn:` prefix to describe bus participation.
The following terms are in use across plugin profiles:

| Term | Kind | Meaning |
| --- | --- | --- |
| `trn:ControlMidi` | Port type | MIDI input/output carrying CC bus messages |
| `trn:AudioSidechain` | Port type | Audio input used as an amplitude control source via an envelope follower, not audio-to-process |
| `trn:BusProducer` | `trn:busRole` value | Plugin emits CC on the bus (e.g. Mixgen) |
| `trn:BusReceiver` | `trn:busRole` value | Plugin accepts CC parameter overlays from a bus producer |
| `trn:ccMapping` | Property | Declares a CC slot produced or consumed by the plugin |
| `trn:ccGate` | Boolean flag on `trn:ccMapping` | Marks the ownership-gate CC (19) as distinct from parameter-overlay CCs |
| `trn:feedsControls` | Property | Producer side: names specific downstream receiver plugins |

`trn:AudioSidechain` and `trn:BusProducer`/`trn:BusReceiver` are extensions
introduced to distinguish audio-envelope control from CC control, and to make
bus membership machine-readable rather than inferred from ccMapping entries
alone.
