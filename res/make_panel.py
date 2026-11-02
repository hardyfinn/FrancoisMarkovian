#!/usr/bin/env python3
# Draws res/FrancoisMarkovian.svg. VCV Rack can't draw SVG <text>, so every
# label is turned into outline paths with fontTools. Positions are in mm and
# must match src/FrancoisMarkovian.cpp.
#   pip install fonttools
#   python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 14 * 5.08, 128.5  # 14 HP

BG = "#ECE6DA"
INK = "#1E1E24"
SOFT = "#7A7468"
ACCENT = "#C8502D"
OUTBOX = "#2A2A31"
OUTINK = "#ECE6DA"

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


# layout, same numbers as src/FrancoisMarkovian.cpp
GRID_X0, CELL = 11.5, 6.4
MATRIX_Y0 = 17.0
HITS_Y0, HIT_H = 72.0, 4.6
EDIT_X, EDIT_Y, EDIT_W, EDIT_H = 46.0, 91.8, 16.0, 3.6
Y_CTRL, Y_OUT = 105.5, 118.5
CTRL = [(8.0, "CLOCK"), (18.0, "RESET"), (30.5, "EVOLVE"), (41.0, "CV"), (52.0, "FREEZE"), (62.5, "GATE")]
OUT_X = [13.0, 28.0, 43.0, 58.0]
LANES = ["BD", "SD", "HH", "RIM"]

parts = []
add = parts.append
add(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')

add(text("FRANCOIS MARKOVIAN", W / 2, 7.6, 3.3, FONT_BOLD, INK, spacing=0.06))
add(text("markov jump drums", W / 2, 10.7, 1.8, FONT_MED, SOFT))
add(f'<rect x="4" y="12.0" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')

# matrix: column numbers = "to" step, row numbers = "from" step
for k in range(8):
    cx = GRID_X0 + (k + 0.5) * CELL
    add(text(str(k + 1), cx, MATRIX_Y0 - 1.4, 1.8, FONT_BOLD, INK))
    cy = MATRIX_Y0 + (k + 0.5) * CELL
    add(text(str(k + 1), 8.4, cy + 0.65, 1.8, FONT_BOLD, INK))
add(text("TO", GRID_X0 + 8 * CELL + 2.6, MATRIX_Y0 - 1.4, 1.4, FONT_BOLD, SOFT))
add(text("FROM", 8.4, MATRIX_Y0 - 1.4, 1.2, FONT_BOLD, SOFT))
add(text("drag a cell up or down · double-click for 0 / 100", GRID_X0 + 4 * CELL, 70.6, 1.3, FONT_MED, SOFT))

# drum lanes
for i, s in enumerate(LANES):
    add(text(s, 8.4, HITS_Y0 + (i + 0.5) * HIT_H + 0.55, 1.5, FONT_BOLD, INK))
add(text("dots = hits in the other snapshot", GRID_X0 - 0.6, EDIT_Y + EDIT_H / 2 + 0.5, 1.3, FONT_MED, SOFT, anchor="start"))
add(text("EDIT", EDIT_X - 1.2, EDIT_Y + EDIT_H / 2 + 0.6, 1.6, FONT_BOLD, INK, anchor="end"))

add(f'<rect x="4" y="97.4" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')
for x, s in CTRL:
    add(text(s, x, Y_CTRL - 4.7, 1.55, FONT_BOLD, INK))
add(text("A", 30.5 - 4.7, Y_CTRL + 0.6, 1.5, FONT_BOLD, SOFT))
add(text("B", 30.5 + 4.7, Y_CTRL + 0.6, 1.5, FONT_BOLD, SOFT))

add(f'<rect x="4" y="110.3" width="{W - 8:.2f}" height="12.5" rx="1.2" fill="{OUTBOX}"/>')
for x, s in zip(OUT_X, LANES):
    add(text(s, x, 113.6, 1.6, FONT_BOLD, OUTINK))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" '
    f'viewBox="0 0 {W:.2f} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "FrancoisMarkovian.svg"), "w") as fh:
    fh.write(svg)
print("wrote FrancoisMarkovian.svg")
