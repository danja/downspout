# AI API solo generation

This note sketches how Downspout could add AI-assisted soloing without trying
to force improvised melody into the existing deterministic generator model.

The core idea is to keep plugin audio/MIDI processing deterministic and
real-time safe, then move HTTP/API work into a separate local coordinator.
Sidecar should publish bounded MIDI material routed to it in the DAW; the
coordinator derives tune context from that MIDI, asks a model for a structured
solo phrase, and validates the result before converting it back into scheduled
MIDI.

## Why a coordinator

Network calls must not happen in a plugin audio callback. Even a fast HTTP
request is too slow and unreliable for a real-time process block, and API
timeouts, rate limits, credentials, and retries should not live inside plugin
DSP code.

A safer architecture is:

```text
Downspout VST plugin client(s)
  -> local coordinator protocol
  -> local AI coordinator process
  -> HTTPS API call
  -> structured phrase response
  -> queued MIDI phrase back to plugin client or DAW track
```

The coordinator could be a small local process, for example:

- a CLI/service launched by the user;
- a localhost HTTP/WebSocket server;
- later, an optional DAW helper script if the host exposes enough routing.

The VST3 plugins should remain usable with no coordinator running.

## Client, coordinator, remote server

The practical system has three distinct roles.

### VST plugin clients

The primary plugin client is Sidecar. Other plugins do not need to change just
to participate; their MIDI output can be routed to Sidecar in the DAW. A plugin
client should only do local, bounded work:

- publish short quantized event windows;
- request or receive a generated phrase;
- queue already-validated MIDI at musical boundaries;
- report UI status such as disconnected, waiting, ready, failed.

They should not own API keys, make remote HTTP calls, retry failed API
requests, or parse untrusted model output in the audio callback.

### Local coordinator

The coordinator is the local non-realtime process. It owns:

- collecting bounded MIDI windows from Sidecar or other plugin clients;
- deriving one tune description from notes, timing, register, density, and
  recent harmonic/bass movement;
- deciding when enough context is available to request a phrase;
- calling the OpenAI API;
- validating and post-processing the returned phrase;
- caching accepted phrases by state hash;
- handing MIDI back to a plugin, DAW script, or MIDI file writer.

This coordinator is the right place for credentials, model selection, prompt
templates, retry policy, and JSON schema validation.

### Remote server

The remote server is the OpenAI API. It should see only the compact tune-state
request and return structured data. The remote model should not be asked to
control the DAW directly or stream unvalidated MIDI into the host.

## Recommended minimal architecture

Do not use MCP for the first implementation. It adds a tool layer before the
basic musical protocol is proven. The simplest modular architecture is:

```text
VST plugin clients <-> localhost coordinator <-> OpenAI API
                                |
                                +-> MIDI file / queued phrase cache
```

Use three small protocol surfaces:

1. Plugin-to-coordinator state messages.
2. Coordinator-to-OpenAI structured solo requests.
3. Coordinator-to-plugin phrase delivery messages.

This keeps the hard boundaries clear:

- plugins are realtime-safe clients;
- the coordinator owns all blocking work;
- OpenAI is the remote phrase generator;
- generated phrases are validated before they enter the DAW.

## Modules

### `ai_protocol`

A small shared C++ header or generated schema package that defines:

- tune-state JSON shape;
- solo-response JSON shape;
- validation limits;
- version strings such as `downspout.ai_solo.v1`.

This module should have no DPF, HTTP, or OpenAI dependency.

### Plugin client module

A small reusable client library used by any plugin that wants AI interaction.
It should handle:

- plugin instance ID;
- nonblocking connection to localhost;
- state publishing at low priority;
- phrase receive queue;
- status flags for the UI.

It must never block the audio thread. A worker thread can communicate with the
audio thread using a lock-free or bounded queue.

### Coordinator process

A standalone executable, for example `downspout-ai-coordinator`. It should
handle:

