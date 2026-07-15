#!/usr/bin/env python3
"""Revoice a named Canticle track and replace its MIDI with an original clarinet line."""

from __future__ import annotations

import argparse
import base64
from pathlib import Path


TRACK_NAME = "HORN II — Canticle Reed Countervoice"
OUTPUT_TRACK_NAME = "CLARINET — Canticle Countervoice"
TICKS_PER_SECOND = 1344.0  # REAPER-MCP's 168 BPM MIDI conversion in this project.


def track_bounds(lines: list[str], name: str) -> tuple[int, int]:
    for start, line in enumerate(lines):
        if not line.startswith("  <TRACK "):
            continue
        depth = 0
        found = False
        for end in range(start, len(lines)):
            stripped = lines[end].lstrip()
            depth += stripped.startswith("<")
            if lines[end].startswith("    NAME ") and f'"{name}"' in lines[end]:
                found = True
            depth -= stripped.startswith(">")
            if depth == 0:
                if found:
                    return start, end + 1
                break
    raise ValueError(f"Track not found: {name}")


def replace_state_value(blob: bytes, key: str, value: str) -> bytes:
    marker = key.encode() + b"\0"
    start = blob.index(marker) + len(marker)
    end = blob.index(b"\0", start)
    old = blob[start:end]
    new = value.encode()
    if len(new) != len(old):
        raise ValueError(f"State replacement for {key} must remain {len(old)} bytes")
    return blob[:start] + new + blob[end:]


def revoice_canticle(lines: list[str], start: int, end: int) -> None:
    vst = next(i for i in range(start, end) if lines[i].startswith('      <VST "VST3i: Canticle'))
    close = next(i for i in range(vst + 1, end) if lines[i].startswith("      >"))
    encoded = [lines[i].strip() for i in range(vst + 1, close)]
    raw_chunks = [base64.b64decode(chunk) for chunk in encoded]
    host, state = raw_chunks[0], b"".join(raw_chunks[1:])
    changes = {
        "model": "1",
        "tone": "0.379999980927",
        "body": "0.719999983311",
        "movement": "0.10000000298",
        "attack": "0.06000000149",
        "decay": "0.280000003576",
        "sustain": "0.73999997139",
        "release": "0.319999986887",
        "detune": "0.020000007153",
        "width": "0.220000004768",
        "drive": "0.04000000149",
        "output": "0.600000007153",
        "articulation": "1",
    }
    for key, value in changes.items():
        state = replace_state_value(state, key, value)
    host_line = "        " + base64.b64encode(host).decode() + "\n"
    state_text = base64.b64encode(state).decode()
    state_lines = ["        " + state_text[i:i + 120] + "\n" for i in range(0, len(state_text), 120)]
    lines[vst + 1:close] = [host_line, *state_lines]


def clarinet_notes() -> list[tuple[float, float, int, int]]:
    # (start seconds, duration seconds, pitch, velocity): original, hand-shaped phrases.
    phrases = [
        # Oblique answers during the head.
        [(7.05,.34,67,68),(7.48,.18,73,76),(7.80,.52,69,71)],
        [(8.92,.20,63,66),(9.22,.28,70,78),(9.62,.18,66,72),(9.91,.48,74,80)],
        [(11.02,.16,61,65),(11.29,.16,64,69),(11.55,.38,72,79),(12.08,.22,68,72),(12.39,.34,65,68)],
        # Uneven developmental phrases with breathing space and chromatic approaches.
        [(13.02,.18,62,72),(13.27,.18,68,79),(13.55,.14,71,75),(13.78,.42,65,70)],
        [(14.35,.15,64,71),(14.58,.15,63,68),(14.81,.22,69,81),(15.14,.14,76,86),(15.40,.46,72,77)],
        [(16.04,.30,67,73),(16.48,.14,74,82),(16.70,.14,70,76),(16.92,.14,77,88),(17.18,.36,73,80)],
        [(17.82,.14,66,72),(18.04,.14,69,76),(18.26,.14,75,84),(18.49,.14,72,78),(18.74,.14,80,91),(19.02,.44,76,83)],
        [(19.73,.18,65,70),(20.01,.18,71,79),(20.29,.12,70,74),(20.48,.12,77,88),(20.68,.12,73,80),(20.90,.38,82,94)],
        [(21.52,.12,74,78),(21.70,.12,81,90),(21.88,.12,78,84),(22.06,.12,85,96),(22.28,.24,80,89),(22.62,.48,76,81)],
        [(23.28,.14,83,92),(23.50,.14,79,85),(23.72,.14,72,76),(23.94,.22,77,84),(24.28,.16,73,77),(24.54,.46,68,70)],
        # Final counter-melody: lower, more lyrical, and unlike the returning head.
        [(26.02,.48,57,64),(26.61,.22,64,72),(26.95,.52,60,67)],
        [(27.72,.30,62,68),(28.12,.18,68,76),(28.40,.62,65,70)],
        [(29.12,.22,58,63),(29.44,.22,66,72),(29.78,.18,63,68),(30.08,.54,69,76)],
        [(30.92,.18,61,64),(31.20,.18,67,72),(31.48,.18,64,68),(31.78,.64,72,78)],
        [(32.65,.24,70,70),(33.00,.24,65,66),(33.36,.72,62,61)],
    ]
    return [note for phrase in phrases for note in phrase]


def midi_events(notes: list[tuple[float, float, int, int]]) -> list[str]:
    events = []
    for start, duration, pitch, velocity in notes:
        on = round(start * TICKS_PER_SECOND)
        off = round((start + duration) * TICKS_PER_SECOND)
        events.extend([(on, 1, f"90 {pitch:02x} {velocity:02x}"), (off, 0, f"80 {pitch:02x} 00")])
    events.sort(key=lambda event: (event[0], event[1]))
    out, previous = [], 0
    for tick, _, payload in events:
        out.append(f"        E {tick - previous} {payload}\n")
        previous = tick
    end_tick = round(34.28571428571428 * TICKS_PER_SECOND)
    out.append(f"        E {max(0, end_tick - previous)} b0 7b 00\n")
    return out


def replace_midi(lines: list[str], start: int, end: int) -> int:
    pool = next(i for i in range(start, end) if lines[i].lstrip().startswith("POOLEDEVTS "))
    interp = next(i for i in range(pool + 1, end) if lines[i].lstrip().startswith("CCINTERP "))
    notes = clarinet_notes()
    lines[pool + 1:interp] = midi_events(notes)
    return len(notes)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    lines = args.source.read_text(encoding="utf-8").splitlines(keepends=True)
    start, end = track_bounds(lines, TRACK_NAME)
    revoice_canticle(lines, start, end)
    start, end = track_bounds(lines, TRACK_NAME)
    count = replace_midi(lines, start, end)
    name_line = next(i for i in range(start, end) if lines[i].startswith("    NAME "))
    lines[name_line] = f'    NAME "{OUTPUT_TRACK_NAME}"\n'
    args.output.write_text("".join(lines), encoding="utf-8")
    print(f"{args.output} ({count} notes)")


if __name__ == "__main__":
    main()
