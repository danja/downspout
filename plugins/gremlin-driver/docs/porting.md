# gremlin-driver porting notes

## UI direction

The first VST3 UI is intentionally lane-oriented rather than trying to imitate a
generic host parameter list:

- global clock and BPM controls
- four modulation lane cards
- two trigger cards
- live status meters for lane output, trigger flashes, transport, and BPM

## MIDI behavior

- incoming MIDI is passed through
- active lanes emit Gremlin macro/master CC updates
- trigger lanes emit Gremlin action notes
- `Randomise` emits a one-shot burst of direct Gremlin patch CCs

## Current known gap

The port currently relies on host parameter persistence only. There is no custom
state/version layer yet.
