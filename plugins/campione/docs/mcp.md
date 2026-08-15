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

| Tool | Description |
|------|-------------|
| `set_master_volume` | Master output level 0–1 |
| `set_midi_channel` | MIDI channel filter (0 = all, 1–16) |
| `set_crossfade_ms` | Loop crossfade duration in ms |
| `set_pitch_bend_range` | Pitch-bend wheel range in semitones |
| `start_recording` | Begin capturing audio input as a new zone |
| `stop_recording` | Stop and auto-map the recording |
| `load_zone` | Load a WAV file by absolute path |
| `remove_zone` | Remove zone by 0-based index |
| `update_zone` | Edit root note, key range, loop toggle and loop points |
| `normalize_zone` | Scale to peak 1.0 and save WAV |
| `trim_zone` | Remove leading/trailing silence and save WAV |
| `fade_zone` | Apply fade-in / fade-out and save WAV |
| `reverse_zone` | Reverse audio and save WAV |
| `get_zones` | Return current zone list as JSON |
| `get_parameters` | Return current parameter values as JSON |
| `get_port` | Return the port the server is listening on |

## Example session

```
# What zones are loaded?
get_zones → {"zones":[{"index":0,"root_note":60,...}]}

# Normalize zone 0
normalize_zone {"index": 0} → "zone 0 normalized to peak"

# Fade in 20 ms, fade out 50 ms on zone 1
fade_zone {"index": 1, "fade_in_ms": 20, "fade_out_ms": 50}

# Set volume to 70 %
set_master_volume {"value": 0.7}
```
