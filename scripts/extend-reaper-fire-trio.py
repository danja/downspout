#!/usr/bin/env python3
"""Expand the 24-bar fire trio into a varied three-minute arrangement."""

from __future__ import annotations

import argparse
from collections import defaultdict
from pathlib import Path


PPQ = 960
BASE_BARS = 24
TOTAL_BARS = 114
BASE_TICKS = BASE_BARS * 4 * PPQ
TOTAL_TICKS = TOTAL_BARS * 4 * PPQ
TOTAL_SECONDS = TOTAL_BARS * 4 * 60 / 152


def track_bounds(lines: list[str], name: str) -> tuple[int, int]:
    for start, line in enumerate(lines):
        if not line.startswith("  <TRACK "):
            continue
        depth, found = 0, False
        for end in range(start, len(lines)):
            stripped = lines[end].lstrip()
            depth += stripped.startswith("<")
            found |= lines[end].startswith("    NAME ") and f'"{name}"' in lines[end]
            depth -= stripped.startswith(">")
            if depth == 0:
                if found:
                    return start, end + 1
                break
    raise ValueError(f"Track not found: {name}")


def parse_notes(event_lines: list[str]) -> list[tuple[int, int, int, int, int]]:
    tick = 0
    active: dict[tuple[int, int], list[tuple[int, int]]] = defaultdict(list)
    notes = []
    for line in event_lines:
        parts = line.split()
        if len(parts) < 5 or parts[0] != "E":
            continue
        tick += int(parts[1])
        status, pitch, velocity = (int(value, 16) for value in parts[2:5])
        kind, channel = status & 0xF0, status & 0x0F
        key = channel, pitch
        if kind == 0x90 and velocity:
            active[key].append((tick, velocity))
        elif kind == 0x80 or (kind == 0x90 and velocity == 0):
            if active[key]:
                start, vel = active[key].pop(0)
                notes.append((start, max(1, tick - start), pitch, vel, channel))
    return sorted(notes)


