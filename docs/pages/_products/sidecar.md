---
title: Sidecar
order: 105
bundle: sidecar.vst3
kind: MIDI generator
role: AI-ready phrase player
screenshot: /assets/plugins/sidecar.png
summary: MIDI phrase player for generated solo material with deterministic local generation and optional localhost coordinator requests.
---

## Opinion

An experiment I gave up on (in favour of MCP).

## Functionality

Sidecar plays validated solo phrases against host BBT transport. It can generate
token-free local phrases or request material from the separate
`downspout-ai-coordinator` process over localhost.

The plugin never owns API keys and never performs network work from the
audio/MIDI callback. Routed MIDI is used as generation context rather than
passed through.

### Status

Taken to proof-of-concept, and left there.