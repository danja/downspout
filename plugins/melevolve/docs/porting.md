# Melevolve porting notes

Melevolve ports the Melogen source at `/home/danny/github/melogen` to a headless-audio VST3
MIDI generator. The original app bred melodies in the browser and ultimately downloaded a
`.mid` file for a separate Jig instrument. The plugin emits live MIDI instead.

## Behaviour kept

- The genetic algorithm from `src/ga`: random genomes on a sixteenth-note grid, tournament
  selection over unrated/valued entries, immigration, crossover, mutation, an archive of
  rated genomes, and five candidates shown per round. Unrated entries remain eligible;
  only disliked entries are excluded, matching `eligible()` in the source.
- Melogen's twelve local scales (major through chromatic), root/octave/mode decoding,
  time signatures, step grid, velocity accents and gate gap.
- User flow: play a row, vote − / 0 / +, **More**, **Evolve**, **New population**, with
  votes shown on the same row buttons.
- Versioned text state covering settings, population, archive, row selection, RNG and GA
  generation/round. A session restores an ongoing run exactly.

## Behaviour changed for VST3

| Melogen | Melevolve | Reason |
| --- | --- | --- |
| SMF download | Live note-on/note-off MIDI on the chosen channel | The user goal is MIDI out, not a file. |
| `LOOKAHEAD = 0.2s`, `TICK_MS = 40ms`, `START_DELAY = 0.05s` | Absolute-quarter event queue, sample-accurate block scheduling | Host blocks and offline renders must not depend on wall-clock timers. |
| Instrument tab: remote Jig plugin loading and control | Compact status strip on Interact: channel, source, BPM, last note/velocity and tick | Browser audio and remote plugin hosting do not belong in a VST3. |
| Browser/PWA UI, WebAudio, server routes | Two-tab NanoVG editor | DPF owns the native window; the light palette and layout are preserved. |
| Local storage for tab/settings | Host parameters plus DPF text state | Hosts save and recall parameters and state with the project. |

The editor uses Melogen's light palette from `styles/main.css` (background `#f6f5f1`, white
surfaces, blue accent, green play, red dislike) and keeps two tabs: Interact and Settings.
Interact opens with a Favourite row; each candidate has a **Fave** button that chooses which row
the top bar follows, so a host transport start or a press of the top Play button plays the chosen
row. Live output status is a single compact line instead of a separate diagnostics tab.

## Host parameters

The 14 input parameters are `mode`, `root`, `scale`, `octave`, `bars`, `time_signature`,
`tempo`, `crossover_rate`, `mutation_rate`, `population_size`, `loop`, `clock_source`,
`midi_channel` and `send_clock`. Nine output parameters expose effective live state:
`status_playing`, `status_generation`, `status_round`, `status_clock_bpm`,
`status_clock_tick`, `status_progress`, `status_clock_running`, `status_last_note` and
`status_last_velocity`.

Scale ordinals 0-11 are the local Melogen set, not the canonical 24/33-scale table in
`docs/scales.md`; `docs/scales.md` is therefore unchanged. Time signatures are indexed by
core order, and octave values are absolute MIDI octaves with mode-aware defaults.

## Transport and clock assumptions

- **Internal** uses the `tempo` parameter and the host sample rate. Playback begins at the
  start of the next processed block; events already inside that block are scheduled at their
  exact frame offsets.
- **MIDI** listens for 24 PPQ clock ticks (`0xf8`) and Start/Continue/Stop (`0xfa`/`0xfb`/`0xfc`).
  Start resets to step 0, Continue resumes the row selected before Stop, and Stop panics with
  CC 123 and clears active notes while preserving the selected row. Incoming tick intervals
  estimate the effective BPM for the status display.
- **Host** converts DPF's 1-based `bar`/`beat` to the core's zero-based position and reads
  `beatsPerBar`, `beatType`, `bpm` and fractional ticks. The Favourite row defaults to row 1, so a
  transport start emits MIDI without a prior button press. Changing Fave while the transport is
  rolling switches the audible row on the next block; the new row restarts from step 0 at the
  current host position. It also restarts after a transport stop or a BBT jump; it does not restart
  on every block or after a natural non-loop stop.
- `send_clock` emits 24 PPQ clock, Start and Stop only when the source is not MIDI, preventing
  a MIDI-slaved plugin from fighting its master.
- CC 123 is emitted on every stop path. Note-offs are queued in absolute time, so held notes
  survive block boundaries and changes in block size.

## State and UI messaging

`session` is the only saved state key. It is text with `version=1`, a 14-field `settings=`
line, `rng`, `gen`, `round`, `rows`, and one `e=` or `a=` line per entry/archive item. An
incomplete or out-of-range value is rejected as a whole rather than partially applied.

`action` is a `kStateIsOnlyForDSP` key used editor-to-processor only. Values are `play:N`,
`fave:N`, `vote:N:R`, `more`, `evolve`, `new`, `reset` and `stop`. Actions are never written to a
project. The DSP wrapper applies the same core `Session` mutation as the editor's local
mirror so the panel updates without relying on a DSP-to-UI state callback; DPF's VST3
controller has no usable `updateStateValue` path for arbitrary keys. Live processor state
crosses back through output parameters.

`activate()` and sample-rate changes clear only runtime scheduling state. They must not
reinitialise the patch or reset the GA.

## Validation

Deterministic core tests cover RNG reproducibility, scales/decoding, parameter validation,
settings effects, state round-trips, votes/More/Evolve/New/Reset, block-size independence,
looping, outgoing clock, incoming MIDI Start/Continue/Stop, favourite selection, host play without
an explicit button press, and host BBT match/restart/rewind/tempo/loop edge cases. Core tests are
registered through `downspout_add_core_test`; the `melevolve-vst3` target builds the DSP and NanoVG UI.

Headless REAPER load/save/reopen passed. The captured two-tab panel was accepted; live listening
and timing-source observation in REAPER remain pending in this environment.
