# Mistakes log

## 2026-08-23 — Documented MCP argument names from prose instead of the schema

**What happened:** `plugins/campione/docs/mcp.md` was rewritten with example
calls using `sustain` and `filter_cutoff_hz` for `update_zone_dsp`. The actual
tool schema uses `sustain_level` and `filter_cutoff`. The doc also omitted
`octave_shift` (`update_zone`), `muted` (`update_zone_dsp`), and
`threshold_db` (`trim_zone`), and repeated the tool description's stale claim
that the recording directory defaults to `~/campione_recordings` when the code
defaults to `$HOME/.vst3/campione-data`.

**Root cause:** The rewrite carried argument names forward from the previous
version of the doc rather than reading the `inputSchema` entries in
`plugins/campione/src/campione_mcp_server.cpp`.

**Prevention:** When documenting MCP tools, take names, types, and defaults
from the `tools/list` schema in the server source (or a live `tools/list`
response), not from existing prose. Copy-pasteable examples must be verified
against the schema.
