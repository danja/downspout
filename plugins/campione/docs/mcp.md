# Campione MCP Server

Campione embeds an HTTP MCP server that starts automatically when the plugin
loads in any VST3 host (REAPER, etc.). It exposes all plugin controls as MCP
tools so Claude Code or any other MCP client can drive the sampler directly.

## Registering with Claude Code

Load Campione in REAPER, then run once in a terminal:

```
claude mcp add campione /home/danny/github/downspout/scripts/campione_mcp_proxy.sh
```

This uses a thin stdio↔HTTP proxy (`scripts/campione_mcp_proxy.sh`) so that Claude Code
sees the tools via the stdio MCP transport, which reliably injects tool schemas into the
model context. The earlier `--transport http` registration appeared to connect but silently
failed to register tool schemas in Claude's context.

Verify it is registered:

```
claude mcp list
```

## Port

Default port is **7220**. If that port is already in use (e.g. a second
instance of Campione), the server tries 7221–7229 and binds the first free
one. Use the `get_port` tool to find the actual port:

```json
{"method":"tools/call","params":{"name":"get_port","arguments":{}}}
```

If you need to point Claude Code at a non-default port, remove the old entry
and re-add with the correct URL:

```
claude mcp remove campione
claude mcp add --transport http campione http://localhost:7221/mcp
```

## Available tools

### Global parameters

| Tool | Description |
|------|-------------|
| `set_master_volume` | Master output level 0–1 |
| `set_midi_channel` | MIDI channel filter (0 = all, 1–16) |
| `set_crossfade_ms` | Loop crossfade duration in ms (0–100) |
| `set_pitch_bend_range` | Pitch-bend wheel range in semitones (0–24) |
| `get_parameters` | Return current parameter values as JSON |

### Zone management

| Tool | Description |
|------|-------------|
| `load_zone` | Load a WAV file by absolute path |
| `remove_zone` | Remove zone by 0-based index |
| `clear_zones` | Remove all zones at once |
| `get_zones` | Return current zone list as JSON |
| `update_zone` | Edit root note, key range, loop toggle, and loop points |
| `update_zone_dsp` | Edit per-zone ADSR envelope and biquad filter (attack, decay, sustain, release, filter type/cutoff/Q, pan) |
| `slice_zone` | Slice a zone into equal or transient-detected sub-zones mapped to consecutive MIDI notes |

### Wave editing (destructive — saves to source WAV)

| Tool | Description |
|------|-------------|
| `normalize_zone` | Scale to peak 1.0 |
| `trim_zone` | Remove leading/trailing silence |
| `fade_zone` | Apply linear fade-in / fade-out |
| `reverse_zone` | Reverse audio |

### Recording

| Tool | Description |
|------|-------------|
| `start_recording` | Begin capturing audio input as a new zone |
| `stop_recording` | Stop and auto-map the recording |
| `get_recording_status` | Return recording state: active, frames captured, elapsed seconds |
| `set_recording_dir` | Set the directory where recorded WAV files are saved |

### Patch files

| Tool | Description |
|------|-------------|
| `save_patch` | Save zones and parameters to a Turtle RDF patch file (.ttl); path auto-generated if omitted |
| `load_patch` | Load zones and parameters from a Turtle RDF patch file (.ttl); replaces current zone list |

### Utility

| Tool | Description |
|------|-------------|
| `get_port` | Return the port the server is listening on |
| `refresh_ui` | Force the UI to repaint with the current DSP state |

## Example session

```
# What zones are loaded?
get_zones → {"zones":[{"index":0,"root_note":60,...}]}

# Normalize zone 0
normalize_zone {"index": 0}

# Fade in 20 ms, fade out 50 ms on zone 1
fade_zone {"index": 1, "fade_in_ms": 20, "fade_out_ms": 50}

# Set ADSR on zone 0: 10 ms attack, 200 ms decay, 0.8 sustain, 300 ms release
update_zone_dsp {"index": 0, "attack_ms": 10, "decay_ms": 200, "sustain": 0.8, "release_ms": 300}

# Enable a low-pass filter at 2 kHz on zone 0
update_zone_dsp {"index": 0, "filter_enabled": true, "filter_type": 0, "filter_cutoff_hz": 2000, "filter_q": 0.707}

# Pan zone 0 left
update_zone_dsp {"index": 0, "pan": -0.5}

# Slice zone 0 into 16 equal slices starting at MIDI note 36
slice_zone {"index": 0, "num_slices": 16, "start_note": 36}

# Save current state to a patch file
save_patch {}

# Load a patch
load_patch {"path": "/home/danny/.vst3/campione-data/patch_20260101_120000.ttl"}

# Set volume to 70%
set_master_volume {"value": 0.7}
```
