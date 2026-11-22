---
title: Fontana
order: 16
bundle: fontana.vst3
kind: Instrument
category: instrument
group: sources
role: SoundFont player + slice sampler
screenshot: /assets/plugins/fontana.png
summary: SF2/SF3 SoundFont player with bank/preset zone resolution, WAV/Ogg sample loading, transient and equal slicing with editable slice points, persistent file state, and a custom in-plugin file browser with waveform preview and audition.
---

## Opinion

Fontana fills the gap between a full SoundFont synthesizer and a one-shot sample
player. It parses SoundFont containers directly (including SF3/Vorbis), resolves
bank/preset/instrument zones with generators and modulators, and also behaves as
a slice sampler for WAV and Ogg files. The browser is drawn inside the plugin:
selecting a sound file shows its waveform, and **Audition** plays it without
leaving the editor.

## Functionality

### SoundFont playback

- Reads RIFF `sfbk` banks, `INFO`, `sdta` and `pdta` metadata, preset/instrument
  zones, sample headers, generators and modulators.
- Supports SF2 PCM16 and 24-bit sample data. SF3 compressed samples are decoded
  from complete mono Ogg Vorbis streams using the vendored `stb_vorbis` decoder.
- GM-style **Bank** and **Preset** parameters select a zone; note and velocity
  ranges, key/root transposition, loop markers, pan, filter, envelopes, LFOs,
  pitch bend and common MIDI CC modulators are applied per voice. A global
  **Pan** offset (-1 = left, 0 = centre, +1 = right) is added to the SoundFont
  pan, so a hard-left preset can be rebalanced without editing the bank.
- 32 voices with voice stealing, sustain pedal handling, per-channel pitch bend
  and a global MIDI-channel filter (0 = all).

### Sample and slice playback

- Loads 8/16/24/32-bit PCM and 32/64-bit float WAV, plus Ogg Vorbis, with root
  note and loop markers read from the WAV `smpl` chunk where present.
- Equal slicing and energy/transient slicing map consecutive slices to
  consecutive MIDI notes from **Base note**. Slice boundaries can be dragged in
  the waveform; slices can be muted, normalized, looped, cleared or regenerated.
- **Source** switches between SoundFont and slice playback.

### File browser and preview

- A custom NanoVG browser lists directories, SoundFont files and audio files.
  Selecting a file decodes it and draws min/max waveform peaks; entering a folder no
  longer auto-decodes its first file.
- The list has a dedicated gutter with `^`/`v` buttons, a draggable proportional
  scrollbar and mouse-wheel support, so clicks on the right edge of a row navigate
  or select instead of scrolling.
- **Audition** plays the selected file through an isolated preview engine.
- **Load file** assigns an SF2/SF3 to the SoundFont slot or a WAV/Ogg to the
  sample slot and triggers the audition.
- The current folder is saved as state, so closing and reopening the editor (or
  the host window losing focus and recreating the view) keeps the same directory.

### State and persistence

File paths, the browser directory, slice maps and settings (including the pan
offset) are versioned text state. Sample data is re-read from the referenced
files when a session is reopened, so sessions stay small and edits to the source
file are picked up on reload.

### Known limitations

- The global **Filter enable/cutoff** control is stored and exposed but is not
  applied as a separate post-voice filter; per-voice SoundFont filters are
  applied.
- Polyphonic and channel-pressure modulator sources evaluate to zero.
- Chorus and reverb sends are parsed into the data model but no effects unit is
  implemented.
- Preview audition currently plays slice 0 of a sliced source.
- The catalogue screenshot predates the Pan slider. A fresh 1280x800 capture was
  taken after this change and differs only in the PLAYER mixer row; visual
  acceptance is pending because the capture session could not inspect image content.

### Parameters

- **Master Volume**, **MIDI Channel**, **Bank**, **Preset**, **Transpose**,
  **Bend Range**, **Attack**, **Release**
- **Source**, **Slice Base Note**, **Slice Loop**
- **Filter Enable**, **Filter Cutoff** (stored; see limitations)
- **Pan** (offset added to the preset's own pan; default centre)
- Output status: **Effective Bank**, **Effective Preset**, **Active Voices**,
  **Last Note**, **Load Status**
