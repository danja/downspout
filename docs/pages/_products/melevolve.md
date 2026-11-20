---
title: Melevolve
order: 211
bundle: melevolve.vst3
kind: MIDI generator
category: generative
group: generators
role: evolutionary melody generator
screenshot: /assets/plugins/melevolve.png
capabilities: [MIDI output, internal clock, MIDI Clock input, MIDI Clock output, host BBT sync, persistent population state]
summary: Melogen's five-row genetic melody generator as a live MIDI plugin: play, vote −/0/+, and breed the next generation on internal, MIDI Clock, or host timing.
---

> **Ported from [Melogen](https://github.com/danja/melogen)**. The genetic algorithm, local scale
> set and light four-tab layout come from the browser app. MIDI output, native controls and clock
> handling are original to this port.

## Opinion

Melevolve is for the moment when a plausible melodic idea matters more than a finished one. It
does not try to write a tune in one shot: it offers five short lines, asks for a quick reaction,
and uses those reactions to breed the next set. The workflow is deliberately closer to auditioning
takes than to editing notes.

The plugin emits MIDI only and makes no sound. Put it before an instrument in a MIDI-capable host,
or route its MIDI output to an instrument track. Its panel keeps Melogen's pale background, white
row cards, round green Play buttons and − / 0 / + votes, so the five-row interaction is readable at
a glance.

## Functionality

### Interact

The **Interact** tab shows a generation and round counter, then five rows. Each row has a round
Play button, a three-way vote, an `OUT` badge while it is sounding, and a thin progress line.
Play sends the row's decoded notes on the selected MIDI channel and starts the chosen clock. Votes
are `−` (dislike), `0` (neutral) and `+` (like); unrated rows are still eligible as parents, while
disliked rows are not.

- **More** moves to the next five candidates and keeps existing ratings.
- **Evolve** breeds a new generation from the current ratings, keeping some high-scoring lines and
  adding fresh immigrants.
- **New population** discards the run and generates a fresh set with the current settings.
- **Loop playback** wraps a phrase to its start instead of stopping at the end.

Every genetic decision flows through a seeded generator, and the population, ratings, archive and
RNG state are saved in the project. Reopening a session restores the same five rows and the same
future random choices.

### Settings

Genes are sixteenth-note rest / note / hold steps over scale degrees, not absolute pitches; they
are decoded only when a row is played. This is why mutations stay in key.

- **Line** chooses Melody or Bass and sets the default register.
- **Key** and **Scale** choose the tonal centre and one of Melogen's local twelve scales, from
  major and the modes through pentatonic, blues, whole tone and chromatic.
- **Octave** sets the lowest note, **Bars** and **Time signature** set the phrase length.
- **Tempo** drives the internal clock.
- **Crossover rate**, **Mutation rate** and **Population** steer the genetic algorithm.
- **MIDI channel** chooses the note channel (1-16). Clock messages are system realtime.
- **Clock source** selects Internal, MIDI or Host. **Send MIDI clock** emits 24 PPQ clock and
  Start/Stop unless the plugin is following an external MIDI clock.

Changing the line or scale shapes new melodies; rows already on screen keep their notes.

### Clock and transport

**Internal** needs no host transport. It schedules note-on, note-off, clock and panic events in
absolute time, so a row sounds the same at any block size or in an offline render.

**MIDI** follows incoming 24 PPQ clock with MIDI Start (`0xfa`), Continue (`0xfb`) and Stop
(`0xfc`). Start resets to step 0, Continue resumes the row that was selected before Stop, and Stop
sends CC 123 and clears active notes. This source ignores incoming notes and never sends its own
clock, avoiding a feedback loop with the master.

**Host** reads host tempo, play state and bar/beat/tick. A row starts on a transport start, a
restart after a transport stop, or a BBT jump; continuous playback after a natural non-loop stop
does not restart it. Bar/beat values are converted from DPF's 1-based values to the core's
zero-based grid.

### MIDI / Clock

The fourth tab replaces Melogen's remote Instrument panel with live output diagnostics: output
channel, last note and velocity, playing row, clock source, resolved BPM, transport state, 24 PPQ
tick, generation, round, rating count, phrase length and routing notes. These values come from
read-only output parameters updated by the processor, so they reflect what was actually sent.

### About

The final tab explains the port and its boundaries: no downloaded `.mid` files, no browser audio,
no remote Jig instruments and no web server.

## State and automation

All 14 settings are ordinary host parameters and can be automated. The saved `session` state is
versioned text holding settings, population, archive, RNG and run counters. Editor actions travel
through a DSP-only `action` key and are never stored with the project.

There is no CC parameter map and no Conductor integration. Incoming MIDI is used only for clock
and transport when **Clock source** is MIDI.

## Status

Core tests, the VST3 target and the four-tab editor are complete. Core tests cover RNG
reproducibility, decoding, settings, state round-trips, votes and evolution, block-size
independence, looping, outgoing clock, MIDI Start/Continue/Stop and host BBT edge cases. A
920x700 screenshot was captured and dimension-checked, but the capturing model could not
inspect PNG pixels and `tesseract` was unavailable, so visual acceptance is explicitly pending.
Headless host load/save/reopen in REAPER 7.85 verified that distinctive settings, parameters
and a regenerated 24-entry population from `action=new` persist (action key not saved; see
`/tmp/opencode/melevolve-reaper-check.RPP` and `/tmp/opencode/melevolve-reaper-reopen.log`).
Listening/render validation, live output-status observation and timing-source checks
(internal clock, incoming 24 PPQ MIDI Clock, host BBT) remain pending; headless render/play
attempts with a downstream ReaSynth produced silent output in this environment.
