#!/usr/bin/env python3
"""Add a Zyn organ accompaniment track to the saved fire-trio REAPER project."""

from __future__ import annotations

import argparse
import re
import uuid
from pathlib import Path


ORGAN_TTL = Path("/usr/lib/lv2/ZynAddSubFX.lv2presets/Organ.ttl")


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


def organ_state() -> list[str]:
    ttl = ORGAN_TTL.read_text(encoding="utf-8")
    xml = re.search(r'"""\s*(.*?)\s*"""', ttl, re.S).group(1)
    return ["             |\n", *[f"            n|{line}\n" for line in xml.splitlines()]]


def midi_events() -> list[str]:
    # bar, quarter offset, duration in quarters, pitches, velocity
    gestures = [
        (0,.00,3.55,[62,65,69],48),(1,.35,2.80,[60,65,70],44),
        (2,.08,2.20,[61,67,70],50),(3,1.05,2.55,[58,64,69],46),
        (4,.00,3.15,[62,68,74],53),(5,.72,2.60,[60,65,71],48),
        (6,.12,3.35,[63,69,72],51),(7,.00,3.75,[62,65,69],46),
        # Sax solo: short shouts alternating with long translucent holds.
        (8,.22,.62,[65,70,74],49),(8,2.10,1.55,[61,67,72],43),
        (9,1.18,.48,[64,69,73],54),(10,.08,2.65,[60,66,71],42),
        (11,2.35,.72,[63,68,74],55),(12,.40,1.15,[61,65,70],47),
        (13,1.72,1.90,[62,67,71],44),
        # Bass solo: almost no comping; one high, soft answer per two bars.
        (14,2.65,.58,[67,72,77],38),(16,3.02,.55,[65,70,76],40),
        # Drum solo: low-volume suspended clusters act as a horizon.
        (18,.00,3.80,[62,68,73],34),(20,.00,3.80,[60,66,71],36),
        # Return.
        (22,.00,3.45,[62,65,69,74],52),(23,.35,3.30,[58,62,67,72],49),
    ]
    events = []
    for bar, offset, duration, pitches, velocity in gestures:
        on = round((bar * 4 + offset) * 960)
        off = round((bar * 4 + offset + duration) * 960)
        for pitch in pitches:
            events.extend([(on, 1, f"90 {pitch:02x} {velocity:02x}"), (off, 0, f"80 {pitch:02x} 00")])
    events.sort(key=lambda event: (event[0], event[1]))
    output, previous = [], 0
    for tick, _, payload in events:
        output.append(f"        E {tick - previous} {payload}\n")
        previous = tick
    output.append(f"        E {24 * 4 * 960 - previous} b0 7b 00\n")
    return output


def fresh_ids(block: list[str]) -> list[str]:
    mapping = {}
    def replace(match: re.Match[str]) -> str:
        old = match.group(0)
        mapping.setdefault(old, "{" + str(uuid.uuid4()).upper() + "}")
        return mapping[old]
    return [re.sub(r"\{[0-9A-Fa-f-]{36}\}", replace, line) for line in block]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    lines = args.source.read_text(encoding="utf-8").splitlines(keepends=True)
    start, end = track_bounds(lines, "BASS — Zyn Bass")
    block = fresh_ids(lines[start:end])
    name = next(i for i, line in enumerate(block) if line.startswith("    NAME "))
    block[name] = '    NAME "HAMMOND-STYLE ORGAN — Zyn Organ"\n'
    volpan = next(i for i, line in enumerate(block) if line.startswith("    VOLPAN "))
    block[volpan] = "    VOLPAN 1 0.14 -1 -1 1\n"
    # Replace the bass preset state with the installed Organ 1 preset.
    state = next(i for i, line in enumerate(block) if line.startswith("          <S urn:distrho:state 3"))
    depth, state_end = 0, state
    while state_end < len(block):
        stripped = block[state_end].lstrip()
        depth += stripped.startswith("<")
        depth -= stripped.startswith(">")
        state_end += 1
        if depth == 0:
            break
    block[state + 1:state_end - 1] = organ_state()
    # Replace the cloned bass MIDI with upper-register organ gestures.
    pool = next(i for i, line in enumerate(block) if line.lstrip().startswith("POOLEDEVTS "))
    interp = next(i for i in range(pool + 1, len(block)) if block[i].lstrip().startswith("CCINTERP "))
    block[pool + 1:interp] = midi_events()
    # Replace the envelope with conservative accompaniment levels.
    env = next(i for i, line in enumerate(block) if line.startswith("    <VOLENV2"))
    env_close = next(i for i in range(env + 1, len(block)) if block[i].startswith("    >"))
    points = [(0,.14125375),(12.63157895,.19952623),(22.10526316,.07943282),
              (28.42105263,.06309573),(34.73684211,.17782794),(37.89473684,.03162278)]
    first_pt = next(i for i in range(env, env_close) if block[i].lstrip().startswith("PT "))
    last_pt = max(i for i in range(env, env_close) if block[i].lstrip().startswith("PT ")) + 1
    block[first_pt:last_pt] = [f"      PT {time:.8f} {value:.8f} 0\n" for time, value in points]
    # Put the organ immediately before the room bus.
    room_start, _ = track_bounds(lines, "ROOM — ReaVerb")
    lines[room_start:room_start] = block
    args.output.write_text("".join(lines), encoding="utf-8")
    print(args.output)


if __name__ == "__main__":
    main()
