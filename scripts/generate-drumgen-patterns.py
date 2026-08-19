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

# =========================================================================
# LATIN patterns
# =========================================================================
LAT_DIR  = os.path.join(REPO_ROOT, "plugins", "drumgen", "patterns", "latin")
LAT_SRC  = "Latin Rhythm Reference"
AFCUBAN  = "latin,afro-cuban"
BRAZN    = "latin,brazilian"

print(f"\nWriting Latin patterns to: {LAT_DIR}")

# --- Clave patterns (pure rhythm in CLAVE lane + kick on beat 1) ----------

# Son Clave 3-2   x . . x . . x . . . . x . x . .
write_template(LAT_DIR, "son-clave-32.dg-pattern", "Son Clave 3-2", LAT_SRC, AFCUBAN, "4/4",
               "Foundation 3-2 son clave; basis of all Afro-Cuban music",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG},
    CLAVE: {0: STRONG, 3: STRONG, 6: STRONG, 11: STRONG, 13: STRONG},
})

# Son Clave 2-3   . . . x . x . . x . . x . . x .
write_template(LAT_DIR, "son-clave-23.dg-pattern", "Son Clave 2-3", LAT_SRC, AFCUBAN, "4/4",
               "2-3 son clave (2-side first); common in Puerto Rican salsa",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG},
    CLAVE: {3: STRONG, 5: STRONG, 8: STRONG, 11: STRONG, 14: STRONG},
})

# Rumba Clave 3-2  x . . x . . . x . . . x . x . .
write_template(LAT_DIR, "rumba-clave-32.dg-pattern", "Rumba Clave 3-2", LAT_SRC, AFCUBAN, "4/4",
               "Rumba 3-2 clave; 3rd hit shifted one 16th later than son",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG},
    CLAVE: {0: STRONG, 3: STRONG, 7: STRONG, 11: STRONG, 13: STRONG},
})

# Rumba Clave 2-3  . . . x . x . . x . . x . . . x
write_template(LAT_DIR, "rumba-clave-23.dg-pattern", "Rumba Clave 2-3", LAT_SRC, AFCUBAN, "4/4",
               "Rumba 2-3 clave; used in Cuban rumba and guaguancó",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG},
    CLAVE: {3: STRONG, 5: STRONG, 8: STRONG, 11: STRONG, 15: STRONG},
})

# Tresillo  x . . x . . x . x . . x . . x .   (3-feel over 4/4; precursor to clave)
write_template(LAT_DIR, "tresillo.dg-pattern", "Tresillo", LAT_SRC, AFCUBAN, "4/4",
               "3-over-8 tresillo; African-derived root of Caribbean rhythms",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 3: STRONG, 6: STRONG},
    CLAVE: {0: STRONG, 3: STRONG, 6: STRONG, 8: SOFT, 11: SOFT, 14: SOFT},
})

# --- Latin grooves ---

# Bossa Nova  (Joao Gilberto-style; kick 1/+2/3, brushed snare on 2/4, son 3-2 clave)
write_template(LAT_DIR, "bossa-nova.dg-pattern", "Bossa Nova", LAT_SRC, BRAZN, "4/4",
               "Brazilian bossa nova; kick on 1/+2/3, brushed snare, son clave",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 6: 80, 8: STRONG},
    SNARE: {4: SOFT, 12: SOFT},
    HAT:   {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
    CLAVE: {0: STRONG, 3: STRONG, 6: STRONG, 11: STRONG, 13: STRONG},
})

# Samba  (partido alto tamborim in CLAVE lane, quarter hi-hat)
write_template(LAT_DIR, "samba.dg-pattern", "Samba", LAT_SRC, BRAZN, "4/4",
               "Brazilian samba; surdo kick, partido alto tamborim in CLAVE lane",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 8: STRONG},
    SNARE: {4: SOFT, 12: SOFT},
    HAT:   {0: 80, 4: 80, 8: 80, 12: 80},
    CLAVE: {0: STRONG, 3: STRONG, 6: SOFT, 8: STRONG, 11: STRONG, 14: SOFT},
})

