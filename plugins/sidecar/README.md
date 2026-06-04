# Sidecar

Sidecar is the Downspout MIDI plugin intended to sit beside the deterministic
ensemble plugins and play validated phrase material. The plugin itself remains
local and realtime-safe: it does not call an API from the audio/MIDI thread.
API-backed phrase generation lives in the separate `downspout-ai-coordinator`
CLI/server.

The model-facing solo contract is documented in `docs/ai-solo-protocol.md`.

## Current behavior

- Generates deterministic fallback phrases from the UI constraints.
- Captures routed MIDI note-ons as optional local context for Generate/Retry.
- Stores the accepted phrase in plugin state.
- Plays the phrase against host BBT transport.
- Emits MIDI note-on/note-off events on the selected channel.
- Does not pass routed input MIDI through to the output.
- Clears active notes immediately when transport stops, Mute is enabled, or the
  phrase is unavailable.

## Controls

- `Channel`: output MIDI channel.
- `Bars`: phrase length in bars.
- `Register`: low, mid, high, or custom pitch range.
- `Low Note` / `High Note`: custom register bounds.
- `Density`: number of phrase events.
- `Risk`: chance of wider or more surprising note choices in fallback mode.
- `Humanize`: reserved for timing/velocity shaping.
- `Output`: open or muted.
- `Source`: `Local` uses Sidecar's deterministic generator; `Server` asks the
  localhost coordinator for an OpenAI phrase.
- `Generate`: creates a new deterministic fallback phrase.
- `Accept`: marks the current phrase as accepted for session state.
- `Retry`: creates an alternate deterministic fallback phrase.
- Connection status: `Local`, `Requesting`, `Ready`, `Server Offline`, or
  `Server Error`.

If MIDI has been routed into Sidecar, Generate and Retry use the captured note
range, density, and event seed as hints. If no MIDI has been routed in, the
plugin keeps the original local fallback behavior.

In `Server` source mode, Generate and Retry queue the current captured
MIDI-derived request to `http://127.0.0.1:37371/openai` on a background thread.
The audio/MIDI callback does not make network calls. While the request is in
flight Sidecar silences the old phrase, then adopts the validated phrase at a
future bar boundary after the coordinator responds.

## DAW Test: Live Sidecar

1. Add Sidecar to a MIDI track.
2. Route Sidecar's MIDI output to a synth or instrument track.
3. Route MIDI from Ground, BassGen, Cadence, MelGen, a MIDI clip, or a keyboard
   into the Sidecar track.
4. Press play and let Sidecar receive a few notes.
5. Click `Generate` or `Retry` in Sidecar.
6. Sidecar should produce its own phrase on the output channel. It should not
   pass the incoming MIDI through.
7. Use `Output` to mute/unmute and confirm active notes are cleared cleanly.

What to listen for:

- With no input routed in, Generate/Retry should use the normal local fallback.
- With MIDI routed in first, Generate/Retry should shift toward the captured
  register and density.
- Saving/reopening the DAW project after `Accept` should restore the accepted
  phrase.

## DAW Test: Live Coordinator

Start the coordinator before opening or testing Sidecar in Server mode:

```bash
build/tools/ai-coordinator/downspout-ai-coordinator health
build/tools/ai-coordinator/downspout-ai-coordinator serve --port 37371
```

`health` reads `.env` on startup and reports whether `OPENAI_API_KEY` is
configured without printing the key. The server prints the localhost URL,
whether the OpenAI endpoint is usable, and any rejected `/openai` requests.

In the DAW:

1. Set Sidecar `Source` to `Server`.
2. Route MIDI into Sidecar and let it capture a few notes.
3. Click `Generate` or `Retry`.
4. `Requesting` means the coordinator call is running in the background.
5. `Ready` means a validated phrase has been received and queued for a future
   bar boundary.
6. `Server Offline` means Sidecar could not connect to localhost.
7. `Server Error` means the coordinator answered but rejected or failed the
   request, for example missing API key, invalid state, or invalid model output.

Leave `Source` on `Local` for token-free deterministic testing.

## DAW Test: Coordinator MIDI Files

From the repository root:

```bash
./exercise-sidecar-ai.sh
```

This builds Sidecar and the coordinator, runs tests, and writes local MIDI
outputs under:

```text
/tmp/downspout-sidecar-exercise/
```

Import these files into the DAW to audition the coordinator paths:

- `sidecar-solo.mid`: deterministic phrase from `state.json`.
- `sidecar-derived-from-midi.mid`: phrase derived from the first MIDI file.
- `sidecar-response-render.mid`: validated local response JSON rendered as MIDI.

## DAW Test: OpenAI MIDI Files

If `.env` contains `OPENAI_API_KEY` or the environment variable is set:

```bash
DOWNSPOUT_RUN_OPENAI=1 ./exercise-sidecar-ai.sh
```

Additional files are written under `/tmp/downspout-sidecar-exercise/`:

- `sidecar-openai.mid`: OpenAI phrase generated from `state.json`.
- `sidecar-openai-from-midi.mid`: OpenAI phrase generated from analyzed MIDI
  context.
- matching `*-phrase.txt` files: Sidecar phrase-state text.
- matching `*-raw.json` files: raw API responses for debugging.

The coordinator validates and constrains API output before writing MIDI. If the
model returns a note slightly outside the requested register, the coordinator
folds/clamps it into range, removes overlaps, and validates the final phrase.

## Coordinator Commands

```bash
build/tools/ai-coordinator/downspout-ai-coordinator health
build/tools/ai-coordinator/downspout-ai-coordinator serve --port 37371
build/tools/ai-coordinator/downspout-ai-coordinator generate tools/ai-coordinator/examples/state.json --out /tmp/solo.mid --phrase /tmp/phrase.txt
build/tools/ai-coordinator/downspout-ai-coordinator generate-from-midi /tmp/source.mid --out /tmp/solo.mid --phrase /tmp/phrase.txt
build/tools/ai-coordinator/downspout-ai-coordinator analyze-midi /tmp/source.mid --out /tmp/state.json
build/tools/ai-coordinator/downspout-ai-coordinator build-request /tmp/state.json --out /tmp/request.json
build/tools/ai-coordinator/downspout-ai-coordinator render-response tools/ai-coordinator/examples/response.json --out /tmp/solo.mid --phrase /tmp/phrase.txt
build/tools/ai-coordinator/downspout-ai-coordinator openai /tmp/state.json --out /tmp/solo.mid --phrase /tmp/phrase.txt --raw /tmp/raw.json
build/tools/ai-coordinator/downspout-ai-coordinator openai-from-midi /tmp/source.mid --out /tmp/solo.mid --phrase /tmp/phrase.txt --raw /tmp/raw.json
```

## Current Limits

- Sidecar does not yet import `phrase.txt` directly through the UI.
- Server mode is asynchronous, but only one request is in flight at a time.
  Repeated Generate/Retry clicks are ignored until the current request finishes.
- The UI/host may take a moment to refresh output status parameters, but the
  plugin itself queues and switches phrases internally once the worker returns.
- The plugin never owns or prints the API key.
