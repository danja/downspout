# Downspout TODO

The following tests FAILED:
          3 - downspout_bassgen_core_tests (Subprocess aborted)
         10 - downspout_mixgen_core_tests (Subprocess aborted)
         11 - downspout_loopdelay_core_tests (Subprocess aborted)
         13 - downspout_melgen_core_tests (Subprocess aborted)
         14 - downspout_rift_core_tests (Subprocess aborted)
         17 - downspout_drumgen_core_tests (Subprocess aborted)
         18 - downspout_drumkit_core_tests (Subprocess aborted)
         20 - downspout_cadence_core_tests (Subprocess aborted)
         45 - downspout_guardian_core_tests (Subprocess aborted)
         48 - downspout_campione_core_tests (Subprocess aborted)

## Campione

The drum sound recognition part of the system doesn't find any matches when tested on samples lifted from a drum loop 

~/Music/samples/loops/Drum\ Loops/KSMB1_0_SandmanBreak_Original_CD.wav

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
