# Sam MCP server

Sam embeds an HTTP MCP server so an agent or a script can drive it while the host plays: set the
words or phonemes to speak, speak them now, and move the parameters. Source:
`src/sam_mcp_server.cpp`; it is a transport and a thin adapter onto `Engine`'s main-thread API
(`setText`, `say`, `setParameter`) and never touches the audio thread.

## Quick start

```
scripts/sam-say.sh hello world                 # speak it now
echo "hello world" | scripts/sam-say.sh        # text from stdin
scripts/sam-say.sh -p "/HEH3LOW2 WER4LD"       # as SAM phonemes
scripts/sam-say.sh --help                      # set, text, status, check, param, params, voice
```

Load Sam in the host first; there is nothing listening until the plugin is instantiated and
activated.

## Transport and port

`POST http://127.0.0.1:<port>/mcp`, JSON-RPC 2.0 (MCP streamable HTTP, `Content-Type:
application/json`). Default port **7230** (Campione uses 7220-7229). If it is taken, e.g. by a second
Sam, 7231-7239 are tried in turn, so every instance in a host gets its own server; `get_port` and
`get_status` report the one bound. `scripts/sam-say.sh` uses the first live Sam from 7230, or `SAM_PORT`.

The server starts at the plugin's first activation, not at construction, so a host scan does not open
a socket. `DOWNSPOUT_SAM_MCP=0` (also `off`, `false`, `no`) in the host's environment disables it.

```
claude mcp add --transport http sam http://localhost:7230/mcp
```

### Security

- Bound to **127.0.0.1** only, no authentication: any local process can control the plugin.
- Requests must declare `Content-Type: application/json` (a web page cannot send that cross-site
  without a preflight, which is never granted), carry a local `Host`, and any `Origin` must be local.
  Otherwise 415 / 403.

## Tools

| Tool | Arguments | Does |
|---|---|---|
| `say` | `text`, `phonetic?` | Replace the text and speak it once now, in either mode, whatever the host transport is doing |
| `set_text` | `text`, `phonetic?` | Replace the text without speaking it |
| `get_text` | | The loaded text and its word slots |
| `check_text` | `text?`, `phonetic?` | Dry run: what SAM reads per token, whether it accepts it, how long it lasts |
| `set_parameter` | `name`, `value` | One parameter, by symbol or panel name; number, boolean or option label |
| `set_parameters` | `values` (object) | Several, in order; stops at the first error and reports what was applied |
| `get_parameters` | | Every parameter: value, range, default, unit, options |
| `set_voice` | `preset` | SAM, Elf, Robot, Stuffy, Old Lady or Alien (speed, pitch, throat, mouth together) |
| `get_status` | | Mode, text, `current_word` (1-based, 0 = silent), port |
| `get_port` | | The bound port |

Examples (arguments only):

```
say            {"text": "/HEH3LOW2 WER4LD", "phonetic": true}
set_text       {"text": "one two _ four"}
check_text     {"text": "HEHLOW", "phonetic": true}      -> accepted: false (the aspirate is "/H")
set_parameter  {"name": "mode", "value": "Transport"}
set_parameter  {"name": "step", "value": "1/8"}
set_parameters {"values": {"speed": 90, "loop": false}}
set_voice      {"preset": "Robot"}
```

### Behaviour worth knowing

- **`phonetic` travels with the text.** It sets the Phonetic switch *before* the text is applied, so
  the two always agree. Leave it out to keep the switch as it is.
- **`say` versus the Speak button.** Speak is a Transport-mode control (greyed out in Keys mode).
  `say` works in both modes: it walks the text once, one word per **Step** at the host tempo (120 bpm
  if there is no host tempo), ignoring the host transport. If the host is actually rolling in Transport
  mode the transport is already reading the text, so `say` just installs the new text and the playhead
  picks it up. The new text is always in place before the first word, never the old.
- **In Keys mode, `set_text` is what a MIDI note will speak next.** It takes effect on the next audio
  block.
- **Text is cleaned like typed text.** Printable ASCII only, tabs and newlines fold to spaces, 2000
  bytes max, a lone `_` is a rest. The reply says what was kept (`text`, `truncated_or_cleaned`).
- **Parameters** accept a number (clamped to the range, integers rounded), a boolean, or the label the
  panel shows (`"Transport"`, `"1/8"`, `"Ch 3"`, `"key = word"`; case-insensitive). `out_word` and
  `mcp_port` are read-only. Use `get_parameters` for names, ranges and options.
- **Vet phonemes first.** SAM silently refuses some strings (e.g. `HEHLOW` as phonemes); `check_text`
  reports `accepted: false` and `seconds: 0` instead of leaving you with a silent word.

## What the host and panel see

- A change made over MCP is applied to the engine immediately and the host is asked to record it as a
  parameter change (best effort: a host may refuse an off-thread request). The text is part of the
  saved state, so it is stored with the project.
- The panel follows text set over MCP while it is open, through an in-process channel keyed by the
  server's port (`include/sam_ui_bridge.hpp`). DPF's `updateStateValue` does nothing in the
  VST3 wrapper and a panel cannot ask the plugin for its state, so this is the route (Campione does the
  same). The port is exposed to the panel as the read-only `mcp_port` output parameter, which is also
  how several Sams in one host stay apart.
- If the host and an agent write the same parameter, the last writer wins, as with any automation.

## Not covered

No MIDI note injection over MCP (use the host's MIDI), no sample-accurate scheduling of words (use
Transport mode for that), no authentication beyond the loopback bind.

## Tests

`ctest --test-dir build -R sam`: `tests/sam_mcp_tests.cpp` covers the protocol, every tool, bad input,
the hooks, control while the audio thread runs, and a real loopback socket (port fall-through, the
415/403 guards, shutdown). `say()` itself is in `tests/sam_core_tests.cpp`.
