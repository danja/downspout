# Downspout AI Coordinator

`downspout-ai-coordinator` is the out-of-plugin process for AI-assisted phrase
generation. It can run as a CLI for file workflows or as a localhost server for
Sidecar's live `Server` source mode.

The human-readable model contract is documented in
`docs/ai-solo-protocol.md`. The coordinator embeds those rules in the
`downspout.ai_solo.v1` request JSON sent to the model.

## Command

Check startup configuration without spending API tokens:

```bash
downspout-ai-coordinator health
```

Run the live localhost server used by Sidecar:

```bash
downspout-ai-coordinator serve --port 37371
```

The server exposes:

- `GET /health`: returns `ok` and whether an OpenAI key is configured.
- `POST /generate`: deterministic local phrase JSON, useful for smoke tests.
- `POST /openai`: OpenAI phrase JSON after Sidecar validation/constraining.

`serve` reads `.env` on startup, prints whether the key is configured, and logs
rejected `/openai` requests. It never prints the API key.

```bash
downspout-ai-coordinator generate tools/ai-coordinator/examples/state.json --out /tmp/solo.mid --phrase /tmp/phrase.txt
```

Or derive the request from an existing MIDI clip:

```bash
downspout-ai-coordinator generate-from-midi /tmp/source.mid --out /tmp/solo.mid --phrase /tmp/phrase.txt
```

Export inferred MIDI context as JSON:

```bash
downspout-ai-coordinator analyze-midi /tmp/source.mid --out /tmp/state.json
```

Build a provider-neutral request JSON file for a future model call:

```bash
downspout-ai-coordinator build-request /tmp/state.json --out /tmp/request.json
downspout-ai-coordinator build-request-from-midi /tmp/source.mid --out /tmp/request.json
```

Render a validated phrase-response JSON file:

```bash
downspout-ai-coordinator render-response tools/ai-coordinator/examples/response.json --out /tmp/solo.mid --phrase /tmp/phrase.txt
```

Call OpenAI from the coordinator:

```bash
downspout-ai-coordinator openai /tmp/state.json --out /tmp/solo.mid --phrase /tmp/phrase.txt --raw /tmp/raw-response.json
downspout-ai-coordinator openai-from-midi /tmp/source.mid --out /tmp/solo.mid --phrase /tmp/phrase.txt --raw /tmp/raw-response.json
```

The API key is read on startup from `OPENAI_API_KEY` or from `.env`. The
coordinator does not print the key. Set `DOWNSPOUT_OPENAI_MODEL` to override
the default model. The repository exercise script keeps this path opt-in:

```bash
DOWNSPOUT_RUN_OPENAI=1 ./exercise-sidecar-ai.sh
```

The `--phrase` output is optional. It writes Sidecar's text phrase state so the
same phrase can be inspected or used in future plugin import work.

## State Format

The first JSON shape is intentionally small and hand-writable:

```json
{
  "key": "D",
  "scale": "dorian",
  "genre": "jazz",
  "tempo": 132,
  "bars": 4,
  "beats_per_bar": 4,
  "channel": 1,
  "register_low": 60,
  "register_high": 84,
  "density": 0.62,
  "risk": 0.38,
  "seed": 7
}
```

The OpenAI path stays outside the audio/MIDI processing thread. It writes MIDI
or returns live phrase JSON only after the returned text has been parsed as a
phrase response and validated by the Sidecar protocol.

If a model response is valid but collapses to too few distinct pitches, the
coordinator repairs the pitch contour with deterministic, scale/guide-aware
substitute notes before returning it to Sidecar.

## MIDI-First Context

The coordinator should primarily derive tune context from MIDI routed to
Sidecar or captured into a local request file. That keeps the system modular:
any existing plugin, external sequencer, hardware input, or manually played
track can become source material without changing the source plugin.

Useful derived context:

- pitch-class histogram for key/scale hints;
- note density and rests for phrase density;
- register range for solo bounds;
- recent bass/chord tones for guide targets;
- bar/beat positions for call-and-response timing.

The `generate-from-midi` command implements the first offline version of this:
it reads a Standard MIDI File, derives key/register/density/guide pitch classes,
and uses those values to guide the deterministic phrase generator.

## Optional State Summary Helpers

Ground and Cadence now expose core-library summary helpers:

- `downspout::ground::summarizeGroundAiState(...)`
- `downspout::cadence::summarizeCadenceAiState(...)`

They produce compact JSON fragments for the coordinator: Ground supplies form,
phrase-role, meter, and guide-bass context; Cadence supplies learned harmony,
voicing, and chord-quality context. These are optional diagnostics/hints and
are not required for the MIDI-first workflow.