- localhost HTTP or WebSocket server for plugin clients;
- current state cache for all connected plugin instances;
- prompt construction;
- OpenAI API calls;
- JSON/schema validation;
- deterministic post-processing;
- MIDI file export;
- phrase delivery back to a plugin when live queueing is enabled.

The first coordinator can start as a CLI:

```text
downspout-ai-coordinator generate state.json --out solo.mid
```

The current implementation now includes that CLI and a first localhost server
mode:

```text
downspout-ai-coordinator health
downspout-ai-coordinator serve --port 37371
```

`health` and `serve` load `.env` on startup. `serve` exposes `GET /health`,
`POST /generate`, and `POST /openai`.

### Sidecar plugin

`Sidecar` is a dedicated MIDI plugin that queues and plays validated phrases.
It has a deterministic fallback phrase generator, accepted phrase state,
transport-aware playback, routed MIDI capture, and a compact UI. Its `Source`
control selects token-free `Local` generation or explicit `Server` requests to
the localhost coordinator on Generate/Retry. Network work remains outside the
audio/MIDI processing thread.

Sidecar does not need to know how Ground, BassGen, or Cadence work internally.
It can rely on the coordinator's merged tune state once the live request path
exists.

## Local transport choice

For the first live version, use localhost HTTP with JSON.

It is not the lowest-latency option, but it is simple, debuggable, and good
enough because phrase generation is lookahead-based. A plugin client can post
state every beat or phrase, and the coordinator can return phrases for future
bar boundaries.

A WebSocket can come later if bidirectional updates become awkward.

## Naming

`Sidecar` is a good name for the plugin.

It suggests a musical passenger riding alongside the deterministic ensemble,
which fits the job better than a literal "AI Soloist" name. It also avoids
promising that the plugin is always a lead instrument: Sidecar could generate
jazz solos, answer phrases, fills, or counter-lines depending on the request.

Recommended names:

- Plugin: `Sidecar`
- Bundle: `sidecar.vst3`
- CMake target: `sidecar`
- Build option: `DOWNSPOUT_BUILD_SIDECAR`
- Local process: `downspout-ai-coordinator`
- Protocol namespace: `downspout.ai_solo.v1`

Avoid using "sidecar" as the name of the local process once the plugin is
called Sidecar. Use "coordinator" for the process to keep the roles clear.

## Implementation paths

The repo already has a consistent plugin pattern:

```text
plugins/<name>/
  CMakeLists.txt
  README.md
  include/
  src/
    dpf/
      DistrhoPluginInfo.h
      <Name>Plugin.cpp
      <Name>UI.cpp
  tests/
```

Sidecar can follow that shape, but the implementation should be split into
three repo areas.

### Protocol package

Suggested path:

```text
include/downspout/ai_protocol.hpp
src/common/ai_protocol.cpp
tests/ai_protocol_tests.cpp
```

Responsibilities:

- JSON request and response structures;
- validation of notes, beats, durations, register, and monophonic overlap;
- version constants;
- state-hash helper for caching.

This package should not know about DPF, OpenAI, HTTP, or any specific plugin.
It is the stable contract between Sidecar and the coordinator.

The first implementation starts locally in Sidecar:

```text
plugins/sidecar/include/sidecar_protocol.hpp
plugins/sidecar/src/sidecar_protocol.cpp
plugins/sidecar/tests/sidecar_core_tests.cpp
```

Move it to shared code after Ground, BassGen, and Cadence start using the same
request/response validation.

### Coordinator executable

Suggested path:

```text
tools/ai-coordinator/
  CMakeLists.txt
  README.md
  src/main.cpp
  src/openai_client.cpp
  src/solo_prompt.cpp
  src/midi_file_writer.cpp
  tests/
```

Responsibilities:

- read `state.json`;
- generate deterministic placeholder phrases for now;
- call the OpenAI API;
- validate the structured response through `ai_protocol`;
- write `solo.mid`;
- run a localhost server for live Sidecar requests.

First useful command:

```text
downspout-ai-coordinator generate state.json --out solo.mid
```

Live commands:

