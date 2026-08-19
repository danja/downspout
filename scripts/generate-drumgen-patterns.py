#!/usr/bin/env python3
"""Generate .dg-pattern template files for DrumGen from Doumbek Rhythm Cheat Sheet."""

import os
import sys

# Lane indices
KICK    = 0
CLAP    = 1
SNARE   = 2
CRASH   = 3
HAT     = 4   # closed hat
LOWTOM  = 5
OPENHAT = 6
HIGHTOM = 7
BASH    = 8
COWBELL = 9
CLAVE   = 10

NUM_LANES = 11
MAX_STEPS = 128

# Velocities
STRONG = 100   # D, T, K uppercase
SOFT   = 70    # d, t, k lowercase


def make_lane_str(lane_idx, total_steps, hits):
    """hits: {step: velocity}. Writes exactly total_steps entries."""
    cells = [f"{hits.get(s, 0)}:0" for s in range(total_steps)]
    return f"lane={lane_idx},0," + ",".join(cells)


def write_template(out_dir, filename, name, source, genre_tags, time_sig, description,
                   bars, meter_n, meter_d, steps_per_beat, steps_per_bar, total_steps,
                   hits_by_lane):
    lines = [
        "# DrumGen Pattern Template v1",
        f"name={name}",
        f"source={source}",
        f"genre_tags={genre_tags}",
        f"time_sig={time_sig}",
        f"description={description}",
        "# ---",
        "version=2",
        f"bars={bars}",
        f"meterNumerator={meter_n}",
        f"meterDenominator={meter_d}",
        f"stepsPerBeat={steps_per_beat}",
        f"stepsPerBar={steps_per_bar}",
        f"totalSteps={total_steps}",
        "generationSerial=0",
    ]
    for lane in range(NUM_LANES):
        lines.append(make_lane_str(lane, total_steps, hits_by_lane.get(lane, {})))

    os.makedirs(out_dir, exist_ok=True)
    path = os.path.join(out_dir, filename)
    with open(path, "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"  {path}")


# -------------------------------------------------------------------------
# Run from repo root. Output goes to plugins/drumgen/patterns/world/
# -------------------------------------------------------------------------
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_DIR = os.path.join(REPO_ROOT, "plugins", "drumgen", "patterns", "world")
SRC  = "Doumbek Rhythm Cheat Sheet"
TAGS = "world,middle-eastern"

print(f"Writing patterns to: {OUT_DIR}")

# =========================================================================
# 4/4  (bars=1, meter=4/4, spb=4, spbar=16, total=16)
# Eighth-note positions on a 1/16 grid: step = 8th-note-index * 2
# =========================================================================
def p44(hits):
    return dict(bars=1, meter_n=4, meter_d=4, spb=4, spbar=16, total=16, hits=hits)

# MAQSUM: D T K T  D K T TK
# D=kick, T=hat, K=clap, TK=hat+clap
# 8th positions: 0 2 4 6  8 10 12 14(+14 for K)
write_template(OUT_DIR, "maqsum.dg-pattern", "Maqsum", SRC, TAGS, "4/4",
               "Egyptian 4/4 foundation rhythm; backbone of Arabic popular music",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 8: STRONG},
    CLAP: {4: STRONG, 10: STRONG, 14: STRONG},
    HAT:  {2: STRONG, 6: STRONG, 12: STRONG, 14: STRONG},
})

# BALADI: D D T  D T K
# D at 0,2,8 | T at 4,10 | K at 12
write_template(OUT_DIR, "baladi.dg-pattern", "Baladi", SRC, TAGS, "4/4",
               "Egyptian 4/4 with double Doum on beat 1",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 2: STRONG, 8: STRONG},
    HAT:  {4: STRONG, 10: STRONG},
    CLAP: {12: STRONG},
})

# SAIDI: D T  D D T
# D at 0,8,10 | T at 4,12
write_template(OUT_DIR, "saidi.dg-pattern", "Saidi", SRC, TAGS, "4/4",
               "Upper-Egyptian 4/4; strong first Doum with triple-Doum mid-pattern",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 8: STRONG, 10: STRONG},
    HAT:  {4: STRONG, 12: STRONG},
})

# NAWARI: T D T  D T   (Gypsy variant starting on Tek)
write_template(OUT_DIR, "nawari.dg-pattern", "Nawari", SRC, TAGS, "4/4",
               "Gypsy/Roma 4/4 variant beginning on Tek",
               1, 4, 4, 4, 16, 16, {
    HAT:  {0: STRONG, 8: STRONG, 12: STRONG},
    KICK: {4: STRONG, 10: STRONG},
})

