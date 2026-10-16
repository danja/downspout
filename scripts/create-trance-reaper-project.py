#!/usr/bin/env python3
"""Write an original uplifting-trance arrangement as a REAPER project.

138 BPM, 4/4, A minor, Am - F - C - G (two bars each). 96 bars (~2:47):

  bars  0-16  Intro      kick, hats, offbeat open hat; bass from bar 8
  bars 16-32  Build      + clap, 16th hats, pad (24), pluck (24), snare roll 30-32
  bars 32-48  Breakdown  no kick/bass; pad, soft pluck, lead melody, snare riser 44-48
  bars 48-80  Drop       everything, crash on 48 and 64
  bars 80-96  Outro      kick, hats, bass, pluck, pad; thins out

Tracks use Downspout instruments: DrumKit (drums), Basilico (bass), Canticle
(pluck arp and pad), Floozy (lead); Guardian on the master. The plugin patches
are left at their defaults; choose sounds in the plugins after opening.

The project header (master chain, tempo envelope layout) is taken from an
existing Downspout project so the file matches this REAPER's format.

Usage: create-trance-reaper-project.py [--template T.RPP] [--out OUT.RPP]
"""

from __future__ import annotations

import argparse
import re
import uuid
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
TEMPLATE = REPO / "artifacts/reaper/mosaic-showcase.RPP"
OUT = REPO / "artifacts/reaper/trance-original.RPP"

BPM = 138
PPQ = 960
BARS = 96
Q = PPQ  # ticks per quarter note

KICK, CLAP, SNARE, CRASH, CHAT, OHAT = 36, 39, 40, 41, 42, 46

# Two bars per chord: (bass root, pad triad, arp pitches low-to-high, lead phrase)
CHORDS = [
    (33, [57, 60, 64], [57, 64, 69, 72]),  # Am
    (29, [53, 57, 60], [53, 60, 65, 69]),  # F
    (36, [55, 60, 64], [55, 60, 64, 67]),  # C
    (31, [55, 59, 62], [55, 62, 67, 71]),  # G
]

# (beat in the two-bar chord, length in beats, pitch)
LEAD = [
    [(0, 1.5, 76), (1.5, .5, 74), (2, 1, 72), (3, 1, 76), (4, 2, 81), (6, 1, 79), (7, 1, 76)],
    [(0, 1.5, 77), (1.5, .5, 76), (2, 1, 72), (3, 1, 69), (4, 2, 72), (6, 1.5, 77), (7.5, .5, 76)],
    [(0, 1.5, 79), (1.5, .5, 76), (2, 1, 79), (3, 1, 84), (4, 1, 83), (5, 1, 79), (6, 2, 76)],
    [(0, 1.5, 74), (1.5, .5, 71), (2, 1, 74), (3, 1, 79), (4, 1.5, 83), (5.5, .5, 81), (6, 2, 79)],
]

Note = tuple[int, int, int, int]  # start tick, pitch, length ticks, velocity


def t(bar: float, beat: float = 0.0) -> int:
    return round((bar * 4 + beat) * Q)