```text
downspout-ai-coordinator health
downspout-ai-coordinator serve --port 37371
```

The coordinator should read the API key from the environment or a user config
outside the repo. It should never be stored in plugin state.

### Sidecar plugin

Suggested path:

```text
plugins/sidecar/
  CMakeLists.txt
  README.md
  include/sidecar_engine.hpp
  include/sidecar_params.hpp
  src/sidecar_engine.cpp
  src/dpf/DistrhoPluginInfo.h
  src/dpf/SidecarPlugin.cpp
  src/dpf/SidecarUI.cpp
  tests/sidecar_core_tests.cpp
```

Responsibilities:

- expose UI controls for `Generate`, `Accept`, `Retry`, `Mute`, `Bars`,
  `Density`, `Risk`, `Register`, and `Humanize`;
- publish requested solo constraints to the coordinator later;
- receive already-validated phrases later;
- queue phrase MIDI at bar/phrase boundaries;
- keep accepted phrases in plugin state so saved sessions remain reproducible.

Sidecar should not need direct knowledge of Ground, BassGen, or Cadence. It can
consume merged tune state from the coordinator. That keeps the first plugin
small: MIDI scheduling, request state, UI, and state persistence.

### Optional State Providers

Sidecar should not require state providers from other plugins. Its main source
material is whatever MIDI the DAW routes to it. Still, passive state-summary
helpers can be useful for diagnostics or later optional hints.

The first two optional helpers are:

```text
plugins/ground/include/ground_ai_state.hpp
plugins/cadence/include/cadence_ai_state.hpp
```

Further helpers could follow the same shape, but they are not required for the
MIDI-first workflow:

```text
plugins/bassgen/include/bassgen_ai_state.hpp
plugins/drumgen/include/drumgen_ai_state.hpp
```

Each helper produces a compact, host-neutral description of the plugin's current
musical role:

- Ground: form position, phrase role, root/scale, guide bass events.
- Cadence: learned harmony, chord size, color, current voicing style.
- BassGen: genre, scale, color, response mode, recent bass events.
- DrumGen: genre, density, pulse/fill feel.

Do not wire these directly to Sidecar as a requirement. Let the coordinator
derive context from MIDI first. The helpers should remain deterministic,
passive, and behavior-neutral.

## First implementation path

1. Add `plugins/sidecar` as a minimal MIDI generator with deterministic phrase
   playback from an in-memory phrase.
2. Add `sidecar_protocol` locally inside the Sidecar plugin with tests.
3. Add accepted phrase state serialization to Sidecar.
4. Add Sidecar UI controls for phrase constraints and Generate/Accept/Retry.
5. Add `tools/ai-coordinator generate state.json --out solo.mid`.
6. Make the coordinator validate AI output with the same protocol tests.
7. Add a manual workflow: export or hand-write `state.json`, generate
   `solo.mid`, import into the DAW.
8. Add MIDI-derived context extraction from Sidecar/coordinator input.
9. Add localhost request/response between Sidecar and the coordinator. Done:
   Sidecar `Source=Server` posts to `/openai` on Generate/Retry and flags
   `Server OK`, `Server Offline`, or `Server Error`.
10. Use passive plugin summaries only as optional hints or diagnostics.

The first offline MIDI-derived path exists as:

```text
downspout-ai-coordinator generate-from-midi source.mid --out solo.mid
```

Sidecar also accepts routed MIDI input now. Locally, Generate/Retry can use the
captured note range, density, and event seed as hints without passing input MIDI
through or changing any other plugin.

## OpenAI API fit

The OpenAI Responses API is the likely API surface for this because current
OpenAI model docs describe recent models as available through Responses, and
model comparison docs list structured outputs and function calling support for
many relevant models. For this project, the important capability is not chat as
such; it is asking for a strict, machine-parseable phrase result.

Relevant current docs:

- OpenAI model overview: <https://developers.openai.com/api/docs/models/overview>
- Model comparison and supported features:
  <https://developers.openai.com/api/docs/models/compare>
