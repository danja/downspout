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
summary: Melogen's five-row genetic melody generator as a live MIDI plugin: pick a Favourite, play, vote −/0/+, and breed the next generation on internal, MIDI Clock, or host timing.
---

> **Ported from [Melogen](https://github.com/danja/melogen)**. The genetic algorithm, local scale
> set and light two-tab layout come from the browser app. MIDI output, native controls and clock
> handling are original to this port.

## Opinion

Melevolve is for the moment when a plausible melodic idea matters more than a finished one. It
does not try to write a tune in one shot: it offers five short lines, asks for a quick reaction,
and uses those reactions to breed the next set. The workflow is deliberately closer to auditioning
takes than to editing notes.

The plugin emits MIDI only and makes no sound. Put it before an instrument in a MIDI-capable host,
or route its MIDI output to an instrument track. Its panel keeps Melogen's pale background, white
row cards, round green Play buttons and − / 0 / + votes. A top Favourite bar and one-line status
make the five-row interaction and live output readable at a glance.

## Functionality

### Interact

The **Interact** tab opens with a compact generation/round line and a **Favourite** bar. Each of
the five short rows has a round Play button, a three-way vote, a **Fave** button and a thin progress
line. **Fave** chooses which row the top bar follows; its green Play button starts that row, and a
host transport start plays it automatically. The default Favourite is row 1. Votes are `−`
(dislike), `0` (neutral) and `+` (like); unrated rows are still eligible as parents, while disliked
rows are not.

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

**Host** reads host tempo, play state and bar/beat/tick. It plays the Favourite row: pressing play
in the host emits MIDI without a prior button press in the plugin. A restart after a transport
stop or a BBT jump starts that row again; continuous playback after a natural non-loop stop does
not restart it. Bar/beat values are converted from DPF's 1-based values to the core's zero-based
grid.

### Live status

The old MIDI / Clock and About tabs are gone. Interact now carries one compact status line: clock
source, resolved BPM and MIDI channel at the top; sounding row, last pitch/velocity and 24 PPQ tick
at the bottom. These values come from read-only output parameters updated by the processor, so
they reflect what was actually sent.

## State and automation

All 14 settings are ordinary host parameters and can be automated. The saved `session` state is
versioned text holding settings, population, archive, RNG and run counters. Editor actions travel
through a DSP-only `action` key and are never stored with the project.

There is no CC parameter map and no Conductor integration. Incoming MIDI is used only for clock
and transport when **Clock source** is MIDI.

## Status

Core tests, the VST3 target and the two-tab editor are complete. Core tests cover RNG
reproducibility, decoding, settings, state round-trips, votes and evolution, block-size
independence, looping, outgoing clock, MIDI Start/Continue/Stop, favourite selection, host play
without a button press, and host BBT rewind/tempo/loop edge cases. The 920x700 screenshot was
recaptured after this UI change and accepted. Headless host load/save/reopen in REAPER 7.85
verified that distinctive settings, parameters and a regenerated 24-entry population from
`action=new` persist (action key not saved; see `/tmp/opencode/melevolve-reaper-check.RPP` and
`/tmp/opencode/melevolve-reaper-reopen.log`). Live listening, render validation and timing-source
observation in REAPER remain pending in this environment.
