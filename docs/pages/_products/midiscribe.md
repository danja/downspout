---
title: Midiscribe
order: 200
bundle: midiscribe.vst3
kind: Utility
summary: Non-destructive MIDI capture plugin that writes recorded events to a Standard MIDI File (.mid) on demand.
---

## Opinion

As yet untested. Built for use with Transmission.

## Functionality

Midiscribe is a MIDI capture utility for tracking and exporting MIDI performances from within a VST3 host. It passes all incoming MIDI events through unchanged, optionally records them with beat-position timestamps, and writes the captured buffer to a Standard MIDI File when triggered.

## Parameters

| Parameter      | Range        | Default | Description                                       |
|----------------|-------------|---------|---------------------------------------------------|
| Armed          | Off / On     | Off     | Enable capture. Events are recorded to the buffer only when armed. |
| Write          | 0 → 1        | 0       | Momentary trigger. Set to 1 to write the captured buffer to the export path and reset it. |
| Capture Beats  | 8 / 16 / 32 / 64 | 16  | Rolling window length in beats. Older events are pruned automatically. |

## State

The export path is stored in plugin state under the key `exportPath` and defaults to `/tmp/midiscribe.mid`. Change it from the host's plugin state panel or via session recall.

## Signal flow

```
MIDI In → [capture buffer when armed] → MIDI Out (pass-through, unchanged)
                    ↓ (write trigger)
               Standard MIDI File (.mid)
```

## Output format

- Type 0 (single-track) Standard MIDI File
- 480 PPQ resolution
- Tempo event derived from host transport BPM (defaults to 120 BPM)
