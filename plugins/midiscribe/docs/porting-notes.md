# Midiscribe — Porting Notes

## Overview

Midiscribe is a new Downspout plugin (not a port of an existing LV2 plugin).
It is a purely non-destructive MIDI capture utility that records incoming events
to a Standard MIDI File on demand.

## Architecture

```
MidiscribeCore (midiscribe_core.hpp / .cpp)
    |— Controls  (armed, writeTrigger, captureBeatIndex)
    |— buffer_   (std::vector<CapturedEvent>, rolling window)
    |— exportPath_ (from DPF state key "exportPath")
    |— serializeBuffer() → SmfWriter.hpp → std::vector<uint8_t>
    `— writeFile() → std::ofstream

MidiscribePlugin.cpp (DPF wrapper)
    |— getTimePosition() → absoluteBeat, bpm
    |— midiEvents[] → CapturedEvent → core_.processBlock()
    |— pass-through: re-emits every incoming DPF MidiEvent unchanged
    `— write trigger edge → core_.writeFile()

SmfWriter.hpp (header-only)
    `— serializeToSmf(events, bpm, ppq) → std::vector<uint8_t>
```

## Parameter mapping

| ID | Symbol         | Range         | Default | Notes                          |
|----|---------------|---------------|---------|--------------------------------|
| 0  | armed          | bool 0/1      | 0       | Enables capture                |
| 1  | write          | float 0..1    | 0       | Momentary trigger              |
| 2  | capture_beats  | int 0..3      | 1       | Index: 0=8, 1=16, 2=32, 3=64  |

## State keys

| Key          | Default               | Notes                       |
|--------------|-----------------------|-----------------------------|
| exportPath   | /tmp/midiscribe.mid   | Output file path            |
| controls     | (serialised)          | armed + captureBeatIndex    |

## DPF-specific decisions

- `DISTRHO_PLUGIN_IS_RT_SAFE 0`: The write trigger performs blocking file I/O
  (`std::ofstream`). This is intentional — the trigger is meant to be used
  manually, not driven by host automation at audio rates.

- `DISTRHO_PLUGIN_HAS_UI 0`: No custom UI; the host's generic parameter panel
  is sufficient for three parameters.

- `DISTRHO_PLUGIN_NUM_INPUTS/NUM_OUTPUTS 0`: Pure MIDI effect; no audio ports.

- Transport beat computation:
  - When `bbt.valid`, absoluteBeat = (bar-1)*beatsPerBar + (beat-1) + tick/ticksPerBeat.
  - Falls back to frame/sampleRate * defaultBpm if BBT is unavailable.

## SMF format

- Type 0 (single track).
- Division: 480 PPQ (hardcoded in `kSmfPpq`).
- Tempo meta event at tick 0 derived from live transport BPM (or 120 BPM
  default).
- Only 3-byte channel messages (status 0x80..0xEF) are written; 2-byte and
  sysex events are dropped silently.
- Events are sorted by tick before writing; delta times are computed from the
  sorted order.

## Known limitations / TODOs

- TODO: 2-byte MIDI messages (Program Change 0xC0, Channel Pressure 0xD0) are
  currently dropped by the SmfWriter because the CapturedEvent struct always
  stores three bytes and the writer always emits three. If 2-byte pass-through
  is needed, extend CapturedEvent with a size field.
- TODO: Sysex capture is not implemented.
- TODO: Sub-block beat accuracy uses a linear interpolation from the block's
  start beat + event frame offset. Hosts that do not report BBT will fall back
  to a frame-based estimate which drifts if tempo changes mid-session.
- Visual acceptance: no UI — screenshot capture is not applicable.