def transform(notes: list[tuple[int, int, int, int, int]], role: str) -> list[tuple[int, int, int, int, int]]:
    output = []
    for cycle in range(4):
        shift = cycle * BASE_TICKS
        for index, (start, duration, pitch, velocity, channel) in enumerate(notes):
            bar = start // (4 * PPQ)
            keep, new_pitch, new_velocity, delay = True, pitch, velocity, 0
            if cycle == 1:
                if role == "sax":
                    new_pitch = min(81, pitch + 2)
                    delay = 90 if bar % 3 == 1 else 0
                elif role == "bass":
                    new_pitch = pitch + (5 if bar % 4 == 2 else 0)
                elif role == "drums":
                    new_velocity = min(118, velocity + ((index * 7) % 13) - 3)
                elif role == "organ":
                    new_pitch = min(84, pitch + 2)
                    keep = index % 5 != 0
            elif cycle == 2:
                if role == "sax":
                    new_pitch = max(49, min(81, pitch + (-3 if bar < 12 else 5)))
                    keep = index % 6 != 0
                elif role == "bass":
                    new_pitch = min(62, pitch + (12 if index % 7 == 0 else 0))
                    delay = 60 if bar % 2 else 0
                elif role == "drums":
                    new_velocity = min(122, velocity + 7)
                elif role == "organ":
                    new_pitch = pitch + (12 if pitch < 65 else -5 if pitch > 73 else 0)
            elif cycle == 3:
                if role == "sax":
                    keep = index % 3 == 0
                    new_pitch = max(49, pitch - 5)
                    duration = round(duration * 1.45)
                elif role == "bass":
                    keep = index % 2 == 0
                    new_pitch = max(24, pitch - (12 if index % 5 == 0 else 0))
                    duration = round(duration * 1.35)
                elif role == "drums":
                    keep = pitch in {42, 45, 47, 49, 50, 51} and index % 2 == 0
                    new_velocity = max(38, velocity - 13)
                elif role == "organ":
                    keep = index % 3 == 0
                    duration = round(duration * 1.8)
                    new_velocity = max(28, velocity - 9)
            if keep:
                output.append((start + shift + delay, duration, new_pitch, new_velocity, channel))
                if cycle == 2 and role == "drums" and index % 4 == 0:
                    output.append((start + shift + 120, max(30, duration // 2), new_pitch, max(35, new_velocity - 28), channel))

    # Eighteen-bar coda derived from the opening, thinning toward silence.
    coda_shift = 4 * BASE_TICKS
    cutoff = 18 * 4 * PPQ
    for index, (start, duration, pitch, velocity, channel) in enumerate(notes):
        if start >= cutoff:
            continue
        bar = start // (4 * PPQ)
        thinning = 2 if bar < 6 else 3 if bar < 12 else 5
        if index % thinning:
            continue
        if role == "sax":
            pitch = max(49, pitch - 3)
        elif role == "bass" and index % 4 == 0:
            pitch = max(24, pitch - 12)
        elif role == "organ":
            duration = round(duration * 1.6)
        velocity = max(26, velocity - 12 - bar)
        output.append((start + coda_shift, duration, pitch, velocity, channel))
    return sorted(output)


def encode(notes: list[tuple[int, int, int, int, int]]) -> list[str]:
    events = []
    for start, duration, pitch, velocity, channel in notes:
        events.append((start, 1, f"{0x90 | channel:02x} {pitch:02x} {velocity:02x}"))
        events.append((min(TOTAL_TICKS - 1, start + duration), 0, f"{0x80 | channel:02x} {pitch:02x} 00"))
    events.sort(key=lambda event: (event[0], event[1]))
    result, previous = [], 0
    for tick, _, payload in events:
        result.append(f"        E {tick - previous} {payload}\n")
        previous = tick
    result.append(f"        E {TOTAL_TICKS - previous} b0 7b 00\n")
    return result


def replace_envelope(block: list[str], role: str) -> None:
    curves = {
        "sax": [(0,-7),(37.9,-5),(75.8,-3),(113.7,-10),(151.6,-7),(170,-11),(180,-24)],
        "bass": [(0,-7.5),(37.9,-6),(75.8,-4),(113.7,-8),(151.6,-7),(170,-12),(180,-24)],
        "drums": [(0,-8),(37.9,-6),(75.8,-3),(113.7,-9),(151.6,-7),(170,-13),(180,-26)],
        "organ": [(0,-17),(37.9,-15),(75.8,-13),(113.7,-20),(151.6,-16),(170,-21),(180,-30)],
    }
    env = next(i for i, line in enumerate(block) if line.startswith("    <VOLENV2"))
    close = next(i for i in range(env + 1, len(block)) if block[i].startswith("    >"))
    first = next(i for i in range(env, close) if block[i].lstrip().startswith("PT "))
    last = max(i for i in range(env, close) if block[i].lstrip().startswith("PT ")) + 1
    import math
    block[first:last] = [f"      PT {time:.8f} {10 ** (db / 20):.8f} 0\n" for time, db in curves[role]]


def extend_track(lines: list[str], name: str, role: str) -> None:
    start, end = track_bounds(lines, name)
    block = lines[start:end]
    item = next(i for i, line in enumerate(block) if line.startswith("    <ITEM"))
    length = next(i for i in range(item, len(block)) if block[i].startswith("      LENGTH "))
    block[length] = f"      LENGTH {TOTAL_SECONDS:.12f}\n"
    pool = next(i for i in range(item, len(block)) if block[i].lstrip().startswith("POOLEDEVTS "))
    interp = next(i for i in range(pool + 1, len(block)) if block[i].lstrip().startswith("CCINTERP "))
    base = parse_notes(block[pool + 1:interp])
    block[pool + 1:interp] = encode(transform(base, role))
    replace_envelope(block, role)
    lines[start:end] = block


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    lines = args.source.read_text(encoding="utf-8").splitlines(keepends=True)
    tracks = [
        ("ALTO SAX — Weresax Samples", "sax"),
        ("BASS — Zyn Bass", "bass"),
        ("DRUMS — AVL Blonde Bop", "drums"),
        ("HAMMOND-STYLE ORGAN — Zyn Organ", "organ"),
    ]
    for name, role in tracks:
        extend_track(lines, name, role)
    args.output.write_text("".join(lines), encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
