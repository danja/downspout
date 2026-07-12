#!/usr/bin/env python3
"""Add or replace named-track volume envelopes in a REAPER .RPP project."""

from __future__ import annotations

import argparse
import math
from pathlib import Path


def amplitude(db: float) -> float:
    return 10.0 ** (db / 20.0)


def envelope(points: list[tuple[float, float]], indent: str = "    ") -> list[str]:
    lines = [
        f"{indent}<VOLENV2\n",
        f"{indent}  ACT 1 -1\n",
        f"{indent}  VIS 1 1 1\n",
        f"{indent}  LANEHEIGHT 0 0\n",
        f"{indent}  ARM 0\n",
        f"{indent}  DEFSHAPE 0 -1 -1\n",
    ]
    lines.extend(f"{indent}  PT {time:.12f} {amplitude(db):.12f} 0\n" for time, db in points)
    lines.append(f"{indent}>\n")
    return lines


def track_ranges(lines: list[str]) -> list[tuple[int, int, str]]:
    ranges = []
    i = 0
    while i < len(lines):
        if not lines[i].startswith("  <TRACK "):
            i += 1
            continue
        start, depth, name = i, 0, ""
        while i < len(lines):
            stripped = lines[i].lstrip()
            if stripped.startswith("<"):
                depth += 1
            if stripped.startswith(">"):
                depth -= 1
                if depth == 0:
                    ranges.append((start, i + 1, name))
                    break
            if lines[i].startswith("    NAME "):
                name = lines[i].split('"', 2)[1]
            i += 1
        i += 1
    return ranges


def apply_envelopes(source: Path, output: Path, curves: dict[str, list[tuple[float, float]]]) -> None:
    lines = source.read_text(encoding="utf-8").splitlines(keepends=True)
    ranges = track_ranges(lines)
    found = {name for _, _, name in ranges}
    missing = sorted(set(curves) - found)
    if missing:
        raise ValueError(f"Tracks not found: {', '.join(missing)}")

    for start, end, name in reversed(ranges):
        if name not in curves:
            continue
        # Replace a prior VOLENV2 block if this script is run again.
        env_start = next((i for i in range(start, end) if lines[i].startswith("    <VOLENV2")), None)
        if env_start is not None:
            depth, env_end = 0, env_start
            while env_end < end:
                stripped = lines[env_end].lstrip()
                if stripped.startswith("<"):
                    depth += 1
                if stripped.startswith(">"):
                    depth -= 1
                    if depth == 0:
                        env_end += 1
                        break
                env_end += 1
            lines[env_start:env_end] = envelope(curves[name])
        else:
            insert_at = next(i + 1 for i in range(start, end) if lines[i].startswith("    VOLPAN "))
            lines[insert_at:insert_at] = envelope(curves[name])

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("".join(lines), encoding="utf-8")


DEFAULT_CURVES = {
    "TENOR — Canticle Reed": [(0, -18), (4.20, -18), (4.40, 0), (12.80, -1), (12.95, -2),
                               (19.29, 0), (24.00, 1), (25.71, 1), (33.70, -1), (34.29, -12)],
    "PIANO — Canticle Keys": [(0, -10), (2.14, -6), (4.29, -2), (6.00, 0), (12.86, -2),
                               (17.14, -1), (21.43, 1), (25.71, -1), (30.00, 0), (34.29, -6)],
    "UPRIGHT BASS — Basilico": [(0, -4), (4.29, 0), (12.86, -1), (17.14, 0), (21.43, 1),
                                  (25.71, 0), (33.70, 0), (34.29, -4)],
    "DRUMS — DrumKit": [(0, -8), (2.14, -5), (4.29, -1), (12.86, 0), (17.14, 1),
                          (21.43, 2), (25.71, 0), (33.70, -1), (34.29, -8)],
    "ROOM — Reverb Bus": [(0, -4), (4.29, -1), (12.86, 0), (21.43, 1), (25.71, -1),
                            (33.70, -2), (34.29, -10)],
}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    apply_envelopes(args.source, args.output, DEFAULT_CURVES)
    print(args.output)