# BOLERO: D T T D
write_template(OUT_DIR, "bolero.dg-pattern", "Bolero", SRC, TAGS, "4/4",
               "Sparse dramatic 4/4 with two Doums and two Teks",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 12: STRONG},
    HAT:  {4: STRONG, 8: STRONG},
})

# BAMBI: D KT -K T  KT -K D D
# D=0,12,14 | KT(hat+clap)=2,8 | T=6
write_template(OUT_DIR, "bambi.dg-pattern", "Bambi", SRC, TAGS, "4/4",
               "Complex 4/4 with hand-crossing KT pairs",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 12: STRONG, 14: STRONG},
    CLAP: {2: STRONG, 8: STRONG},
    HAT:  {2: STRONG, 6: STRONG, 8: STRONG},
})

# WAHDA: D TK TK T  TK TK T TK
write_template(OUT_DIR, "wahda.dg-pattern", "Wahda", SRC, TAGS, "4/4",
               "Dense 4/4 full of TK pairs; strong one-feel",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG},
    HAT:  {2: STRONG, 4: STRONG, 6: STRONG, 8: STRONG, 10: STRONG, 12: STRONG, 14: STRONG},
    CLAP: {2: STRONG, 4: STRONG, 8: STRONG, 10: STRONG, 14: STRONG},
})

# SOMBATI: D kT -k T  D kk T kk
# kT = soft clap (k) + strong hat (T); kk = soft clap
write_template(OUT_DIR, "sombati.dg-pattern", "Sombati", SRC, TAGS, "4/4",
               "Soft 4/4 variant of Wahda family with kk pairs",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 8: STRONG},
    CLAP: {2: SOFT, 10: SOFT, 14: SOFT},
    HAT:  {2: STRONG, 6: STRONG, 12: STRONG},
})

# ZAFFA: D tt t t  D t t
# D=0,8 | tt(soft hat pair) at 2,3 | t at 4,6 | t at 10,12
write_template(OUT_DIR, "zaffa.dg-pattern", "Zaffa", SRC, TAGS, "4/4",
               "Wedding procession feel with rolling soft Teks",
               1, 4, 4, 4, 16, 16, {
    KICK: {0: STRONG, 8: STRONG},
    HAT:  {2: SOFT, 3: SOFT, 4: SOFT, 6: SOFT, 10: SOFT, 12: SOFT},
})

# =========================================================================
# 2/4  (bars=1, meter=2/4, spb=4, spbar=8, total=8)
# =========================================================================

# MALFUF: D -T T   ("spinning" groove)
write_template(OUT_DIR, "malfuf.dg-pattern", "Malfuf", SRC, TAGS, "2/4",
               "Fast-spinning 2/4; common in Egyptian weddings",
               1, 2, 4, 4, 8, 8, {
    KICK: {0: STRONG},
    HAT:  {6: STRONG},
})

# KHALIGI: D -D T  (Malfuf with 2 Doums; Gulf region)
write_template(OUT_DIR, "khaligi.dg-pattern", "Khaligi", SRC, TAGS, "2/4",
               "Gulf region 2/4; Malfuf with double Doum",
               1, 2, 4, 4, 8, 8, {
    KICK: {0: STRONG, 4: STRONG},
    HAT:  {6: STRONG},
})

# AYUB: D -k D T
write_template(OUT_DIR, "ayub.dg-pattern", "Ayub", SRC, TAGS, "2/4",
               "Sufi/Zar ceremony 2/4 with soft Ka pickup",
               1, 2, 4, 4, 8, 8, {
    KICK: {0: STRONG, 4: STRONG},
    CLAP: {2: SOFT},
    HAT:  {6: STRONG},
})

# KARACHI: T -k T D  (inverted Ayub)
write_template(OUT_DIR, "karachi.dg-pattern", "Karachi", SRC, TAGS, "2/4",
               "Inverted Ayub; starts on Tek",
               1, 2, 4, 4, 8, 8, {
    HAT:  {0: STRONG, 4: STRONG},
    CLAP: {2: SOFT},
    KICK: {6: STRONG},
})

# =========================================================================
# 3/4  (bars=1, meter=3/4, spb=4, spbar=12, total=12)
# =========================================================================