# Mambo  (2-3 son clave, cowbell, kick on 1/3)
write_template(LAT_DIR, "mambo.dg-pattern", "Mambo", LAT_SRC, AFCUBAN, "4/4",
               "Afro-Cuban mambo; cowbell on clave-derived accents, 2-3 son clave",
               1, 4, 4, 4, 16, 16, {
    KICK:    {0: STRONG, 8: STRONG},
    SNARE:   {4: SOFT, 12: SOFT},
    HAT:     {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
    COWBELL: {0: STRONG, 4: SOFT, 6: SOFT, 8: STRONG, 10: SOFT, 12: SOFT, 14: SOFT},
    CLAVE:   {3: STRONG, 5: STRONG, 8: STRONG, 11: STRONG, 14: STRONG},
})

# Cha-Cha  (2-3 son clave, kick on 1/3, characteristic beat on +4)
write_template(LAT_DIR, "cha-cha.dg-pattern", "Cha Cha", LAT_SRC, AFCUBAN, "4/4",
               "Cha-cha-cha; 2-3 son clave, snare on 2/4 plus the 'cha' on +4",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 8: STRONG},
    SNARE: {4: STRONG, 12: STRONG, 14: 80},
    HAT:   {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
    CLAVE: {3: STRONG, 5: STRONG, 8: STRONG, 11: STRONG, 14: STRONG},
})

# Cumbia  (kick on 1, clap on 2/3/4, open hat on and-of-2)
write_template(LAT_DIR, "cumbia.dg-pattern", "Cumbia", LAT_SRC, "latin,colombian", "4/4",
               "Colombian cumbia; kick on 1, claps on 2/3/4, open hat offbeat",
               1, 4, 4, 4, 16, 16, {
    KICK:    {0: STRONG, 4: SOFT},
    CLAP:    {4: STRONG, 8: STRONG, 12: STRONG},
    OPENHAT: {6: STRONG},
    HAT:     {0: 80, 2: 80, 4: 80, 8: 80, 10: 80, 12: 80, 14: 80},
})

# =========================================================================
# CLASSIC drum machine patterns
# =========================================================================
CLS_DIR = os.path.join(REPO_ROOT, "plugins", "drumgen", "patterns", "classic")
CLS_SRC = "Classic Drum Machine Reference"

print(f"\nWriting Classic patterns to: {CLS_DIR}")

# Four on the Floor  (disco/house foundation)
write_template(CLS_DIR, "four-on-floor.dg-pattern", "Four on the Floor", CLS_SRC, "house,disco,techno", "4/4",
               "Kick on every quarter note; the rhythmic foundation of house and disco",
               1, 4, 4, 4, 16, 16, {
    KICK:    {0: STRONG, 4: STRONG, 8: STRONG, 12: STRONG},
    CLAP:    {4: STRONG, 12: STRONG},
    OPENHAT: {2: 80, 6: 80, 10: 80, 14: 80},
})

# Boom Bap  (classic 90s hip-hop)
write_template(CLS_DIR, "boom-bap.dg-pattern", "Boom Bap", CLS_SRC, "hip-hop", "4/4",
               "Classic 90s hip-hop; kick on 1 and +3, snare on 2 and 4",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 10: STRONG},
    SNARE: {4: STRONG, 12: STRONG},
    HAT:   {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
})

# 808 Hip-Hop  (TR-808 trap/hip-hop style)
write_template(CLS_DIR, "hip-hop-808.dg-pattern", "Hip-Hop 808", CLS_SRC, "hip-hop,trap", "4/4",
               "TR-808 hip-hop; syncopated kick, clap on 2/4, 16th hi-hat",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 6: SOFT, 8: STRONG, 14: SOFT},
    CLAP:  {4: STRONG, 12: STRONG},
    HAT:   {0: 80, 1: SOFT, 2: 80, 3: SOFT, 4: 80, 5: SOFT, 6: 80, 7: SOFT,
            8: 80, 9: SOFT, 10: 80, 11: SOFT, 12: 80, 13: SOFT, 14: 80, 15: SOFT},
})

# House / 909  (Roland TR-909 club house)
write_template(CLS_DIR, "house-909.dg-pattern", "House 909", CLS_SRC, "house,techno", "4/4",
               "Roland TR-909 house; four-on-floor, snare on 2/4, 16th hat, open hat on +2/+4",
               1, 4, 4, 4, 16, 16, {
    KICK:    {0: STRONG, 4: STRONG, 8: STRONG, 12: STRONG},
    SNARE:   {4: STRONG, 12: STRONG},
    HAT:     {0: 80, 1: SOFT, 2: 80, 3: SOFT, 4: 80, 5: SOFT,
              8: 80, 9: SOFT, 10: 80, 11: SOFT, 12: 80, 13: SOFT},
    OPENHAT: {6: STRONG, 14: STRONG},
})