- All models list: <https://developers.openai.com/api/docs/models/all>

Model selection should be configurable. The protocol should not depend on a
specific model name.

## Musical scope

The first useful AI feature should be phrase-level solo generation, not
bar-by-bar real-time improvisation.

Good initial target:

- request 2, 4, or 8 bars of solo MIDI;
- generate ahead of playback;
- queue the result for the next phrase boundary;
- let the user regenerate, thin, or mute it.

Avoid at first:

- live note-by-note streaming;
- audio analysis;
- long multi-chorus solos;
- asking the model to infer the whole composition from raw MIDI alone.

The local generators already know useful musical state. The model should see
that state explicitly.

## Tune state protocol

Use JSON for the coordinator boundary. It is portable, inspectable, and fits
structured model output.

Example request:

```json
{
  "protocol": "downspout.ai_solo.v1",
  "request_id": "8fb184b6",
  "transport": {
    "bpm": 128.0,
    "meter": {"numerator": 4, "denominator": 4},
    "bar": 33,
    "phrase_bars": 4,
    "lookahead_bars": 4
  },
  "key": {
    "root_midi": 48,
    "root_name": "C",
    "scale": "Dorian",
    "genre": "Jazz"
  },
  "harmony": [
    {"bar": 0, "symbol": "Dm7", "role": "ii", "scale": "Dorian"},
    {"bar": 1, "symbol": "G7alt", "role": "V", "scale": "Altered"},
    {"bar": 2, "symbol": "Cmaj7", "role": "I", "scale": "Major"},
    {"bar": 3, "symbol": "A7", "role": "turnaround", "scale": "Mixolydian"}
  ],
  "parts": {
    "ground": {
      "channel": 1,
      "role": "form_owner",
      "events": [
        {"beat": 0.0, "note": 36, "duration": 0.75, "velocity": 92},
        {"beat": 1.0, "note": 40, "duration": 0.5, "velocity": 80}
      ]
    },
    "bassgen": {"density": 0.42, "color": 0.7},
    "cadence": {"chord_size": "Sevenths", "color": 0.8}
  },
  "solo": {
    "instrument": "tenor_sax",
    "register": {"min": 55, "max": 82},
    "density": 0.55,
    "risk": 0.35,
    "style_notes": "melodic bebop line, leave space, outline ii-V-I"
  }
}
```

The request should carry enough context for a soloist:

- transport: tempo, meter, current phrase, requested length;
- key/scale/genre;
- harmonic map, if available;
- current generator roles and controls;
- a reduced event list for guide parts;
- desired solo instrument/register/density/risk.

The event list should be quantized and short. Do not send every raw MIDI event
from every plugin unless there is a clear reason.

## Solo response protocol

The response should be strict JSON. The coordinator should validate it before any
MIDI reaches the plugin.

Example response:

```json
{
  "protocol": "downspout.ai_solo.v1",
  "request_id": "8fb184b6",
  "summary": "Four-bar bebop answer with space in bar 2 and altered V color.",
  "events": [
    {"beat": 0.0, "note": 62, "duration": 0.5, "velocity": 86},
    {"beat": 0.5, "note": 65, "duration": 0.25, "velocity": 78},
    {"beat": 0.75, "note": 69, "duration": 0.25, "velocity": 82},
    {"beat": 1.0, "note": 72, "duration": 0.5, "velocity": 88}
  ],
  "annotations": [
    {"bar": 1, "text": "altered dominant enclosure into Cmaj7"}
  ]
}
```

Validation rules:

- beats must be inside the requested lookahead window;
- notes must be within requested register;
- durations must be positive and quantized to supported grids;
- velocity must be 1-127;
- overlapping monophonic notes should be trimmed or rejected unless polyphony
  is explicitly requested;
- output should be clipped or transposed if the model exceeds the register.

## Data sources inside Downspout

Some plugins can already provide useful state:

