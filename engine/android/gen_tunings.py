#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# The phone's tunings and scales -> engine/android/tunings_android.h (read in place of the FM-1's tunings.h).
# Quorum's own list (its tools/gen_tables.py) with the phone's additions:
#   SUPERJUST (Wendy Carlos) after 12-TET (older patches' tunings are moved up one as they load: dinghy.c, ver 3)
#   22-EDO: the temperaments' scales (porcupine, orwell, hedgehog, pajara)
#   scales for the tunings that had only ALL STEPS, and a few more for the others
#   the Carlos tunings repeat at their "fifth" (9, 11, 20 steps) so they can have scales; ALL STEPS is as before
# A tuning: its steps in cents within one period (the octave, or not: Bohlen-Pierce's 3:1, the Carlos fifths), and
# the scales over it (which of those steps are allowed). Pitches in the firmware: 1/256 semitone (a cent is 2.56).
#   python3 engine/android/gen_tunings.py
import math
import os


def cents(r):
    return 1200 * math.log2(r)


def ratios(*rs):
    return [cents(eval(r)) for r in rs]


def edo(n, period=1200.0):
    return [period * i / n for i in range(n)]


def mos(n, gen, lo, hi, period=None):
    """a temperament's scale in n-EDO: the generator (steps) stacked lo..hi times, within each period (steps)"""
    period = period or n
    base = sorted({(g * gen) % period for g in range(lo, hi + 1)})
    return [b + p for p in range(0, n, period) for b in base]


def near(step, r):
    """the step of an equal tuning (cents a step) nearest a ratio"""
    return round(cents(r) / step)


S12 = [("CHROMATIC", range(12)), ("MAJOR", [0, 2, 4, 5, 7, 9, 11]), ("MINOR", [0, 2, 3, 5, 7, 8, 10]),
       ("DORIAN", [0, 2, 3, 5, 7, 9, 10]), ("PHRYGIAN", [0, 1, 3, 5, 7, 8, 10]), ("LYDIAN", [0, 2, 4, 6, 7, 9, 11]),
       ("MIXOLYDIAN", [0, 2, 4, 5, 7, 9, 10]), ("LOCRIAN", [0, 1, 3, 5, 6, 8, 10]), ("HARM MINOR", [0, 2, 3, 5, 7, 8, 11]),
       ("MAJ PENTA", [0, 2, 4, 7, 9]), ("MIN PENTA", [0, 3, 5, 7, 10]), ("WHOLE TONE", [0, 2, 4, 6, 8, 10]),
       ("4TH + 5TH", [0, 5, 7]), ("FIFTHS", [0, 7]), ("OCTAVES", [0])]
ALL = lambda n: [("ALL STEPS", range(n))]


def carlos(name, step, n):
    """a Carlos tuning, repeating at n steps (its near-3:2): its thirds as chains, its tetrachord"""
    sc = ALL(n) + [("MAJ THIRDS", [0, near(step, 5 / 4)]), ("MIN THIRDS", [0, near(step, 6 / 5)]),
                   ("BOTH THIRDS", [0, near(step, 6 / 5), near(step, 5 / 4)])]
    if step < 70:                               # (beta, gamma: a whole tone near enough for a tetrachord)
        sc.append(("TETRACHORD", [0, near(step, 9 / 8), near(step, 5 / 4), near(step, 4 / 3)]))
    return (name, step * n, edo(n, step * n), sc)


