# gremlin

`gremlin` is the `downspout` VST3 port of the `flues` LV2 malfunction
instrument. It keeps the source engine as a portable core, wraps it in DPF, and
exposes a custom performance UI instead of relying on the original X11 panel.

## Current VST3 shape

- synth-style VST3 with stereo audio output
- one MIDI input that accepts both note performance and controller gestures
- custom UI for scenes, actions, macros, live controls, and momentary holds
- no custom saved-state layer yet beyond normal host parameter persistence

## Practical test note

`gremlin` is a note-driven instrument, not an audio insert effect. Silence with
no MIDI input is expected.

For a first DAW sanity check:

- put it on an instrument track;
- send it ordinary MIDI notes;
- avoid using the very low controller-note range as the musical test range.

The lowest mapped MIDImix/controller note IDs are reserved for performance
button gestures rather than pitch, so a normal musical test should start above
that range.

## Important mapping decision

The LV2 plugin had separate `midi_in` and `controller_in` ports plus controller
LED feedback. The VST3 port intentionally collapses that into a single MIDI
input path. The processor differentiates notes, CCs, and controller-button
notes internally.

That keeps the host-facing wrapper simple enough for mainstream DAWs while
preserving the practical MIDImix-style behavior.