# Electro  (TR-808 electro-funk)
write_template(CLS_DIR, "electro.dg-pattern", "Electro", CLS_SRC, "electro,hip-hop", "4/4",
               "TR-808 electro; syncopated kick on 1/a1/3/a3, clap on 2/4",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 3: STRONG, 8: STRONG, 11: STRONG},
    CLAP:  {4: STRONG, 12: STRONG},
    HAT:   {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
})

# Dembow  (reggaeton)
write_template(CLS_DIR, "dembow.dg-pattern", "Dembow", CLS_SRC, "reggaeton,latin", "4/4",
               "Reggaeton dembow; kick on 1/3, clap on 2/4 and the syncopated 'bow' on a4",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 8: STRONG},
    CLAP:  {4: STRONG, 12: STRONG, 14: STRONG},
    HAT:   {2: 80, 6: 80, 10: 80, 14: 80},
})

# Funk  (Clyde Stubblefield / Funky Drummer style)
write_template(CLS_DIR, "funk.dg-pattern", "Funk", CLS_SRC, "funk,soul", "4/4",
               "Funky drummer style; dense 16th hat, syncopated kick, ghost snare on a4",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 8: STRONG, 10: 80},
    SNARE: {4: STRONG, 12: STRONG, 14: SOFT},
    HAT:   {0: 80, 2: 80, 3: SOFT, 4: 80, 5: SOFT, 6: 80,
            8: 80, 10: 80, 11: SOFT, 12: 80, 13: SOFT, 14: 80},
})

# Second Line  (New Orleans)
write_template(CLS_DIR, "second-line.dg-pattern", "Second Line", CLS_SRC, "funk,new-orleans", "4/4",
               "New Orleans second line; syncopated kick on 1/+2/3, snare on 2/+3/+4",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 6: 80, 8: STRONG},
    SNARE: {4: STRONG, 10: 80, 14: STRONG},
    HAT:   {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
})

# Classic Rock
write_template(CLS_DIR, "classic-rock.dg-pattern", "Classic Rock", CLS_SRC, "rock", "4/4",
               "Classic rock; kick on 1/3 with pickup, snare on 2/4, 8th hat, crash on 1",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 8: STRONG, 10: 80},
    SNARE: {4: STRONG, 12: STRONG},
    HAT:   {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80},
    CRASH: {0: STRONG},
})

# Shuffle  (swung 8ths approximated in 16th grid: "and" lands on position 3, 7, 11, 15)
write_template(CLS_DIR, "shuffle.dg-pattern", "Shuffle", CLS_SRC, "blues,rock,swing", "4/4",
               "Blues shuffle; swung 8ths approximated in 16th grid (+beat on positions 3/7/11/15)",
               1, 4, 4, 4, 16, 16, {
    KICK:  {0: STRONG, 8: STRONG},
    SNARE: {4: STRONG, 12: STRONG},
    HAT:   {0: 80, 3: 80, 4: 80, 7: 80, 8: 80, 11: 80, 12: 80, 15: 80},
})

# Breakbeat / DnB  (2 bars, Amen-inspired)
write_template(CLS_DIR, "breakbeat.dg-pattern", "Breakbeat", CLS_SRC, "dnb,jungle,breakbeat", "4/4",
               "Drum-and-bass breakbeat over 2 bars; syncopated kick roll, snare on 3 of each bar",
               2, 4, 4, 4, 16, 32, {
    KICK:    {0: STRONG, 14: 80, 16: STRONG, 22: STRONG},
    SNARE:   {8: STRONG, 24: STRONG, 26: 80},
    HAT:     {0: 80, 2: 80, 4: 80, 6: 80, 8: 80, 10: 80, 12: 80, 14: 80,
              16: 80, 18: 80, 20: 80, 22: 80, 24: 80, 26: 80, 28: 80, 30: 80},
    OPENHAT: {6: STRONG, 22: STRONG, 30: STRONG},
})

# Waltz  (3/4)
write_template(CLS_DIR, "waltz.dg-pattern", "Waltz", CLS_SRC, "classical,pop", "3/4",
               "Classic 3/4 waltz; kick on beat 1, snare/hat on beats 2 and 3",
               1, 3, 4, 4, 12, 12, {
    KICK:  {0: STRONG},
    SNARE: {4: SOFT, 8: SOFT},
    HAT:   {0: STRONG, 4: 80, 8: 80},
})

# =========================================================================
# Final count
# =========================================================================
import glob
all_patterns = glob.glob(os.path.join(REPO_ROOT, "plugins", "drumgen", "patterns", "**", "*.dg-pattern"),
                         recursive=True)
print(f"\nDone. {len(all_patterns)} total pattern files across all directories")
