# Downspout TODO

new plugin - bassproc - ducker and mid/side eq

Transmission
add parse to transmission console
add audio/midi clip - to loop

check that everything in UI works with MCP & vice versa

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