BP = cents(3) / 13
TUNINGS = [   # (name, period in cents, steps in cents, scales)
    ("12-TET", 1200, edo(12), S12),
    ("SUPERJUST", 1200, ratios("1", "17/16", "9/8", "6/5", "5/4", "4/3", "11/8", "3/2", "13/8", "5/3", "7/4", "15/8"),
     S12 + [("OVERTONE", [0, 2, 4, 6, 7, 8, 10]), ("HARMONIC 7", [0, 4, 7, 10])]),
    ("JUST 5-LIMIT", 1200, ratios("1", "16/15", "9/8", "6/5", "5/4", "4/3", "45/32", "3/2", "8/5", "5/3", "9/5", "15/8"), S12),
    ("PYTHAGOREAN", 1200, ratios("1", "256/243", "9/8", "32/27", "81/64", "4/3", "729/512", "3/2", "128/81", "27/16",
                                 "16/9", "243/128"), S12),
    ("MEANTONE 1/4", 1200, [0, 76.0, 193.2, 310.3, 386.3, 503.4, 579.5, 696.6, 772.6, 889.7, 1006.8, 1082.9], S12),
    ("SHRUTI 22", 1200, ratios("1", "256/243", "16/15", "10/9", "9/8", "32/27", "6/5", "5/4", "81/64", "4/3", "27/20",
                               "45/32", "729/512", "3/2", "128/81", "8/5", "5/3", "27/16", "16/9", "9/5", "15/8", "243/128"),
     ALL(22) + [("BILAWAL", [0, 4, 7, 9, 13, 16, 20]), ("BHAIRAV", [0, 2, 7, 9, 13, 15, 20]),
                ("YAMAN", [0, 4, 7, 11, 13, 16, 20]), ("KAFI", [0, 4, 6, 9, 13, 16, 19]),
                ("BHAIRAVI", [0, 2, 6, 9, 13, 15, 19]), ("TODI", [0, 1, 5, 12, 13, 14, 20])]),
    ("5-EDO", 1200, edo(5), ALL(5) + [("QUARTAL", [0, 2, 4]), ("4TH + 5TH", [0, 2, 3])]),
    ("7-EDO", 1200, edo(7), ALL(7) + [("THAI PENTA", [0, 1, 2, 4, 5]), ("NEUTRAL TRI", [0, 2, 4]),
                                      ("4TH + 5TH", [0, 3, 4])]),
    ("17-EDO", 1200, edo(17), ALL(17) + [("DIATONIC", [0, 3, 6, 7, 10, 13, 16]), ("MAQAM RAST", [0, 3, 5, 7, 10, 13, 15]),
                                         ("PENTA", [0, 3, 6, 10, 13])]),
    ("19-EDO", 1200, edo(19), ALL(19) + [("DIATONIC", [0, 3, 6, 8, 11, 14, 17]), ("PENTA", [0, 3, 6, 11, 14]),
                                         ("MINOR", [0, 3, 5, 8, 11, 13, 16])]),
    ("22-EDO", 1200, edo(22), ALL(22) + [("SUPERPYTH", [0, 4, 8, 9, 13, 17, 21]),
                                         ("PORCUPINE 7", mos(22, 3, -3, 3)), ("PORCUPINE 8", mos(22, 3, -3, 4)),
                                         ("ORWELL 9", mos(22, 5, -4, 4)),
                                         ("HEDGEHOG 6", mos(22, 3, -1, 1, 11)), ("HEDGEHOG 8", mos(22, 3, -1, 2, 11)),
                                         ("PAJARA 10", mos(22, 2, -2, 2, 11))]),
    ("24-EDO", 1200, edo(24), ALL(24) + [("MAQAM RAST", [0, 4, 7, 10, 14, 18, 21]),
                                         ("MAQAM BAYATI", [0, 3, 6, 10, 14, 16, 20]),
                                         ("MAQAM SABA", [0, 3, 6, 8, 14, 16, 20])]),
    ("31-EDO", 1200, edo(31), ALL(31) + [("DIATONIC", [0, 5, 10, 13, 18, 23, 28]), ("MINOR", [0, 5, 8, 13, 18, 21, 26]),
                                         ("ORWELL 9", mos(31, 7, -4, 4))]),
    ("53-EDO", 1200, edo(53), ALL(53) + [("JUST MAJOR", [0, 9, 17, 22, 31, 39, 48]),
                                         ("JUST MINOR", [0, 9, 14, 22, 31, 36, 45])]),
    ("HARMONIC 8", 1200, [cents(k / 8) for k in range(8, 16)], ALL(8) + [("ODD ONLY", [0, 1, 3, 5, 7])]),
    ("HARMONIC 16", 1200, [cents(k / 16) for k in range(16, 32)],
     ALL(16) + [("PRIMES", [0, 1, 3, 7, 13, 15])]),
    ("SLENDRO", 1200, [0, 228, 484, 728, 960], ALL(5) + [("QUARTAL", [0, 2, 4])]),
    ("PELOG", 1200, [0, 120, 270, 540, 670, 785, 950], ALL(7) + [("BEM", [0, 1, 2, 4, 5]), ("BARANG", [1, 2, 4, 5, 6])]),
    ("BOHLEN-PIERCE", cents(3), edo(13, cents(3)), ALL(13) + [("LAMBDA", [0, 2, 3, 4, 6, 7, 9, 10, 12]),
                                                              ("3:5:7 TRIAD", [0, near(BP, 5 / 3), near(BP, 7 / 3)]),
                                                              ("5:7:9 TRIAD", [0, near(BP, 7 / 5), near(BP, 9 / 5)])]),
    carlos("CARLOS ALPHA", 78.0, 9),
    carlos("CARLOS BETA", 63.8, 11),
    carlos("CARLOS GAMMA", 35.1, 20),
]