# VALS: D T T  (Turkish waltz)
write_template(OUT_DIR, "vals.dg-pattern", "Vals", SRC, TAGS, "3/4",
               "Turkish waltz (Vals=Waltz); simple three-beat feel",
               1, 3, 4, 4, 12, 12, {
    KICK: {0: STRONG},
    HAT:  {4: STRONG, 8: STRONG},
})

# =========================================================================
# 5/8  (bars=1, meter=5/8, spb=2, spbar=10, total=10)
# One 16th note per step-unit; tk pairs fit within adjacent steps
# =========================================================================

# TURKISH 5: D k T k k  (12 123)
write_template(OUT_DIR, "turkish5.dg-pattern", "Turkish 5", SRC, TAGS, "5/8",
               "Turkish 5/8 groove in 2+3 grouping",
               1, 5, 8, 2, 10, 10, {
    KICK: {0: STRONG},
    CLAP: {2: SOFT, 6: SOFT, 8: SOFT},
    HAT:  {4: STRONG},
})

# SHOUSH: D tk tk D T  (123 12)
# D=0, t=2 k=3, t=4 k=5, D=6, T=8
write_template(OUT_DIR, "shoush.dg-pattern", "Shoush", SRC, TAGS, "5/8",
               "5/8 in 3+2 grouping with tk subdivisions",
               1, 5, 8, 2, 10, 10, {
    KICK: {0: STRONG, 6: STRONG},
    HAT:  {2: SOFT, 4: SOFT, 8: STRONG},
    CLAP: {3: SOFT, 5: SOFT},
})

# =========================================================================
# 6/4  (bars=1, meter=6/4, spb=4, spbar=24, total=24)
# =========================================================================

# SUDASI: D D D D D T
write_template(OUT_DIR, "sudasi.dg-pattern", "Sudasi", SRC, TAGS, "6/4",
               "Six-beat Sudanese rhythm with five Doums and one Tek",
               1, 6, 4, 4, 24, 24, {
    KICK: {0: STRONG, 4: STRONG, 8: STRONG, 12: STRONG, 16: STRONG},
    HAT:  {20: STRONG},
})

# =========================================================================
# 6/8  (bars=1, meter=6/8, spb=2, spbar=12, total=12)
# =========================================================================

# MOROCCAN 6: D k k  D k k
write_template(OUT_DIR, "moroccan6.dg-pattern", "Moroccan 6", SRC, TAGS, "6/8",
               "Moroccan 6/8 with two groups of three beats",
               1, 6, 8, 2, 12, 12, {
    KICK: {0: STRONG, 6: STRONG},
    CLAP: {2: SOFT, 4: SOFT, 8: SOFT, 10: SOFT},
})

# RENG (Shish Hasht): D KT D TK TK
# 5 events in 6 eighth-note positions
write_template(OUT_DIR, "reng.dg-pattern", "Reng", SRC, TAGS, "6/8",
               "Persian Shish Hasht (6/8) with KT and TK pairs",
               1, 6, 8, 2, 12, 12, {
    KICK: {0: STRONG, 4: STRONG},
    CLAP: {2: STRONG, 6: STRONG, 8: STRONG},
    HAT:  {2: STRONG, 6: STRONG, 8: STRONG},
})

# =========================================================================
# 7/8  (bars=1, meter=7/8, spb=2, spbar=14, total=14)
# =========================================================================

# LAZ: D k T k  D k k  (12 12 123)
write_template(OUT_DIR, "laz.dg-pattern", "Laz", SRC, TAGS, "7/8",
               "Turkish 7/8 in 2+2+3 grouping",
               1, 7, 8, 2, 14, 14, {
    KICK: {0: STRONG, 8: STRONG},
    CLAP: {2: SOFT, 6: SOFT, 10: SOFT, 12: SOFT},
    HAT:  {4: STRONG},
})

# KALAMATIANO: D k k  D k T k  (123 12 12)
write_template(OUT_DIR, "kalamatiano.dg-pattern", "Kalamatiano", SRC, TAGS, "7/8",
               "Greek 7/8 in 3+2+2 grouping",
               1, 7, 8, 2, 14, 14, {
    KICK: {0: STRONG, 6: STRONG},
    CLAP: {2: SOFT, 4: SOFT, 8: SOFT, 12: SOFT},
    HAT:  {10: STRONG},
})

# =========================================================================
# 8/4  encoded as 2 bars of 4/4  (bars=2, meter=4/4, spb=4, spbar=16, total=32)
# Bar 1 steps 0-15, bar 2 steps 16-31
# =========================================================================

