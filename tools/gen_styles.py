# Writes src/core/fx/FilterStyleTables.h from FabFilter Volcano 3's style files.
#   python3 -I tools/gen_styles.py "/Library/Audio/Plug-Ins/VST3/FabFilter Volcano 3.vst3/Contents/Resources" src/core/fx/FilterStyleTables.h
# A .fil file: "FFil", version, then floats: clipper hardness, the frequency
# axis (log2 Hz, low and high), and three tables, each an int 11 followed by
# 11 x 11 floats (rows = peak 0..1, columns = frequency): clipper drive, cubic
# band damping, effective peak.
import struct, sys, os
res, out = sys.argv[1], sys.argv[2]
# Style order and input gains as Volcano 3 registers them (FUN_000877e8).
styles = [("Classic", "One.fil", 1.8), ("Smooth", "Smooth.fil", 1.0), ("Raw", "Raw.fil", 1.0),
          ("Hard", "Monopoly.fil", 1.4), ("Hollow", "Cheap.fil", 1.0), ("Extreme", "Extreme.fil", 1.0),
          ("Gentle", "Gentle.fil", 1.0), ("Tube", "Tube.fil", 1.0), ("Metal", "Metal.fil", 1.0),
          ("Easy Going", "EasyGoing.fil", 1.0)]
def f(x):
    s = "%.7g" % x
    if "e" not in s and "." not in s: s += ".0"
    return s + "f"
lines = []
w = lines.append
w("#pragma once")
w("")
w("// Generated from FabFilter Volcano 3's style files (Resources/*.fil) by")
w("// tools/gen_styles.py; do not edit. Per style: input gain, clipper hardness h,")
w("// the frequency axis (log2 Hz) and three 11 x 11 tables, rows = peak 0..1,")
w("// columns = frequency: clipper drive, cubic band damping, effective peak.")
w("")
w("namespace ba::style::data")
w("{")
w("    struct Table")
w("    {")
w("        const char* name;")
w("        float gain, hardness, lowLog2, highLog2;")
w("        float clip[11][11], cubic[11][11], peak[11][11];")
w("    };")
w("")
w("    inline constexpr Table kTables[] = {")
for name, fil, gain in styles:
    d = open(os.path.join(res, fil), "rb").read()
    assert d[:4] == b"FFil"
    v = struct.unpack("<369f", d[8:])
    n = struct.unpack("<3i", d[8 + 12:8 + 16] + d[8 + 4 * 125:8 + 4 * 126] + d[8 + 4 * 247:8 + 4 * 248])
    assert n == (11, 11, 11), n
    w("        { \"%s\", %s, %s, %s, %s," % (name, f(gain), f(v[0]), f(v[1]), f(v[2])))
    for t in range(3):
        base = 4 + t * 122
        w("          {")
        for r in range(11):
            row = ", ".join(f(x) for x in v[base + 11 * r: base + 11 * r + 11])
            w("            { %s }," % row)
        w("          }%s" % ("," if t < 2 else " },"))
w("    };")
w("} // namespace ba::style::data")
open(out, "w").write("\n".join(lines) + "\n")
print("wrote", out, len(lines), "lines")
