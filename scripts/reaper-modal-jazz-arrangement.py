#!/usr/bin/env python3
"""Emit a reusable REAPER-MCP MIDI arrangement recipe as JSON.

The output contains relative-time note dictionaries accepted by
`create_midi_clip`.  It intentionally uses an original modal-jazz vocabulary
rather than transcribing any existing composition.
"""

from __future__ import annotations

import argparse
import json


def midi(pitch: int, start: float, length: float, velocity: int = 90, channel: int = 0) -> dict:
    return {"pitch": max(0, min(127, pitch)), "start": start, "length": length,
            "velocity": velocity, "channel": channel}


def arrangement(tempo: float = 168.0, bars: int = 32) -> dict:
    q = 60.0 / tempo
    eighth, bar = q / 2.0, q * 3.0
    lead, piano, bass, drums = [], [], [], []
    roots = [38, 38, 39, 38, 36, 41, 40, 45]
    chords = [[50, 53, 57, 60], [50, 53, 57, 60], [51, 55, 58, 62], [50, 53, 57, 60],
              [48, 52, 55, 58], [53, 57, 60, 64], [52, 55, 59, 62], [45, 49, 52, 55]]

    for b in range(bars):
        t, idx = b * bar, b % 8
        root = roots[idx]
        walk = [root, root + 7, root + 10, root + 12, root + 7, root - 1 if b % 2 else root + 2]
        bass.extend(midi(p, t + s * eighth, eighth * .82, 70 + (s % 3) * 8) for s, p in enumerate(walk))
        if b >= 2:
            voicing = [p + (12 if 16 <= b < 24 and j > 1 else 0) for j, p in enumerate(chords[idx])]
            for s, k in ((1, 0), (4, 1)):
                piano.extend(midi(p, t + s * eighth, eighth * (1.45 if k else .8), 52 + j * 5 + (b % 3) * 3)
                             for j, p in enumerate(voicing))
        drums.extend(midi(51, t + s * eighth, eighth * .35, 58 + (24 if s % 3 == 0 else 8), 9)
                     for s in range(6))
        drums.extend([midi(36, t, eighth * .45, 82, 9), midi(36, t + 3 * eighth, eighth * .4, 68, 9),
                      midi(44, t + 2 * eighth, eighth * .25, 58, 9), midi(44, t + 5 * eighth, eighth * .25, 68, 9)])
        if b % 2:
            drums.append(midi(38, t + 4 * eighth, eighth * .3, 54 + (b % 4) * 6, 9))

    motif = [
        (0,62,0,2),(0,65,2,1),(0,69,3,2),(0,72,5,1),(1,70,0,1),(1,69,1,1),(1,65,2,2),(1,62,4,2),
        (2,63,0,2),(2,67,2,1),(2,70,3,1),(2,74,4,2),(3,72,0,1),(3,69,1,1),(3,65,2,1),(3,62,3,3),
        (4,60,0,1),(4,64,1,1),(4,67,2,2),(4,70,4,2),(5,69,0,2),(5,72,2,1),(5,76,3,2),(5,74,5,1),
        (6,71,0,1),(6,67,1,1),(6,64,2,2),(6,62,4,2),(7,61,0,1),(7,64,1,1),(7,69,2,1),(7,73,3,1),(7,76,4,2)]
    for b, p, s, d in motif:
        lead.append(midi(p, (4 + b) * bar + s * eighth, d * eighth * .9, 84 + (s % 3) * 5))
        lead.append(midi(p + (12 if b % 3 == 0 else 0), (24 + b) * bar + s * eighth, d * eighth * .86, 88))
    cells = [[62,65,69,72], [63,67,70,74], [60,64,67,70], [57,61,64,69]]
    for b in range(12, 24):
        for s in range(6):
            if (b + s) % 5:
                octave = (12 if b >= 18 else 0) + (12 if s == 5 and b % 2 else 0)
                lead.append(midi(cells[b % 4][(s + b) % 4] + octave, b * bar + s * eighth,
                                 eighth * (1.6 if s % 3 == 2 else .72), 72 + ((b * 7 + s * 5) % 28)))
    return {"tempo": tempo, "time_signature": "6/8", "bars": bars, "length": bars * bar,
            "tracks": {"tenor": lead, "piano": piano, "bass": bass, "drums": drums}}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--tempo", type=float, default=168.0)
    parser.add_argument("--bars", type=int, default=32)
    parser.add_argument("--indent", type=int, default=2)
    args = parser.parse_args()
    print(json.dumps(arrangement(args.tempo, args.bars), indent=args.indent))
