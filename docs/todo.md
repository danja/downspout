# Downspout TODO

## Producer Control Bus

~~Read /home/danny/github/downspout/docs/producer-control-bus.md and see what might need tweaking/implementing in the plugins to make this more intuitive and useful in /home/danny/github/transmission~~

Assessment: contract is well-defined. Remaining gaps:
- ~~Bassops CC 34/35 overlay not active~~ Done: `DISTRHO_PLUGIN_WANT_MIDI_INPUT 1` added, `handleMidi()`, `effectiveDuckDepth()`, `effectiveWet()` wired in engine, bus section added to UI, `trn:ControlMidi` added to profile.ttl
- ~~Transmission should be able to read bus participation from each plugin's `profile.ttl` using `trn:busRole`, `trn:ccMapping`, and `trn:ccGate` — verify these are populated correctly in all bus-participant profiles (Mixgen, T-Mix, Loopdelay, Lightverb, Bassops)~~ Done: all five profiles now have `trn:busRole` and `trn:ccMapping` entries.
- ~~Profile TTL gaps: add `trn:busRole trn:BusProducer` to Mixgen; add `trn:busRole trn:BusReceiver` + CC 20-27 mappings to T-Mix; add `trn:busRole trn:BusReceiver` to Loopdelay and Lightverb~~ Done.

## Conductor

~~Determine the plugins that can be influenced by Conductor, check to see if this list can be extended in line with docs/producer-control-bus.md~~

Current Conductor receivers (CC 20–24, configurable channel): **BassGen** (CC 21–24), **DrumGen** (CC 21–24), **Ground** (CC 20–24 including scene→arc tension).

Best extension candidates: ~~**Harmonic Atlas**~~ Done: CC 20→style, CC 22→tension, CC 23→inversion range, CC 24→reset; "Conductor ch" selector added to INPUT & ROUTING panel; profile.ttl updated with `trn:busRole trn:BusReceiver` and CC mappings. **MelGen** (only melodic generator without Conductor support — needs `DISTRHO_PLUGIN_WANT_MIDI_INPUT` + CC handlers).

DrumGen now has section label "Conductor · CC 21–24 from Mixgen" to match BassGen.

## Campione

~~What does the "+ auto -" control do?~~ It's the slice count spinner next to the "Slice ▸" button. Value 0 = "auto" (transient detection), 1–64 = explicit equal-duration slices. Displays "auto" when 0.

~~Make the row of controls starting with Normalise more visibly noticeable with color, and have the buttons blink when clicked. Add a Random button to this row which will reorder the zones randomly.~~ Done: Normalize/Trim/Fade/Reverse/Shuffle buttons now use teal accent color, brighten for 200ms on click (flash state + animation loop), and Shuffle sends `kStateKeyZoneShuffle` → `doZoneShuffle()` (Fisher-Yates, preserves keyboard range assignments).

~~High priority : Campione still forgets its zones.~~ Fixed: root cause was `kStateKeyZoneClear` and `kStateKeyMapDrum` handlers lacked empty-value guards. With `DISTRHO_PLUGIN_WANT_FULL_STATE`, REAPER calls `setState("zone_clear", "")` on reload (for every registered key), which cleared zones after `kStateKeyZones` had already restored them. Fix: added `&& value && value[0] != '\0'` guards in `CampionePlugin.cpp` and changed UI triggers to send `"1"` instead of `""`.

~~The drum sound recognition part of plugins/campione doesn't find any matches when tested on samples lifted from a drum loop~~ Fixed: three-part acoustic fix + loop-slicing mode.

- Kick vs Low Floor Tom: added high-salience/low-flatness bonus to kick scorer; added matching penalty to tom scorer (very high dp_sal + low flatness + dp_freq < 110 Hz = kick, not tom)
- Snare vs Low-Mid Tom: added room/bleed snare rule (dp_freq 150–400 Hz + high_end > 0.25 + sub_bass < 0.08 + eff_dur < 0.60s); added high_end penalty to tom scorer (high_end > 0.20 indicates noise, not a closed-body tom)
- Loop-slicing mode: when n > 16 zones, use argmax instead of Hungarian so multiple slices can share the same GM note. Result on Sandman Break: 55/56 slices assigned (was 10/56); kicks → MIDI 36, snares → MIDI 38, cymbals → MIDI 49.

~~Create a helper system for Campione in JS using third_party/freesound-js to obtain drum samples to run the sound recognition algorithms over.~~ Done: `scripts/campione-freesound-helper.js`.

~~Ensure the operations supported by the UI are also covered by MCP, and vice versa.~~ Done: added `import_wavetable`, `reorder_zone`, `preview_zone` to MCP; added "Clear All" to UI context menu; registered all state keys in `initState()`.

## Gremlin

Simplify UI, make sounds more varied.

## Lightverb

~~Remove the SIGNAL FLOW block - if it is only labels~~ Done: removed block, shifted remaining sections up 82px, reduced window height 650→568.
Think about lightweight additions: spatial? Presets?

## Floozy

Floozy has no Conductor integration yet (physical modelling synth). If added, avoid hardcoding CC numbers — expose them as parameters or document they must match Conductor's panel settings (default CC 20–24).

~~BassGen and DrumGen hardcode Conductor's CC numbers (21–24). Consider exposing CC number parameters, or at minimum label the UI section clearly.~~ Done: BassGen has "MIDI Follow · note-on triggers" and "Conductor · CC 20–24 from Mixgen" labels; DrumGen now has "Conductor · CC 21–24 from Mixgen" label added.

## Evaluate Manually in Reaper

* ambo
* arpgen
* conductor
* drift
* harmonic-atlas
* mnemosyne
* mosaic
* oracle,
* orbit
* polymeter
* resonance-garden
* tuney-vst.