# CIFTETELLI: Line1: D K T K T (tk) | Line2: D D T
write_template(OUT_DIR, "ciftetelli.dg-pattern", "Ciftetelli", SRC, TAGS, "8/4",
               "Turkish belly-dance 8/4 in two 4-beat phrases",
               2, 4, 4, 4, 16, 32, {
    KICK: {0: STRONG, 16: STRONG, 20: STRONG},
    CLAP: {2: STRONG, 6: STRONG, 11: SOFT},      # K K, then soft k from tk
    HAT:  {4: STRONG, 8: STRONG, 10: SOFT, 24: STRONG},  # T T tk-t, then T
})

# MASMOUDI: Line1: D D TK TK T | Line2: D TK TK T TK TK T
write_template(OUT_DIR, "masmoudi.dg-pattern", "Masmoudi", SRC, TAGS, "8/4",
               "Heavy 8/4 with cascading TK pairs; common in North Africa",
               2, 4, 4, 4, 16, 32, {
    KICK: {0: STRONG, 2: STRONG, 16: STRONG},
    CLAP: {4: STRONG, 6: STRONG, 18: STRONG, 20: STRONG, 24: STRONG, 26: STRONG},
    HAT:  {4: STRONG, 6: STRONG, 8: STRONG,
           18: STRONG, 20: STRONG, 22: STRONG, 24: STRONG, 26: STRONG, 28: STRONG},
})

# =========================================================================
# 9/8  (bars=1, meter=9/8, spb=2, spbar=18, total=18)
# =========================================================================

# KARSILAMA: D T D T T  (12 12 12 123)
write_template(OUT_DIR, "karsilama.dg-pattern", "Karsilama", SRC, TAGS, "9/8",
               "Turkish 9/8 in 2+2+2+3 grouping",
               1, 9, 8, 2, 18, 18, {
    KICK: {0: STRONG, 8: STRONG},
    HAT:  {4: STRONG, 12: STRONG, 16: STRONG},
})

# GYPSY 9: D D tk T tk T T tk
# D=0,2  t=4,k=5  T=6  t=8,k=9  T=10  T=12  t=14,k=15
write_template(OUT_DIR, "gypsy9.dg-pattern", "Gypsy 9", SRC, TAGS, "9/8",
               "Romani 9/8 with tk subdivisions",
               1, 9, 8, 2, 18, 18, {
    KICK: {0: STRONG, 2: STRONG},
    HAT:  {4: SOFT, 6: STRONG, 8: SOFT, 10: STRONG, 12: STRONG, 14: SOFT},
    CLAP: {5: SOFT, 9: SOFT, 15: SOFT},
})

# SYNCOPATED 9: D D tk Tk kT kk T
# D=0,2  tk=4-5  Tk=6-7  kT=8-9  kk=10-11  T=12
write_template(OUT_DIR, "syncopated9.dg-pattern", "Syncopated 9", SRC, TAGS, "9/8",
               "Advanced syncopated 9/8 with cross-rhythms",
               1, 9, 8, 2, 18, 18, {
    KICK: {0: STRONG, 2: STRONG},
    HAT:  {4: SOFT, 6: STRONG, 9: STRONG, 12: STRONG},
    CLAP: {5: SOFT, 7: SOFT, 8: SOFT, 10: SOFT, 11: SOFT},
})

# =========================================================================
# 10/8  (bars=1, meter=10/8, spb=2, spbar=20, total=20)
# =========================================================================

# CURCUNA: D T D T  (123 12 12 123)
# Accent positions at 0, 6, 10, 14
write_template(OUT_DIR, "curcuna.dg-pattern", "Curcuna", SRC, TAGS, "10/8",
               "10/8 in 3+2+2+3 grouping (also known as Jorjuna)",
               1, 10, 8, 2, 20, 20, {
    KICK: {0: STRONG, 10: STRONG},
    HAT:  {6: STRONG, 14: STRONG},
})

# SAMAI: D T D D T  (12 12 12 123)
write_template(OUT_DIR, "samai.dg-pattern", "Samai", SRC, TAGS, "10/8",
               "10/8 Samai with strong opening Doum",
               1, 10, 8, 2, 20, 20, {
    KICK: {0: STRONG, 8: STRONG, 12: STRONG},
    HAT:  {4: STRONG, 16: STRONG},
})

count = len([f for f in os.listdir(OUT_DIR) if f.endswith(".dg-pattern")])
print(f"\nDone. {count} pattern files in {OUT_DIR}")
