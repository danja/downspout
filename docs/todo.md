# Downspout TODO

To all plugins which offer a choice of scales, add Ionian, Neopolitan Major (NeoMajor) and Neopolitan Minor (NeoMinor)

## Producer Control Bus

Read /home/danny/github/downspout/docs/producer-control-bus.md and see what might need tweaking/implementing in the plugins to make this more intuitive and useful in /home/danny/github/transmission

## Campione

~~The drum sound recognition part of plugins/campione doesn't find any matches when tested on samples lifted from a drum loop~~ Fixed: three-part acoustic fix + loop-slicing mode.

- Kick vs Low Floor Tom: added high-salience/low-flatness bonus to kick scorer; added matching penalty to tom scorer (very high dp_sal + low flatness + dp_freq < 110 Hz = kick, not tom)
- Snare vs Low-Mid Tom: added room/bleed snare rule (dp_freq 150–400 Hz + high_end > 0.25 + sub_bass < 0.08 + eff_dur < 0.60s); added high_end penalty to tom scorer (high_end > 0.20 indicates noise, not a closed-body tom)
- Loop-slicing mode: when n > 16 zones, use argmax instead of Hungarian so multiple slices can share the same GM note. Result on Sandman Break: 55/56 slices assigned (was 10/56); kicks → MIDI 36, snares → MIDI 38, cymbals → MIDI 49.

~~Create a helper system for Campione in JS using third_party/freesound-js to obtain drum samples to run the sound recognition algorithms over.~~ Done: `scripts/campione-freesound-helper.js`.

~~Ensure the operations supported by the UI are also covered by MCP, and vice versa.~~ Done: added `import_wavetable`, `reorder_zone`, `preview_zone` to MCP; added "Clear All" to UI context menu; registered all state keys in `initState()`.

## Rift

Can you set up skream so that with default settings here and in plugins/drift,  the CCs will work. Also allow control of plugins/rift from plugins/drift - it should just work with default CCs.

## Gremlin

Simplify UI, make sounds more varied.

## Lightverb

Remove the SIGNAL FLOW block - if it is only labels
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
