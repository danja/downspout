# Downspout TODO

All 10 previously-failing tests now pass. Pre-existing unrelated failure:
         29 - downspout_basilico_core_tests (Failed)

## Campione

The drum sound recognition part of plugins/campione doesn't find any matches when tested on samples lifted from a drum loop 

~/Music/samples/loops/Drum\ Loops/KSMB1_0_SandmanBreak_Original_CD.wav

Create a helper system for Campione in JS using third_party/freesound-js to obtain drum samples to run the sound recognition algorithms over. You can find the API key in .env (Keep this secret!).


Ensure the operations supported by the UI are also covered by MCP, and vice versa.

## Rift

Allow control from Drift

## Gremlin

Simplify UI, make sounds more varied.

## Lightverb

Remove the SIGNAL FLOW block - is it only labels?
Think about lightweight additions: spatial? Presets?

## Floozy

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

## Newer plugins needing evaluation

* ambo
* arpgen
* conductor
* drift
* guardian
* harmonic-atlas
* mnemosyne
* mosaic
* oracle
* orbit
* polymeter
* resonance-garden
* tuney-vst
