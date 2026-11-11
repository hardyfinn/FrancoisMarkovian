#!/usr/bin/env python3
# Draws res/Governor.svg. VCV Rack can't draw SVG <text>, so every
# label is turned into outline paths with fontTools. Positions are in mm and
# must match src/Governor.cpp.
#   pip install fonttools
#   python3 res/make_panel_governor.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 22 * 5.08, 128.5  # 22 HP

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


# layout, same numbers as src/Governor.cpp
GRAPH_Y = [15.0, 46.0]
ROW_Y = [40.0, 71.0]
OUT_X = [49.0, 61.0, 73.0, 86.0, 100.0]
Y_GLOBAL, Y_IN = 86.0, 108.0
IN_X0, IN_DX = 9.0, 12.0
X_POLY_TRIG, X_POLY_ACT = 82.0, 96.0
X_STATE, Y_STATE = 100.0, 119.0
LANE_COL = ["#E0692E", "#C9A21E", "#2BAFB9", "#9670D8", "#4FA83F", "#D84F8C"]

parts = []
add = parts.append
add(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')
add(text("GOVERNOR", W / 2, 7.6, 3.6, FONT_BOLD, INK, spacing=0.08))
add(text("holds density and variety on target \u00b7 francois markovian", W / 2, 11.2, 2.4, FONT_MED, SOFT))
add(f'<rect x="4" y="12.6" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')

def lab(s, x, y, size=1.5, color=INK, font=FONT_BOLD):
    add(text(s, x, y, size, font, color))

gates = [("BUSY", "SPARSE"), ("VARIED", "SAME")]
for k in range(2):
    y = ROW_Y[k]
    lab("TARGET", 10.0, y - 4.8, 3.2)
    lab("CV", 23.0, y - 4.8, 3.2)
    lab("POL", 34.5, y - 4.8, 3.2)
    # CKSS: up = 1 = inverted, down = 0 = normal
    add(text("\u2013", 38.4, y - 1.2, 2.6, FONT_BOLD, SOFT))
    add(text("+", 38.4, y + 3.0, 2.6, FONT_BOLD, SOFT))
    add(f'<rect x="42.4" y="{y - 7.6:.2f}" width="{W - 42.4 - 2.6:.2f}" height="12.2" rx="1.2" fill="{OUTBOX}"/>')
    for x, s in zip(OUT_X, ["CTRL", "INV", "ERR", gates[k][0], gates[k][1]]):
        lab(s, x, y - 4.7, 2.9, OUTINK)

add(f'<rect x="4" y="77.4" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')
for x, s in [(10.0, "CLOCK"), (23.0, "RESET"), (37.0, "WINDOW"), (53.0, "RESPONSE"), (69.0, "WOBBLE")]:
    lab(s, x, Y_GLOBAL - 4.9, 2.9)
add(text("fast \u2013 slow", 53.0, Y_GLOBAL + 7.0, 2.2, FONT_MED, SOFT))
add(text("smooth \u2013 hunt", 69.0, Y_GLOBAL + 7.0, 2.2, FONT_MED, SOFT))
add(f'<rect x="4" y="95.4" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')
add(text("LISTEN TO TRIGGERS", 4.0, 99.6, 2.4, FONT_BOLD, SOFT, anchor="start"))
for i, s in enumerate("ABCDEF"):
    lab(s, IN_X0 + i * IN_DX, Y_IN - 4.9, 2.9)
    add(f'<rect x="{IN_X0 + i * IN_DX - 2.5:.2f}" y="{Y_IN + 4.2:.2f}" width="5" height="0.6" rx="0.3" fill="{LANE_COL[i]}"/>')
add(f'<rect x="{X_POLY_TRIG - 6.5:.2f}" y="{Y_IN - 11.0:.2f}" width="0.25" height="16" fill="#CFC7B8"/>')
add(text("POLY", (X_POLY_TRIG + X_POLY_ACT) / 2, 99.6, 2.4, FONT_BOLD, SOFT))
lab("TRIG", X_POLY_TRIG, Y_IN - 4.9, 2.9, color=PCOL["TRIG"])
lab("ACT", X_POLY_ACT, Y_IN - 4.9, 2.9, color=PCOL["ACT"])
add(text("unpatched inputs don't count", 4.0, Y_IN + 9.0, 2.0, FONT_MED, SOFT, anchor="start"))
add(text("muted tracks (ACT) don't count", 4.0, Y_IN + 11.8, 2.0, FONT_MED, SOFT, anchor="start"))
add(f'<rect x="{X_STATE - 22.0:.2f}" y="{Y_STATE - 5.4:.2f}" width="29" height="10.8" rx="1.2" fill="{OUTBOX}"/>')
add(text("STATE", X_STATE - 6.4, Y_STATE + 0.2, 2.9, FONT_BOLD, PCOL["STATE"], anchor="end"))
add(text("poly \u00b7 6 ch", X_STATE - 6.4, Y_STATE + 3.2, 2.0, FONT_MED, "#9A9488", anchor="end"))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" '
    f'viewBox="0 0 {W:.2f} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "Governor.svg"), "w") as fh:
    fh.write(svg)
print("wrote Governor.svg")
