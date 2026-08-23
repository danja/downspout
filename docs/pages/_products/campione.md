---
title: Campione
order: 15
bundle: campione.vst3
kind: Instrument
role: Multi-zone sampler
screenshot: /assets/plugins/campione.png
summary: Multi-zone sampler with per-note MIDI mapping, WAV file loading, audio recording with auto-pitch detection, pitch-shift gap fill, zero-crossing loop snap, crossfade looping, per-zone ADSR/filter/pan, beat slicing, and an embedded MCP server.
---

## Functionality

Campione is a multi-zone sampler instrument. Each loaded WAV file is mapped to
a MIDI note range so that incoming notes trigger the nearest sample zone and
pitch-shift it to the target pitch. The gap-fill algorithm generates smooth
pitch-shifted transitions for notes that fall between recorded samples.

Loop points are snapped to zero crossings to eliminate clicks at loop
boundaries. Crossfade looping blends the loop end back into the start to
produce smooth sustained tones from short recordings.

Audio recording is built in: the plugin can capture live input directly into a
new sample zone. After recording, it runs auto-pitch detection to set the root
note and tune the zone into the mapping automatically.

WAV file loading uses standard RIFF/WAVE PCM and 32-bit float formats. Zones
can be reloaded, replaced, or cleared without restarting the host.

Beat slicing splits a zone into equal or transient-detected sub-zones mapped to
consecutive MIDI notes, suitable for drum loop playback and remixing.

### Parameters

- **Volume** — master output level.
- **MIDI Channel** — incoming MIDI channel filter; 0 accepts all channels.
- **Crossfade ms** — crossfade length at loop boundaries in milliseconds.
- **Pitch Bend Range** — pitch-bend wheel range in semitones.
- **Recording** — boolean flag; non-zero arms the audio input for capture into a new sample zone.
- **MCP Enabled** — boolean flag; enables the embedded MCP HTTP server (default on).

### Per-zone DSP controls

Each zone has an independent ADSR envelope and biquad filter editable via
rotary knobs in the zone DSP panel below the waveform display:

- **ATK / DEC / SUS / REL** — attack, decay, sustain, release envelope
- **Filter enable / type** — low-pass, band-pass, high-pass, or notch
- **CUTOFF / Q** — filter cutoff frequency and resonance
- **PAN** — stereo pan position (−1 full left to +1 full right)

All per-zone DSP parameters are persisted in both plugin state and Turtle patch
files, and are accessible via the `update_zone_dsp` MCP tool.

### MCP server

Campione embeds an HTTP MCP server (default port 7220) that exposes all plugin
controls as MCP tools. This allows Claude Code or any MCP client to load zones,
edit wave data, adjust parameters, and inspect state without touching the DAW
UI. Register once with `claude mcp add campione /path/to/campione_mcp_proxy.sh`.

See [plugins/campione/docs/mcp.md](https://github.com/danja/downspout/blob/main/plugins/campione/docs/mcp.md) for the full tool reference.

### Status

Full implementation with core sampler engine, per-note MIDI mapping,
crossfade looping, zero-crossing snap, pitch-shift gap fill, WAV loading,
audio recording with auto-pitch detection, per-zone ADSR envelope, biquad
filter, stereo pan, beat slicing, wave editing (normalize, trim, fade,
reverse), patch save/load (Turtle RDF), waveform display with loop point
handles, and embedded MCP server.