def main():
    q8 = lambda c: round(c * 2.56)
    steps, off, scl_name, lo, hi, scl0, nscl = [], [], [], [], [], [], []
    for name, period, st, scales in TUNINGS:
        assert len(st) <= 53 and st[0] == 0 and len(name) <= 20
        off.append(len(steps))
        steps += [q8(c) for c in st]
        scl0.append(len(scl_name))
        nscl.append(len(scales))
        for sname, sel in scales:
            sel = list(sel)
            assert len(sname) <= 14 and all(0 <= i < len(st) for i in sel) and len(set(sel)) == len(sel), (name, sname, sel)
            m = 0
            for i in sel:
                m |= 1 << i
            scl_name.append(sname)
            lo.append(m & 0xFFFFFFFF)
            hi.append(m >> 32)
    arr = lambda name, typ, v: f"static const {typ} {name}[{len(v)}] = {{" + ",".join(map(str, v)) + "};"
    strs = lambda name, v: f"static const char *const {name}[{len(v)}] = {{" + ", ".join(f'"{x}"' for x in v) + "};"
    lines = [
        "/* SPDX-License-Identifier: GPL-3.0-only */",
        "/* Generated by engine/android/gen_tunings.py: do not edit by hand. Pitches in 1/256 semitone. */",
        f"#define NTUN {len(TUNINGS)}u",
        f"#define NSCL {len(scl_name)}u",
        strs("TUN_NAME", [t[0] for t in TUNINGS]),
        arr("TUN_PERIOD", "uint16_t", [q8(t[1]) for t in TUNINGS]),
        arr("TUN_N", "uint8_t", [len(t[2]) for t in TUNINGS]),
        arr("TUN_OFF", "uint16_t", off),
        arr("TUN_STEPS", "uint16_t", steps),
        arr("TUN_SCL0", "uint8_t", scl0),
        arr("TUN_NSCL", "uint8_t", nscl),
        strs("SCL_NAME", scl_name),
        arr("SCL_LO", "uint32_t", lo),
        arr("SCL_HI", "uint32_t", hi),
    ]
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tunings_android.h")
    open(out, "w").write("\n".join(lines) + "\n")
    print(f"tunings_android.h: {len(TUNINGS)} tunings, {len(scl_name)} scales")
    for name, period, st, scales in TUNINGS:   # (what each new scale came to, to check by ear and eye)
        for sname, sel in scales:
            if sname != "ALL STEPS" and name not in ("12-TET", "JUST 5-LIMIT", "PYTHAGOREAN", "MEANTONE 1/4"):
                print(f"  {name:14} {sname:14} " + " ".join(f"{st[i]:.0f}" for i in sel))


if __name__ == "__main__":
    main()
