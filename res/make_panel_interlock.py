#!/usr/bin/env python3
# Draws res/Interlock.svg. VCV Rack can't draw SVG <text>, so every
# label is turned into outline paths with fontTools. Positions are in mm and
# must match src/Interlock.cpp.
#   pip install fonttools
#   python3 res/make_panel_interlock.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 16 * 5.08, 128.5  # 16 HP

BG = "#ECE6DA"
INK = "#1E1E24"
SOFT = "#7A7468"
ACCENT = "#C8502D"
OUTBOX = "#2A2A31"
OUTINK = "#ECE6DA"

PCOL = {"TRIG": "#E0692E", "TRIG IN": "#E0692E", "ACT": "#4FA83F", "ACT IN": "#4FA83F", "STATE": "#2BAFB9"}

fonts = {}


def text(s, x, y, size, font=FONT_MED, fill=INK, anchor="middle", spacing=0.04):
    """Return an SVG path for string s; (x, y) is the baseline point in mm."""
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    if anchor == "middle":
        x0 = x - width * scale / 2
    elif anchor == "end":
        x0 = x - width * scale
    else:
        x0 = x
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        tp = TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y))
        gs[n].draw(tp)
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


# layout, same numbers as src/Interlock.cpp
GX0, GY0, GC = 12.0, 20.5, 8.5
NEXT_Y, NEXT_H = 78.6, 4.0
Y_CTRL, Y_IN, Y_OUT = 93.0, 105.2, 118.6
JX0, JDX = 10.0, 12.0
PX, PY_TRIG_IN, PY_ACT_IN = 72.0, 26.5, 38.5
PY_TRIG_OUT, PY_ACT_OUT, PY_STATE = 55.5, 66.5, 77.5
LANES = "ABCDEF"
LANE_COL = ["#E0692E", "#C9A21E", "#2BAFB9", "#9670D8", "#4FA83F", "#D84F8C"]

parts = []
add = parts.append
add(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')
add(text("INTERLOCK", W / 2, 7.6, 3.6, FONT_BOLD, INK, spacing=0.08))
add(text("influence grid \u00b7 francois markovian", W / 2, 11.2, 2.4, FONT_MED, SOFT))
add(f'<rect x="4" y="12.6" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')

for i, s in enumerate(LANES):
    cx = GX0 + (i + 0.5) * GC
    add(text(s, cx, GY0 - 1.5, 2.9, FONT_BOLD, INK))
    add(f'<rect x="{GX0 - 4.8:.2f}" y="{GY0 + i * GC + 2.0:.2f}" width="0.8" height="{GC - 4.0:.2f}" rx="0.3" fill="{LANE_COL[i]}"/>')
    add(text(s, GX0 - 2.4, GY0 + (i + 0.5) * GC + 1.0, 2.9, FONT_BOLD, INK))
add(text("FIRES", GX0 - 4.6, GY0 - 1.5, 2.0, FONT_BOLD, SOFT))
add(text("\u2192 NEXT STEP", GX0 + 6 * GC + 0.6, GY0 - 5.0, 2.2, FONT_BOLD, SOFT, anchor="end"))
add(text("drag up to push, down to hold back", GX0 + 3 * GC, GY0 + 6 * GC + 2.9, 2.0, FONT_MED, SOFT))
add(text("double-click clears", GX0 + 3 * GC, GY0 + 6 * GC + 5.5, 2.0, FONT_MED, SOFT))
add(text("NOW", GX0 - 4.6, NEXT_Y + NEXT_H / 2 + 0.8, 2.0, FONT_BOLD, SOFT))

add(f'<rect x="4" y="85.0" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')
for x, s in [(9, "CLOCK"), (21.5, "RESET"), (34.5, "AMOUNT"), (45.5, "CV"), (57, "MEMORY")]:
    add(text(s, x, Y_CTRL - 4.9, 2.9, FONT_BOLD, INK))
add(text("steps", 70.0, Y_CTRL - 4.9, 2.2, FONT_MED, SOFT))
for i, s in enumerate(LANES):
    add(text(s, JX0 + i * JDX, Y_IN - 4.9, 2.9, FONT_BOLD, INK))
add(text("IN", 3.5, Y_IN + 0.8, 2.2, FONT_BOLD, SOFT))
add(f'<rect x="3" y="110.4" width="{W - 6:.2f}" height="13.2" rx="1.2" fill="{OUTBOX}"/>')
for i, s in enumerate(LANES):
    add(text(s, JX0 + i * JDX, Y_OUT - 4.8, 2.9, FONT_BOLD, OUTINK))

# ---- right column: poly jacks
add(text("POLY", PX, GY0 - 5.0, 2.2, FONT_BOLD, SOFT))
for y, s in [(PY_TRIG_IN, "TRIG IN"), (PY_ACT_IN, "ACT IN")]:
    add(text(s, PX, y - 4.8, 2.9, FONT_BOLD, PCOL[s]))
add(f'<rect x="{PX - 7.0:.2f}" y="{PY_TRIG_OUT - 7.6:.2f}" width="14" height="{PY_STATE - PY_TRIG_OUT + 12.2:.2f}" rx="1.2" fill="{OUTBOX}"/>')
for y, s in [(PY_TRIG_OUT, "TRIG"), (PY_ACT_OUT, "ACT"), (PY_STATE, "STATE")]:
    add(text(s, PX, y - 4.7, 2.9, FONT_BOLD, PCOL[s]))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" '
    f'viewBox="0 0 {W:.2f} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "Interlock.svg"), "w") as fh:
    fh.write(svg)
print("wrote Interlock.svg")
