# Polyscribe

`polyscribe.vst3` — polyphonic audio-to-MIDI generator.

Polyscribe listens to a stereo audio input and emits MIDI note messages. It is a
transcriber rather than an audio processor: choose what the source is —
**Melody**, **Bass**, **Chords** or **Drums** — and the analyser selects notes
accordingly. The output is silent unless **Audio Thru** is switched on, so the
usual wiring is audio in from the source and MIDI out to a synth on another
track.

Magneto look: a header explains the signal flow, then Input Listening, Note
Shaping, Routing and Live Status panels group the controls by purpose. Segmented
buttons choose the input type; steppers set notes, counts and channels; sliders
set the continuous shaping controls. Read-only status parameters show the last
note, active count, velocity, input level and a detection flash.

## Signal flow

```
in L/R ─ mono sum ─ 4096-sample Hann window ─ Goertzel bins 24-120 ─ pitch selection ─┐
                 └─ 1024-sample Hann window ─ 3 drum probes ─ onset gating ───────────┤
                                                                                      ├─ gate/release ─ MIDI out
                                                              Audio Thru ──────────────┘
```

Analysis runs every 512 samples on absolute sample counts, so host block size
does not change what is measured or when a note is emitted. The first pitched
analysis happens once the 4096-sample window is full: about 93 ms at 44.1 kHz or
85 ms at 48 kHz. The plugin does not report latency to the host because it never
delays audio; the audio pass-through is sample-parallel, while MIDI is
necessarily detected after that window has filled.

## Input types

| Type | Selection | Search range |
|------|-----------|--------------|
| Melody | Strongest accepted pitch, with hysteresis on the last note | C3-C6 (48-84) |
| Bass | Lowest accepted pitch, with the same hysteresis | C2-C4 (36-60) |
| Chords | Up to **Max Notes** simultaneous pitches; already-sounding tones are retained while they stay strong | C2-C7 (36-96) |
| Drums | Three band onsets: 65 Hz, 294 Hz and 4.2 kHz probes map to GM kick 36, snare 38 and closed hat 42 | fixed probe bands |

The **Low Note** and **High Note** controls narrow the pitched search further.
They do not retune the drum probes. Drums are routed to channel 10 by convention,
but **MIDI Channel** remains user-selectable. Melody and Bass are always
monophonic regardless of **Max Notes**.

## Controls

| Control | Range | Default | Meaning |
|---------|-------|---------|---------|
| Input Type | Melody / Bass / Chords / Drums | Melody | Chooses the note-selection strategy |
| Sensitivity | 0-100 % | 65 % | Raises or lowers the detection floor |
| Low Note | 24-96 | 36 | Lower bound, intersected with the input type's range |
| High Note | 36-120 | 84 | Upper bound, intersected with the input type's range |
| Max Notes | 1-8 | 4 | Polyphony limit in Chords mode |
| Gate | 0-100 ms | 20 ms | Minimum time a detected note stays on |
| Release | 0-500 ms | 120 ms | Extra hold after the note is no longer detected |
| Velocity | 1-127 | 100 | Base note-on velocity |
| Dynamics | 0-100 % | 35 % | How much detected level varies velocity around the base |
| Transpose | -24 to +24 st | 0 | Whole semitones added to output notes |
| MIDI Channel | 1-16 | 1 | Channel for generated note-on/off messages |
| MIDI Out | Off / On | On | Enables or suppresses generated MIDI |
| Audio Thru | Off / On | Off | Copies the sanitised input to the audio outputs |

Dynamics scales the base velocity between 45 % and 100 % of its value; at 0 %
every note has the same velocity. Sensitivity at 0 % rejects the quiet test tone
used in the core suite, while the default and full sensitivity accept it.

## MIDI

Generated note-on messages are sent when a note first appears. A matching
note-off follows once both **Gate** and **Release** have elapsed after the note
stops being seen. Note-offs are also sent when MIDI Out is turned off, the
channel changes, the input type changes, Transpose changes, or the plugin is
reset. Changing one of those settings releases sounding notes and lets them
retrigger under the new settings.

In Drums mode each onset emits 36, 38 or 42 on the selected channel and holds it
for Gate + Release. Shorten those controls if you want tighter drum hits.
Velocity comes from the band magnitude with the shared Dynamics mapping.

There is no MIDI input port and no CC mapping. Automate the host parameters
instead. Five output parameters expose `out_count`, `out_last_note`,
`out_level`, `out_velocity` and `out_flash` to the panel and host.

## Documented assumptions

- **State.** None. Every setting is a host parameter, which DPF saves by symbol.
  `activate()` and `sampleRateChanged()` clear analysis and note state only;
  they never reset the patch.
- **Transport.** Not used. Detection follows audio continuously and has no
  tempo, bar or play-state behaviour.
- **Audio.** Inputs are summed to mono for analysis. Non-finite samples are
  replaced with zero and counted internally. With Audio Thru off the outputs
  are silent; with it on each channel mirrors its own input.
- **Polyphony.** At most eight active notes and 64 MIDI events per block; both
  are bounded by fixed arrays and never allocate in the audio path.
- **UI.** Live values are ordinary output parameters because the DPF panel has
  no direct path to the engine's runtime state.

## Known limits

- Pitch detection is vibrato- and phase-sensitive: equal chord tones can trade
  places as their relative phase drifts, so Chords mode uses hysteresis to keep
  the chosen notes rather than re-triggering every hop.
- Harmonics up to the eighth are suppressed when a lower partial is stronger.
  Inharmonic or heavily distorted material can still produce extra notes.
- Drums mode recognises three spectral regions, not a trained drum classifier.
  It will map a sustained tone in one region to a drum hit at its onset.
- Notes are detected after the analysis window fills; this is not a sample-
  accurate transcription of a percussive attack.

## Verification

The portable core has deterministic tests for all four input types, sensitivity,
note-range filtering, max-notes, gate, release, velocity, dynamics, transpose,
MIDI channel, MIDI Out, Audio Thru and channel changes. The VST3 target builds
without warnings. The 920x720 panel was reviewed at full resolution and published
as `docs/pages/assets/plugins/polyscribe.png`. Host listening and MIDI-routing
checks on real voices, bass, chords and a drum bus are in progress.
