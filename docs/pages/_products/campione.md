---
title: Campione
order: 15
bundle: campione.vst3
kind: Instrument
role: Multi-zone sampler
screenshot: /assets/plugins/campione.png
summary: Multi-zone sampler with per-note MIDI mapping, WAV file loading, audio recording with auto-pitch detection, pitch-shift gap fill, zero-crossing loop snap, and crossfade looping.
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

### Parameters

- **Volume** — master output level.
- **MIDI Channel** — incoming MIDI channel filter; 0 accepts all channels.
- **Crossfade ms** — crossfade length at loop boundaries in milliseconds.
- **Pitch Bend Range** — pitch-bend wheel range in semitones.
- **Recording** — boolean flag; non-zero arms the audio input for capture into a new sample zone.
- **MCP Enabled** — boolean flag; enables the embedded MCP HTTP server (default on).

### MCP server

Campione embeds an HTTP MCP server (default port 7220) that exposes all plugin
controls as MCP tools. This allows Claude Code or any MCP client to load zones,
edit wave data, adjust parameters, and inspect state without touching the DAW
UI. Register once with `claude mcp add campione /path/to/campione_mcp_proxy.sh`.

### Status

Full implementation with core sampler engine, per-note MIDI mapping,
crossfade looping, zero-crossing snap, pitch-shift gap fill, WAV loading,
audio recording with auto-pitch detection, wave editing (normalize, trim, fade,
reverse), and embedded MCP server.
