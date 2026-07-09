# gremlin-driver porting notes

## UI direction

The first VST3 UI is intentionally lane-oriented rather than trying to imitate a
generic host parameter list:

- global clock and BPM controls
- four modulation lane cards
- two trigger cards
- live status meters for lane output, trigger flashes, transport, and BPM

## MIDI behavior

- incoming MIDI is passed through by default, controlled by the `Pass Input`
  parameter
- active lanes emit Gremlin macro/master CC updates
- trigger lanes emit Gremlin action notes
- `Randomise` emits a one-shot burst of direct Gremlin patch CCs

Default lanes are biased toward Gremlin's newer musical/extreme behavior:
pitch motion, breakage pressure, space movement, and stutter pulses. The second
trigger now defaults to `Rand All`, and direct patch randomisation uses wider
ranges for damage, fold, feedback, stutter, pitch spread, cross feedback,
glitch length, and chaos rate so it can reach Gremlin's catastrophe layer
instead of staying in the older midrange-safe zone.

## Current known gap

The port currently relies on host parameter persistence only. There is no custom
state/version layer yet.
