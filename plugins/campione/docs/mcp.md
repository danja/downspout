# Campione MCP Server

Campione embeds an HTTP MCP server that starts automatically when the plugin
loads in any VST3 host (REAPER, etc.). It exposes the sampler's controls,
zone map, wave editing, recording, and patch I/O as MCP tools.

## When to use it

Use the MCP server when you want an agent (or a script) to drive the sampler
instead of doing it by hand in the plugin UI:

- Building a kit or instrument programmatically: load a directory of WAVs,
  slice a loop, and map the results across the keyboard in one go.
- Batch wave surgery: normalize, trim, fade, or reverse many zones without
  round-tripping through an external editor.
- Repeatable setups: save and reload patches as Turtle RDF files, so a session
  can be reconstructed exactly.
- Inspecting live state from outside the host — current parameters, zone list,
  and recording status are all readable.

It is not needed for ordinary manual use; the plugin UI covers that.

## Transport and port

The server listens on `POST http://localhost:<port>/mcp` and speaks JSON-RPC
(MCP streamable HTTP). Default port is **7220**. If that port is taken (for
example a second Campione instance), it tries 7221–7229 and binds the first
free one. The `get_port` tool reports the actual port:

```
curl -s -X POST http://localhost:7220/mcp \
  -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"get_port","arguments":{}}}'
```

Load Campione in the host **before** registering the server — there is nothing
listening until the plugin is instantiated.

## Claude Code

### HTTP server

```
claude mcp add --transport http campione http://localhost:7220/mcp
claude mcp list
```

Adjust the port if `get_port` reports something other than 7220.

### stdio proxy

If the HTTP registration connects but the tools never appear in context (this
has been observed with Claude Code's HTTP transport), use the bundled
stdio↔HTTP proxy instead. It scans ports 7220–7229 for the live instance, so no
port needs to be configured:

```
claude mcp remove campione
claude mcp add campione /home/danny/github/downspout/scripts/campione_mcp_proxy.sh
claude mcp list
```

## OpenAI Codex

### HTTP server

Codex reads `~/.codex/config.toml`. Remote/HTTP MCP servers require the RMCP
client:

```toml
experimental_use_rmcp_client = true

[mcp_servers.campione]
url = "http://localhost:7220/mcp"
```

### stdio proxy

If your Codex build lacks HTTP MCP support, or the port varies, point it at the
proxy over stdio:

```toml
[mcp_servers.campione]
command = "/home/danny/github/downspout/scripts/campione_mcp_proxy.sh"
```

Equivalently, from a terminal:

```
codex mcp add campione -- /home/danny/github/downspout/scripts/campione_mcp_proxy.sh
codex mcp list
```

## Supported facilities

### Global parameters

| Tool | Description |
|------|-------------|
| `set_master_volume` | Master output level 0–1 |
| `set_midi_channel` | MIDI channel filter (0 = all, 1–16) |
| `set_crossfade_ms` | Loop crossfade duration in ms (0–100) |
| `set_pitch_bend_range` | Pitch-bend wheel range in semitones (0–24) |
| `get_parameters` | Current parameter values as JSON |

### Zone management

| Tool | Description |
|------|-------------|
| `load_zone` | Load a WAV file by absolute path |
| `remove_zone` | Remove a zone by 0-based index |
| `clear_zones` | Remove all zones |
| `get_zones` | Current zone list as JSON |
| `update_zone` | Edit root note, key range, octave shift, loop toggle, loop points |
| `update_zone_dsp` | Per-zone ADSR, biquad filter (type/cutoff/Q), pan, and mute |
| `slice_zone` | Slice a zone into equal or transient-detected sub-zones on consecutive notes |

### Keyboard mapping

| Tool | Description |
|------|-------------|
| `map_drum` | Map all zones to consecutive GM drum notes from 35 (Bass Drum 2) |
| `map_spread` | Spread all zones evenly over C2–B5 (notes 36–83) |

### Wave editing (destructive — writes the source WAV)

| Tool | Description |
|------|-------------|
| `normalize_zone` | Scale to peak 1.0 |
| `trim_zone` | Remove leading/trailing silence (`threshold_db`, default −60) |
| `fade_zone` | Apply linear fade-in / fade-out |
| `reverse_zone` | Reverse the audio |

### Recording

| Tool | Description |
|------|-------------|
| `start_recording` | Capture audio input into a new zone |
| `stop_recording` | Stop capture and auto-map the recording; errors if nothing was captured |
| `get_recording_status` | Recording state: active, frames captured, elapsed seconds |
| `set_recording_dir` | Directory for recorded WAVs, slices, and the autosave patch (default `~/.vst3/campione-data`) |

### Patch files

| Tool | Description |
|------|-------------|
| `save_patch` | Save zones and parameters to a Turtle RDF patch (.ttl); path auto-generated if omitted |
| `load_patch` | Load zones and parameters from a .ttl patch, replacing the current zone list |

### Utility

| Tool | Description |
|------|-------------|
| `get_port` | Port the server is listening on |
| `refresh_ui` | Force the UI to repaint from current DSP state |

## Example session

```
# What zones are loaded?
get_zones → {"zones":[{"index":0,"root_note":60,...}]}

# Normalize zone 0
normalize_zone {"index": 0}

# Fade in 20 ms, fade out 50 ms on zone 1
fade_zone {"index": 1, "fade_in_ms": 20, "fade_out_ms": 50}

# Set ADSR on zone 0: 10 ms attack, 200 ms decay, 0.8 sustain, 300 ms release
update_zone_dsp {"index": 0, "attack_ms": 10, "decay_ms": 200, "sustain_level": 0.8, "release_ms": 300}

# Enable a low-pass filter at 2 kHz on zone 0
update_zone_dsp {"index": 0, "filter_enabled": true, "filter_type": 0, "filter_cutoff": 2000, "filter_q": 0.707}

# Pan zone 0 left
update_zone_dsp {"index": 0, "pan": -0.5}

# Slice zone 0 into 16 equal slices starting at MIDI note 36
slice_zone {"index": 0, "num_slices": 16, "start_note": 36}

# Lay the resulting zones out as a drum kit
map_drum {}

# Save current state to a patch file
save_patch {}

# Load a patch
load_patch {"path": "/home/danny/.vst3/campione-data/patch_20260101_120000.ttl"}

# Set volume to 70%
set_master_volume {"value": 0.7}
```