def chord_at(bar: int) -> int:
    return (bar // 2) % 4


def drums() -> list[Note]:
    n: list[Note] = []

    def hit(bar: int, beat: float, pitch: int, vel: int, length: float = 0.2) -> None:
        n.append((t(bar, beat), pitch, round(length * Q), vel))

    for bar in range(BARS):
        in_break = 32 <= bar < 48
        build = 16 <= bar < 32
        drop = 48 <= bar < 80
        outro = bar >= 80
        if not in_break:
            for beat in range(4):
                hit(bar, beat, KICK, 118 if beat == 0 else 110)
                hit(bar, beat + .5, OHAT, 82 if drop else 70, .35)
            if build or drop:
                hit(bar, 1, CLAP, 100)
                hit(bar, 3, CLAP, 104)
        # Closed hats: off in the opening bars, 8ths -> 16ths as energy rises.
        if bar >= 4 and not in_break:
            step = .25 if (build or drop) else .5
            beat = 0.0
            while beat < 4:
                accent = beat % 1 == 0
                if beat % 1 != .5:  # the open hat owns the offbeat
                    hit(bar, beat, CHAT, 66 if accent else 54 + round(8 * ((beat * 4) % 2)))
                beat += step
        if bar in (0, 48, 64):
            hit(bar, 0, CRASH, 112, 1.0)
        if bar == 32:
            hit(bar, 0, CRASH, 96, 1.0)
        # Snare rolls into the breakdown and the drop: 8ths, 16ths, 32nds, rising velocity.
        for start, end in ((30, 32), (44, 48)):
            if start <= bar < end:
                span = (end - start) * 4
                pos = (bar - start) * 4
                for beat in range(4):
                    progress = (pos + beat) / span
                    div = 2 if progress < .4 else 4 if progress < .75 else 8
                    for s in range(div):
                        vel = round(50 + 70 * (progress + s / div / span))
                        hit(bar, beat + s / div, SNARE, min(124, vel), .1)
    return n


def bass() -> list[Note]:
    n: list[Note] = []
    for bar in range(BARS):
        if bar < 8 or 32 <= bar < 48:
            continue
        root = CHORDS[chord_at(bar)][0]
        for beat in range(4):
            # Classic offbeat roll: a note on every "and", off the kick.
            vel = 100 if bar >= 48 else 90
            pitch = root + (12 if beat % 2 else 0) if bar >= 48 and bar % 4 == 3 and beat == 3 else root
            n.append((t(bar, beat + .5), pitch, round(.38 * Q), vel))
    return n


def pluck() -> list[Note]:
    n: list[Note] = []
    pattern = [0, 1, 2, 3, 2, 1, 2, 3]  # indices into the chord's four arp pitches
    for bar in range(24, BARS):
        if 80 <= bar and bar >= 92:
            continue
        soft = 32 <= bar < 48
        arp = CHORDS[chord_at(bar)][2]
        octave = 12 if bar >= 48 and bar < 80 else 0
        for step in range(16):
            if soft and step % 2:
                continue
            pitch = arp[pattern[step % 8]] + 12 + octave
            ramp = min(1.0, (bar - 24) / 8) if bar < 32 else 1.0
            vel = round((58 + 18 * ((step % 4) == 0)) * (0.6 + 0.4 * ramp) * (0.8 if soft else 1))
            n.append((t(bar, step * .25), pitch, round(.2 * Q), vel))
    return n


def pad() -> list[Note]:
    n: list[Note] = []
    for bar in range(16, BARS, 2):
        if 80 <= bar and bar >= 94:
            continue
        for pitch in CHORDS[chord_at(bar)][1]:
            n.append((t(bar), pitch, t(2) - round(.1 * Q), 62 if bar < 32 else 70))
    return n


def lead() -> list[Note]:
    n: list[Note] = []
    for bar in list(range(32, 48, 2)) + list(range(48, 80, 2)):
        octave = 12 if bar >= 64 else 0
        for beat, length, pitch in LEAD[chord_at(bar)]:
            vel = 84 if bar < 48 else 96
            n.append((t(bar, beat), pitch + octave, round((length - .06) * Q), vel))
    return n


def midi_block(notes: list[Note], channel: int) -> list[str]:
    events: list[tuple[int, int, int, int]] = []  # tick, order, status, data...
    for start, pitch, length, vel in notes:
        events.append((start, 1, 0x90 | channel, pitch, vel))
        events.append((start + max(1, length), 0, 0x80 | channel, pitch, 0))
    events.sort()
    lines, last = [], 0
    for tick, _, status, pitch, vel in events:
        lines.append(f"        E {tick - last} {status:x} {pitch:02x} {vel:02x}")
        last = tick
    return lines


def guid() -> str:
    return "{" + str(uuid.uuid4()).upper() + "}"


def track(name: str, color: int, vst: str, notes: list[Note], channel: int, volume: float, pan: float) -> list[str]:
    seconds = BARS * 4 * 60 / BPM
    track_id = guid()
    return [
        f"  <TRACK {track_id}",
        f'    NAME "{name}"',
        f"    PEAKCOL {color}",
        "    BEAT -1",
        "    AUTOMODE 0",
        "    PANLAWFLAGS 3",
        f"    VOLPAN {volume:.4f} {pan:.3f} -1 -1 1",
        "    MUTESOLO 0 0 0",
        "    IPHASE 0",
        "    ISBUS 0 0",
        "    BUSCOMP 0 0 0 0 0",
        "    SHOWINMIX 1 0.6667 0.5 1 0.5 0 0 0 0",
        "    SEL 0",
        "    REC 0 4096 1 0 0 0 0 0",
        "    VU 64",
        "    NCHAN 2",
        "    FX 1",
        f"    TRACKID {track_id}",
        "    MAINSEND 1 0",
        "    <FXCHAIN",
        "      SHOW 0",
        "      LASTSEL 0",
        "      DOCKED 0",
        "      BYPASS 0 0 0",
        f"      {vst}",
        "      >",
        "      FLOATPOS 0 0 0 0",
        f"      FXID {guid()}",
        "      WAK 0 0",
        "    >",
        "    <ITEM",
        "      POSITION 0",
        "      SNAPOFFS 0",
        f"      LENGTH {seconds:.6f}",
        "      LOOP 0",
        "      ALLTAKES 0",
        "      MUTE 0 0",
        "      SEL 0",
        f"      IGUID {guid()}",
        f'      NAME "{name}"',
        "      VOLPAN 1 0 1 -1",
        "      SOFFS 0 0",
        "      PLAYRATE 1 1 0 -1 0 0.0025",
        "      CHANMODE 0",
        f"      GUID {guid()}",
        "      <SOURCE MIDI",
        f"        HASDATA 1 {PPQ} QN",
        "        CCINTERP 32",
        f"        POOLEDEVTS {guid()}",
        *midi_block(notes, channel),
        f"        E {t(BARS) - max((n[0] + n[2] for n in notes), default=0)} b0 7b 00",
        "        CCINTERP 32",
        "      >",
        "    >",
        "  >",
    ]


def build(template: Path) -> str:
    lines = template.read_text(encoding="utf-8").splitlines()
    first_track = next(i for i, line in enumerate(lines) if line.startswith("  <TRACK "))
    header = lines[:first_track]
    # Replace the long NOTES block, then set the tempo.
    start = next(i for i, line in enumerate(header) if line.startswith("  <NOTES"))
    end = next(i for i in range(start + 1, len(header)) if header[i] == "  >")
    header[start:end + 1] = ["  <NOTES 0 2", "  >"]
    text = "\n".join(header)
    text = re.sub(r"^  TEMPO \S+", f"  TEMPO {BPM}", text, flags=re.M)
    text = re.sub(r"(    PT 0\.0+ )\S+", rf"\g<1>{BPM}.0000000000", text, count=1)

    vsts = {
        "drum": '<VST "VST3i: DrumKit (danja)" drumkit.vst3 0 "" 699622239{2046504473616C6374496B4470536E44} ""',
        "bass": '<VST "VST3i: Basilico (danja)" basilico.vst3 0 "" 1393210385{2046504473616C63636C734270536E44} ""',
        "canticle": '<VST "VST3i: Canticle (danja)" canticle.vst3 0 "" 1086427472{2046504473616C636C746E4370536E44} ""',
        "lead": '<VST "VST3i: Floozy (danja)" floozy.vst3 0 "" 1153462542{2046504473616C63797A6C4670536E44} ""',
    }
    body: list[str] = []
    body += track("DRUMS - DrumKit", 19810408, vsts["drum"], drums(), 9, 0.80, 0.0)
    body += track("BASS - Basilico", 16744448, vsts["bass"], bass(), 0, 0.85, 0.0)
    body += track("PLUCK - Canticle arp", 33023, vsts["canticle"], pluck(), 0, 0.55, -0.25)
    body += track("PAD - Canticle", 8388736, vsts["canticle"], pad(), 0, 0.45, 0.25)
    body += track("LEAD - Floozy", 16711680, vsts["lead"], lead(), 0, 0.60, 0.0)
    return text + "\n" + "\n".join(body) + "\n  <EXTENSIONS\n  >\n>\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--template", type=Path, default=TEMPLATE)
    parser.add_argument("--out", type=Path, default=OUT)
    args = parser.parse_args()
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(build(args.template), encoding="utf-8")
    for name, notes in (("drums", drums()), ("bass", bass()), ("pluck", pluck()), ("pad", pad()), ("lead", lead())):
        end = max(n[0] + n[2] for n in notes) / (4 * Q)
        print(f"{name:6s} {len(notes):5d} notes, ends bar {end:.2f}")
    print(f"{BARS} bars at {BPM} bpm = {BARS * 4 * 60 / BPM:.1f} s -> {args.out}")


if __name__ == "__main__":
    main()
