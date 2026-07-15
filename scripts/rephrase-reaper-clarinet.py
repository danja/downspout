#!/usr/bin/env python3
"""Replace the Downspout jazz project's clarinet MIDI with an angular line."""

from __future__ import annotations

import argparse
from pathlib import Path


TRACK_NAME = "CLARINET — Canticle Countervoice"
TICKS_PER_SECOND = 1344.0


def track_bounds(lines: list[str]) -> tuple[int, int]:
    for start, line in enumerate(lines):
        if not line.startswith("  <TRACK "):
            continue
        depth, found = 0, False
        for end in range(start, len(lines)):
            stripped = lines[end].lstrip()
            depth += stripped.startswith("<")
            found |= lines[end].startswith("    NAME ") and f'"{TRACK_NAME}"' in lines[end]
            depth -= stripped.startswith(">")
            if depth == 0:
                if found:
                    return start, end + 1
                break
    raise ValueError(f"Track not found: {TRACK_NAME}")


def notes() -> list[tuple[float, float, int, int]]:
    # Irregular, blues-inflected phrases designed to resist a lilting 6/8 reading.
    return [
        (6.82,.19,62,67),(7.13,.42,68,76),(7.72,.13,64,69),(7.97,.57,61,64),
        (8.61,.16,67,73),(8.88,.16,66,70),(9.17,.34,72,81),(9.68,.12,63,68),(9.93,.63,69,74),
        (10.74,.14,58,63),(11.02,.28,65,72),(11.43,.11,71,80),(11.66,.47,60,66),(12.28,.18,64,69),
        (13.07,.13,62,70),(13.29,.13,63,68),(13.52,.31,69,79),(13.98,.15,73,83),(14.25,.54,67,72),
        (14.96,.12,61,66),(15.17,.12,68,78),(15.39,.12,65,72),(15.62,.36,74,86),(16.17,.61,70,78),
        (16.93,.16,57,65),(17.19,.22,64,73),(17.54,.14,70,82),(17.80,.14,69,77),(18.05,.49,76,88),
        (18.77,.12,66,71),(18.97,.12,72,82),(19.18,.12,67,74),(19.39,.12,75,89),(19.61,.29,71,81),
        (20.09,.17,63,69),(20.37,.13,70,80),(20.59,.13,69,76),(20.82,.44,78,91),(21.41,.18,73,82),
        (21.74,.11,65,72),(21.94,.11,71,82),(22.15,.11,68,76),(22.36,.11,77,91),(22.58,.52,72,83),
        (23.31,.15,64,70),(23.56,.15,63,67),(23.83,.38,70,81),(24.37,.16,66,73),(24.66,.68,61,65),
        # A low, spacious ending instead of restating a familiar modal theme.
        (26.08,.23,55,60),(26.44,.51,62,69),(27.18,.17,58,63),(27.47,.72,65,72),
        (28.46,.14,59,64),(28.70,.14,66,73),(28.96,.43,60,66),(29.61,.76,68,75),
        (30.58,.18,57,61),(30.88,.31,64,69),(31.36,.14,61,65),(31.62,.82,67,73),
        (32.68,.19,63,66),(33.00,.27,58,62),(33.43,.62,62,59),
    ]


def event_lines() -> list[str]:
    events = []
    for start, duration, pitch, velocity in notes():
        on = round(start * TICKS_PER_SECOND)
        off = round((start + duration) * TICKS_PER_SECOND)
        events.extend([(on, 1, f"90 {pitch:02x} {velocity:02x}"), (off, 0, f"80 {pitch:02x} 00")])
    events.sort(key=lambda event: (event[0], event[1]))
    result, previous = [], 0
    for tick, _, payload in events:
        result.append(f"        E {tick - previous} {payload}\n")
        previous = tick
    end = round(34.28571428571428 * TICKS_PER_SECOND)
    result.append(f"        E {end - previous} b0 7b 00\n")
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    lines = args.source.read_text(encoding="utf-8").splitlines(keepends=True)
    start, end = track_bounds(lines)
    pool = next(i for i in range(start, end) if lines[i].lstrip().startswith("POOLEDEVTS "))
    interp = next(i for i in range(pool + 1, end) if lines[i].lstrip().startswith("CCINTERP "))
    lines[pool + 1:interp] = event_lines()
    args.output.write_text("".join(lines), encoding="utf-8")
    print(f"{args.output} ({len(notes())} notes)")


if __name__ == "__main__":
    main()
