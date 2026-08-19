# Downspout TODO


arabic beats in drumgen

plugins/drumgen needs a new piece of functionality : pattern load & save. The patterns will be templates that act as a starting point, their behaviour will be modified by the slider parameters. This should complement the existing Genre and Style selectors somehow. I don't know what format would be appropriate for the patterns - would .mid snippets be appropriate? The kind of patterns we need to have available can be seen in docs/reference/Doumbek_Rhythm_Cheat_Sheet.pdf these need encoding into whatever format we decide on. The UI should have some kind of preview functionality to listen to patterns before loading.

we need some Latin clave patterns, plus any others you think  might be useful, like classic drum machine patterns

new plugin - bassproc - ducker and mid/side eq

new plugin - clipper/soft distortion

check that all key operations in the UI are covered by MCP & vice versa

## Campione

add Pan to zone edit

maybe - FFT analyse beat slices to determine drum instrument

## Screenshots

Floozy UI changed (Voices slider added, dynamic header). Refresh screenshot:

```
scripts/capture-plugin-screenshots.sh floozy
```

## Floozy

~~It is very resource-hungry. Examine for potential ways of making it lighter without removing functionality. Make the number of voices selectable 1-8, default 4.~~ Done.
the planned Ground integration) currently
hardcode Conductor's default CC numbers (20–24). If the user changes Conductor's CC
assignments, the generators will not follow. Consider exposing CC number parameters
on the receiver side, or adding a note to the UI that CC numbers must match
Conductor's settings.

### Pending — Conductor CC channel collision note

The BassGen "MIDI Follow" system (Note On response for drum triggering) and the
"Conductor" system (CC response) operate on different MIDI message types, so they
can coexist on the same channel. However, the UI should make this clearer — consider
a tooltip or panel note explaining that Follow Ch and Conductor Ch serve different
purposes and can differ.