- Ground: form length, phrase role, root, scale, long bass events.
- BassGen: genre, scale, color, current generated bass events.
- Cadence: learned harmony, chord size, spread, arpeggio, pass state.
- MelGen: phrase contour, scale, improvisation amount.
- Counterpointer: learned source phrase and response settings.
- DrumGen: genre, density, pulse/fill feel.

The missing piece is a shared state summary format. Each plugin could expose a
small host-neutral `describeState()` result, independent of DPF. The coordinator
could combine those descriptions into one tune-state request.

## Where the generated MIDI should land

There are two useful delivery stages.

### Coordinator writes MIDI clips or files

The coordinator generates `.mid` clips that the user imports or that a DAW
script places on a track.

Pros:

- simplest real-time safety story;
- easy to inspect and edit;
- no plugin-to-plugin state bus required initially.

Cons:

- less immediate;
- DAW-specific automation may be needed for a smooth workflow.

This should be the first implementation target.

### Sidecar plugin

Create a new MIDI generator plugin named `Sidecar`.

Pros:

- clean UI and routing;
- owns request/regenerate/accept/mute controls;
- can queue MIDI without modifying other plugins heavily.

Cons:

- needs access to state from other plugins, probably via the coordinator or DAW
  sends;
- initial implementation needs a new plugin shell.

Sidecar should come after the coordinator MIDI-file workflow proves the request
and response protocol.

## UI controls

For the future `Sidecar` plugin:

- `Generate`: request a phrase for the next boundary.
- `Accept`: keep the current generated phrase.
- `Retry`: request a different phrase with the same state.
- `Mute`: suppress output without discarding phrase.
- `Bars`: 2/4/8.
- `Register`: low/mid/high or min/max notes.
- `Density`: how busy the line is.
- `Risk`: inside playing vs outside/altered playing.
- `Humanize`: timing/velocity variation applied locally after validation.
- `Model`: coordinator-selected or explicit.

The plugin should show request state: idle, waiting, ready, queued, failed.

## Prompting approach

The model should be treated like an arranger receiving a lead sheet plus
constraints, not like a raw MIDI transformer.

Useful instruction shape:

- "Generate a monophonic jazz solo phrase."
- "Respect the supplied harmony and register."
- "Leave space; do not fill every subdivision."
- "Prefer chord tones on strong beats."
- "Use approach notes, enclosures, and altered tones where the harmony says so."
- "Return only JSON matching this schema."

The coordinator can add a style system prompt and keep user-facing controls mapped
to concrete musical constraints.

## Failure modes

- Latency: generate ahead and queue only at phrase boundaries.
- Invalid JSON: reject, retry once, then report failure.
- Bad musical output: keep deterministic thinning, clipping, transposition, and
  quantization as local post-processing.
- Cost/rate limits: cache accepted phrases by state hash, expose a manual
  generate button rather than automatic repeated calls.
- Privacy: send only musical state, not project paths or user metadata.
- Offline use: plugin should fall back to deterministic generators.

## Suggested implementation phases

1. Define the JSON request/response schema in a small `docs` or `include`
   contract.
2. Add state-summary helpers to Ground, BassGen, Cadence, and DrumGen.
3. Build a command-line coordinator that accepts a JSON file and writes a
   validated MIDI file.
4. Add a simple prompt and structured-response request to the coordinator.
5. Add DAW-friendly workflow docs: export state, generate, import MIDI.
6. Build the `Sidecar` MIDI plugin once the coordinator protocol is stable.
7. Add optional live queueing through localhost HTTP/WebSocket, still keeping
   all API/network work outside the audio callback. The first HTTP request path
   exists; a background queue can come later if button-triggered synchronous
   requests feel too blocking in a DAW.

## Open questions

- Should harmony come primarily from Cadence, or should Ground/BassGen expose a
  simpler inferred harmonic map?
- Should the model return absolute MIDI notes, scale degrees, or both?
- Is the first target jazz only, or should the protocol allow Fugue/classical
  counter-lines too?
- Should accepted AI phrases become deterministic state so saved sessions do
  not depend on the model being available later?
- What DAW integration path is most practical for the first MIDI-file workflow?
