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

W, H = 34 * 5.08, 128.5  # 34 HP

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


# layout, same numbers as src/FrancoisMarkovian.cpp
MX0, MY0, MCW, MCH = 11.0, 18.0, 8.0, 9.9
TX0, TY0, TCW, TCH = 85.5, 18.0, 10.0, 7.6
EDIT_X, EDIT_Y, EDIT_W, EDIT_H = 149.5, 70.0, 16.0, 3.6
Y_ROW1, Y_ROW2, Y_OUT = 85.0, 98.0, 116.0
X_COL = [90.0, 104.0, 118.0, 132.0, 146.0, 160.0]
OUT_X0, OUT_DX = 89.0, 71.0 / 6
X_MUTE_CV, X_POLY = 14.0, [34.0, 50.0, 66.0]
LANES = "ABCDEF"
LANE_COL = ["#E0692E", "#C9A21E", "#2BAFB9", "#9670D8", "#4FA83F", "#D84F8C"]

parts = []
add = parts.append
add(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')

add(text("FRANCOIS MARKOVIAN", W / 2, 7.6, 3.6, FONT_BOLD, INK, spacing=0.06))
add(text("markov jump trigger sequencer", W / 2, 11.2, 2.4, FONT_MED, SOFT))
add(f'<rect x="4" y="12.6" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')

# ---- left: jump matrix. Columns = "to" step, rows = "from" step
for k in range(8):
    add(text(str(k + 1), MX0 + (k + 0.5) * MCW, MY0 - 1.3, 2.9, FONT_BOLD, INK))
    add(text(str(k + 1), 7.0, MY0 + (k + 0.5) * MCH + 1.0, 2.9, FONT_BOLD, INK))
add(text("FROM", 6.4, MY0 - 1.3, 1.9, FONT_BOLD, SOFT))
add(text("TO", MX0 + 8 * MCW + 2.6, MY0 - 1.3, 1.9, FONT_BOLD, SOFT))
add(text("drag a cell up or down \u00b7 rows add up to 100 \u00b7 double-click 0 / 100", MX0 + 4 * MCW - 1.5, MY0 + 8 * MCH + 4.0, 2.0, FONT_MED, SOFT))
add(f'<rect x="{MX0 + 8 * MCW + 4.6:.2f}" y="15" width="0.25" height="104" fill="#CFC7B8"/>')

# ---- right: trigger chances
for k in range(8):
    add(text(str(k + 1), TX0 + (k + 0.5) * TCW, TY0 - 1.3, 2.9, FONT_BOLD, INK))
for i, s in enumerate(LANES):
    add(f'<rect x="{TX0 - 4.6:.2f}" y="{TY0 + i * TCH + 1.2:.2f}" width="0.8" height="{TCH - 2.4:.2f}" rx="0.3" fill="{LANE_COL[i]}"/>')
    add(text(s, TX0 - 2.2, TY0 + (i + 0.5) * TCH + 1.0, 2.9, FONT_BOLD, INK))
add(text("click a letter to mute", TX0 - 5.2, TY0 + 6 * TCH + 7.0, 2.0, FONT_MED, SOFT, anchor="start"))
add(text("trigger chance per step \u00b7 white line = other snapshot", TX0 - 5.2, TY0 + 6 * TCH + 3.8, 2.2, FONT_MED, SOFT, anchor="start"))
add(text("EDIT", EDIT_X - 1.4, EDIT_Y + EDIT_H / 2 + 1.0, 2.9, FONT_BOLD, INK, anchor="end"))

add(f'<rect x="{TX0 - 5:.2f}" y="76.5" width="{W - 4 - TX0 + 5:.2f}" height="0.35" fill="{ACCENT}"/>')
for x, s in zip(X_COL, ["CLOCK", "RESET", "EVOLVE", "CV", "FREEZE", "GATE"]):
    add(text(s, x, Y_ROW1 - 5.0, 2.9, FONT_BOLD, INK))
add(text("X", X_COL[2] - 5.4, Y_ROW1 + 0.9, 2.4, FONT_BOLD, SOFT))
add(text("Y", X_COL[2] + 5.4, Y_ROW1 + 0.9, 2.4, FONT_BOLD, SOFT))
add(text("DIV", X_COL[0], Y_ROW2 - 5.0, 2.9, FONT_BOLD, INK))
add(text("OFFSET", X_COL[1], Y_ROW2 - 5.0, 2.9, FONT_BOLD, INK))
add(text("clock divider", 124.0, Y_ROW2 - 5.0, 2.2, FONT_MED, SOFT))

add(f'<rect x="{TX0 - 5:.2f}" y="104.4" width="{W - 4 - TX0 + 5:.2f}" height="17.8" rx="1.2" fill="{OUTBOX}"/>')
for i, s in enumerate(list(LANES) + ["DIV"]):
    add(text(s, OUT_X0 + i * OUT_DX, Y_OUT - 5.0, 2.9, FONT_BOLD, OUTINK))

# ---- bottom left: mute CV and the poly outputs
add(text("MUTE", X_MUTE_CV, Y_OUT - 8.4, 2.9, FONT_BOLD, INK))
add(text("CV", X_MUTE_CV, Y_OUT - 5.0, 2.9, FONT_BOLD, INK))
add(f'<rect x="24" y="104.4" width="53.5" height="17.8" rx="1.2" fill="{OUTBOX}"/>')
for x, s in zip(X_POLY, ["TRIG", "ACT", "STATE"]):
    add(text(s, x, Y_OUT - 5.0, 2.9, FONT_BOLD, PCOL[s]))
add(text("POLY OUT \u00b7 6 CH", 50.0, Y_OUT - 8.6, 2.0, FONT_BOLD, "#9A9488"))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" '
    f'viewBox="0 0 {W:.2f} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "FrancoisMarkovian.svg"), "w") as fh:
    fh.write(svg)
print("wrote FrancoisMarkovian.svg")

# --- Haer maker logo (res/haer_logo.py), placed clear of the jacks and rail artwork
import os as _os, sys as _sys
_sys.path.insert(0, _os.path.dirname(_os.path.abspath(__file__)))
import haer_logo
haer_logo.add_to_file(_os.path.join(_os.path.dirname(_os.path.abspath(__file__)), "FrancoisMarkovian.svg"), x=86.39, y=126.0, cap=3.0, bg='#ECE6DA')
