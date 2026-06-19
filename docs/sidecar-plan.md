# Sidecar implementation checklist

Sidecar is the Downspout VST client for AI-assisted phrase playback. The local
`downspout-ai-coordinator` process will own OpenAI API calls later; Sidecar
stays realtime-safe and only queues validated MIDI phrases.

## First slice

- [x] Add `plugins/sidecar` scaffold.
- [x] Add local phrase protocol structs and validation.
- [x] Add deterministic fallback phrase generation.
- [x] Add transport-aware phrase playback engine.
- [x] Add text serialization for accepted phrase state.
- [x] Add DPF/VST3 wrapper and compact UI.
- [x] Add deterministic core tests.
- [x] Wire CMake build target and root build option.
- [x] Add coordinator CLI at `tools/ai-coordinator`.
- [x] Add OpenAI API client in coordinator.
- [x] Add `state.json -> solo.mid` workflow.
- [x] Add offline MIDI-derived tune context in coordinator.
- [x] Add local phrase-response validation/rendering.
- [x] Add provider-neutral solo request JSON builder.
- [x] Add CLI-only OpenAI request path.
- [x] Add live MIDI-derived tune context in Sidecar/coordinator.
- [x] Add localhost live request/response.
- [x] Add state summaries for Ground and Cadence.
- [x] Add Sidecar to release packaging once the first plugin slice is accepted.

## Implemented plugin slice

- `plugins/sidecar` builds `sidecar.vst3`.
- The core validates phrase version, length, event count, register, duration,
  velocity, and monophonic event ordering.
- The fallback generator provides deterministic local phrases for testing and
  offline use before the coordinator exists.
- Accepted phrases are serialized into plugin state as text.
- The engine schedules note-on/note-off MIDI from host BBT transport and clears
  active notes when playback stops, the phrase is unavailable, or Mute is on.
- The compact UI exposes Channel, Bars, Register, custom range, Density, Risk,
  Humanize, Source, Mute/Open, Generate, Accept, Retry, and connection status.
- `Source=Local` keeps deterministic, token-free generation.
- `Source=Server` posts the current MIDI-derived request to the localhost
  coordinator on Generate/Retry from a background worker and flags `Requesting`,
  `Ready`, `Server Offline`, or `Server Error`.
- Server results are validated, queued internally, and activated at a future bar
  boundary instead of blocking the DAW or replacing the phrase mid-callback.

## Current non-goals

- No network calls in the audio/MIDI processing thread.
- No API keys in plugin state.
- No MCP layer.
- No automatic model calls during playback.
- No automatic model calls during playback; live server calls are explicit
  button actions with one background request in flight.

## Implemented coordinator slice

- `downspout-ai-coordinator generate state.json --out solo.mid` reads a small,
  hand-writable tune-state JSON file.
- The current generator is deterministic and local. It is a placeholder for
  validated model output, not an API client.
- The output path writes a Standard MIDI File that can be imported into a DAW.
- Optional `--phrase phrase.txt` writes Sidecar text phrase state for inspection
  and future plugin import work.

## Implemented state-summary slice

- Ground has `summarizeGroundAiState(...)`, producing host-neutral JSON with
  key, scale, style, form/phrase position, current phrase role, meter, shaping
  controls, and bounded guide bass events.
- Cadence has `summarizeCadenceAiState(...)`, producing host-neutral JSON with
  key, scale, chord size, color/spread/arpeggio settings, progression readiness,
  and bounded chord slots with roots, qualities, and notes.
- These helpers are core-library functions with deterministic tests and no
  change to current plugin behavior. They are optional diagnostics/export
  helpers, not the primary AI input path.

## MIDI-first coordinator direction

The musical material for Sidecar should come from MIDI routed to it in the DAW.
The coordinator should derive tune context from captured MIDI notes, timing,
density, register, and recent harmonic/bass movement before it uses any
plugin-specific summaries.

This keeps the architecture modular:

- no direct dependency from Sidecar to Ground, Cadence, BassGen, or DrumGen;
- no behavior changes required in existing plugins;
- any plugin, hardware controller, or hand-played MIDI track can provide source
  material;
- optional plugin summaries can still be used later as hints if they remain
  passive and do not affect current plugin behavior.

## Implemented MIDI context slice

- `downspout-ai-coordinator generate-from-midi source.mid --out solo.mid`
  analyzes a Standard MIDI File.
- `downspout-ai-coordinator analyze-midi source.mid --out state.json` exports
  the inferred context as hand-editable JSON.
- The analyzer derives pitch-class guides, approximate key/scale, register,
  density, phrase length, and seed from note events.
- The deterministic phrase generator uses guide pitch classes from the analyzed
  material when choosing phrase targets.

## Implemented response-validation slice

- `downspout-ai-coordinator render-response response.json --out solo.mid`
  validates a model-style phrase response from a local JSON file and renders it.
- The response path rejects invalid timing, overlap, register, note, duration,
  and velocity through Sidecar phrase validation before writing MIDI.

## Implemented request-builder slice

- `downspout-ai-coordinator build-request state.json --out request.json` writes a
  provider-neutral solo-generation request.
- `downspout-ai-coordinator build-request-from-midi source.mid --out request.json`
  derives MIDI context first, then writes the request.
- The request includes protocol name, instructions, tune state, guide pitch
  classes, and a response schema. It does not call any remote API.

## Implemented API client slice

- `downspout-ai-coordinator openai state.json --out solo.mid` calls OpenAI from
  the coordinator process, never from the plugin.
- `downspout-ai-coordinator openai-from-midi source.mid --out solo.mid` derives
  MIDI context first, then calls OpenAI.
- The API key is loaded from `OPENAI_API_KEY` or `.env` and is not printed.
- `downspout-ai-coordinator health` reads `.env` on startup and reports whether
  the OpenAI endpoint is configured.
- `downspout-ai-coordinator serve --port 37371` exposes localhost
  `GET /health`, `POST /generate`, and `POST /openai` for Sidecar Server mode.
- API responses that validate but collapse to too few distinct pitches are
  repaired with deterministic scale/guide-aware substitute notes before MIDI or
  live JSON is emitted.
- The raw response can be saved with `--raw`, but MIDI is written only after the
  extracted phrase JSON validates through Sidecar's phrase protocol.
- `exercise-sidecar-ai.sh` keeps this path opt-in through
  `DOWNSPOUT_RUN_OPENAI=1` so routine local checks do not spend quota.
