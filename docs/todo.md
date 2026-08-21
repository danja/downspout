# Downspout TODO



bassops will be a new plugin featuring a ducker and mid/side eq. It will has 4 audio inputs, 2 outputs. The main signal will flow into inputs 1 and 2, a control signal into inputs 3 and 4. The control signal, which will typically be a bass drum send, will go to an envelope follower and  inverter, which will then feed into VCAs in the main signal path. After ducking the signal will be split into mid and side channels. Two matching linear phase filters will be applied : a LP to the mid signal and a HP to the side. There will be a control for the cutoff frequency. The mid and side will then be recombined into regular stereo and the signal output. 

can you add an extra processing block on the side signal before its high pass filter. This will be a function controlled by a slider that goes from direct linear transfer through non-linear soft clipping to hard clipping, to boost the harmonics on the side channels.

new plugin - clipper/soft distortion

check that all key operations in the UI are covered by MCP & vice versa

## Campione

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
