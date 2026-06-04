# Sidecar

Sidecar is a Downspout MIDI phrase player for generated solo material. It sits
beside the deterministic ensemble plugins, captures routed MIDI as context, and
plays validated phrases against host BBT transport.

API-backed generation is handled by the separate
`downspout-ai-coordinator` process. Sidecar never owns the API key and never
makes network calls from the audio/MIDI callback.

Model-facing prompt text is read from
[`tools/ai-coordinator/prompts/solo-system.txt`](../../tools/ai-coordinator/prompts/solo-system.txt).
The broader protocol rules are documented in
[`docs/ai-solo-protocol.md`](../../docs/ai-solo-protocol.md).

## Modes

- `Source=Local`: token-free deterministic phrase generation from Sidecar's UI
  controls and any captured MIDI context.
- `Source=Server`: queues a background request to
  `http://127.0.0.1:37371/openai`, receives validated phrase JSON from the
  coordinator, and switches to that phrase at a future bar boundary.

Connection status:

- `Local`: local deterministic mode.
- `Requesting`: coordinator call is running in the background.
- `Ready`: validated server phrase has been received and queued.
- `Server Offline`: Sidecar could not connect to the coordinator.
- `Server Error`: the coordinator rejected or failed the request.

## Controls

- `Channel`: output MIDI channel.
- `Bars`: phrase length.
- `Register`, `Low Note`, `High Note`: output pitch range.
- `Density`: event density.
- `Risk`: wider leaps and more surprising notes.
- `Humanize`: reserved for timing/velocity shaping.
- `Output`: open or muted.
- `Generate`: request or create a phrase.
- `Play`: arm the current ready phrase for the next bar boundary.
- `Retry`: request or create an alternate phrase.
- `Accept`: store the current phrase in plugin state.

Sidecar does not pass routed input MIDI through to its output. Input notes are
used only as generation context.

## Live Server Use

Start the coordinator from the repository root:

```bash
build/tools/ai-coordinator/downspout-ai-coordinator health
build/tools/ai-coordinator/downspout-ai-coordinator serve --port 37371
```

`health` reads `.env` or `OPENAI_API_KEY` and reports whether the key is
configured without printing it.

For debugging generated solos, run:

```bash
build/tools/ai-coordinator/downspout-ai-coordinator serve --port 37371 --debug
```

This prints the Sidecar request, OpenAI payload/response, and final phrase JSON
returned to the plugin. It does not print the API key.

In the DAW:

1. Add Sidecar to a MIDI track.
2. Route Sidecar output to a synth or instrument track.
3. Route MIDI from Ground, BassGen, Cadence, MelGen, a MIDI clip, or a keyboard
   into Sidecar.
4. Set `Source` to `Server`.
5. Press play, let Sidecar capture a few notes, then click `Generate` or
   `Retry`.
6. Wait for `Ready`; the phrase will start on the next bar boundary. Press
   `Play` to re-arm the current ready phrase.

Use `Source=Local` for token-free testing.

## File Workflows

Run the local exercise script:

```bash
./exercise-sidecar-ai.sh
```

It builds Sidecar and the coordinator, runs tests, and writes MIDI/phrase files
under `/tmp/downspout-sidecar-exercise/`.

To include OpenAI-backed file generation:

```bash
DOWNSPOUT_RUN_OPENAI=1 ./exercise-sidecar-ai.sh
```

Useful coordinator commands:

```bash
build/tools/ai-coordinator/downspout-ai-coordinator generate tools/ai-coordinator/examples/state.json --out /tmp/solo.mid --phrase /tmp/phrase.txt
build/tools/ai-coordinator/downspout-ai-coordinator generate-from-midi /tmp/source.mid --out /tmp/solo.mid --phrase /tmp/phrase.txt
build/tools/ai-coordinator/downspout-ai-coordinator openai /tmp/state.json --out /tmp/solo.mid --phrase /tmp/phrase.txt --raw /tmp/raw.json
build/tools/ai-coordinator/downspout-ai-coordinator openai-from-midi /tmp/source.mid --out /tmp/solo.mid --phrase /tmp/phrase.txt --raw /tmp/raw.json
```

## Current Limits

- Only one Server request is in flight at a time.
- Saved `phrase.txt` files can be produced by the coordinator, but Sidecar does
  not yet import those files through a UI file picker.
- Host/UI status display may lag output parameter updates, but phrase queuing
  and playback happen inside the plugin once the worker returns.
