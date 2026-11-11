#!/usr/bin/env python3
# Draws res/Rewrite.svg. VCV Rack can't draw SVG <text>, so every
# label is turned into outline paths with fontTools. Positions are in mm and
# must match src/Rewrite.cpp.
#   pip install fonttools
#   python3 res/make_panel_rewrite.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 24 * 5.08, 128.5  # 24 HP

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


# layout, same numbers as src/Rewrite.cpp
ROW_Y0, ROW_H = 18.5, 12.5
HIT_X, REST_X, CARD_W, CARD_H = 8.5, 24.0, 14.0, 9.0
EVERY_X, EVERY_W = 39.5, 9.0
X_AMT, X_LOCK = 53.5, 61.5
GEN_X, GEN_W = 66.5, 41.0
X_OUT = 114.0
Y_G1, Y_G2 = 105.5, 117.5
X_G1 = [7.0, 17.5, 29.0, 38.0, 50.0, 59.0, 71.0, 80.0, 92.0, 101.0]
X_G2 = [7.0, 17.5, 28.0, 40.0, 50.0, 63.0, 74.0, 85.0, 96.0]
LANES = "ABCDEF"
LANE_COL = ["#E0692E", "#C9A21E", "#2BAFB9", "#9670D8", "#4FA83F", "#D84F8C"]
TRIG_COL, ACT_COL, STATE_COL = "#E0692E", "#4FA83F", "#2BAFB9"

parts = []
add = parts.append
add(f'<rect width="{W:.2f}" height="{H}" fill="{BG}"/>')
add(text("REWRITE", W / 2, 7.6, 3.6, FONT_BOLD, INK, spacing=0.08))
add(text("per-track pattern grammar \u00b7 francois markovian", W / 2, 10.9, 1.8, FONT_MED, SOFT))
add(f'<rect x="4" y="12.2" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')

# column headings
hy = ROW_Y0 - 1.0
add(text("HIT BECOMES", HIT_X + CARD_W / 2, hy, 1.3, FONT_BOLD, INK))
add(text("REST BECOMES", REST_X + CARD_W / 2, hy, 1.3, FONT_BOLD, INK))
add(text("EVERY", EVERY_X + EVERY_W / 2, hy, 1.3, FONT_BOLD, INK))
add(text("AMOUNT", X_AMT, hy, 1.3, FONT_BOLD, INK))
add(text("LOCK", X_LOCK, hy, 1.3, FONT_BOLD, INK))
add(text("GENERATIONS \u00b7 newest at the bottom", GEN_X, hy, 1.3, FONT_BOLD, INK, anchor="start"))
add(f'<rect x="{X_OUT - 5.0:.2f}" y="{ROW_Y0 - 3.6:.2f}" width="10" height="{6 * ROW_H + 3.4:.2f}" rx="1.2" fill="{OUTBOX}"/>')
add(text("OUT", X_OUT, hy, 1.3, FONT_BOLD, OUTINK))
for i, s in enumerate(LANES):
    cy = ROW_Y0 + i * ROW_H + ROW_H / 2
    add(f'<rect x="2.6" y="{cy - 3.4:.2f}" width="0.8" height="6.8" rx="0.3" fill="{LANE_COL[i]}"/>')
    add(text(s, 5.4, cy + 0.7, 1.9, FONT_BOLD, INK))
    if i < 5:
        add(f'<rect x="8" y="{ROW_Y0 + (i + 1) * ROW_H - 0.1:.2f}" width="{X_OUT - 13.5:.2f}" height="0.2" fill="#D9D1C2"/>')
add(text("click a card to change it \u00b7 shift-click goes back", HIT_X, ROW_Y0 + 6 * ROW_H + 1.9, 1.2, FONT_MED, SOFT, anchor="start"))

add(f'<rect x="4" y="96.9" width="{W - 8:.2f}" height="0.35" fill="{ACCENT}"/>')
g1 = [("CLOCK", X_G1[0]), ("RESET", X_G1[1]), ("SEED", (X_G1[2] + X_G1[3]) / 2),
      ("REWRITE NOW", (X_G1[4] + X_G1[5]) / 2), ("HOLD", (X_G1[6] + X_G1[7]) / 2), ("REWIND", (X_G1[8] + X_G1[9]) / 2)]
for s, x in g1:
    add(text(s, x, Y_G1 - 4.6, 1.45, FONT_BOLD, INK))

add(text("WINDOW", X_G2[0], Y_G2 - 4.6, 1.45, FONT_BOLD, INK))
add(text("CV", X_G2[1], Y_G2 - 4.6, 1.45, FONT_BOLD, INK))
add(text("FIT", X_G2[2], Y_G2 - 4.6, 1.45, FONT_BOLD, INK))
add(text("SQZ", X_G2[2] + 3.4, Y_G2 - 1.4, 1.15, FONT_BOLD, SOFT, anchor="start"))
add(text("WIN", X_G2[2] + 3.4, Y_G2 + 2.6, 1.15, FONT_BOLD, SOFT, anchor="start"))
add(text("TRIG", X_G2[3], Y_G2 - 4.6, 1.45, FONT_BOLD, TRIG_COL))
add(text("ACT", X_G2[4], Y_G2 - 4.6, 1.45, FONT_BOLD, ACT_COL))
add(text("POLY IN", (X_G2[3] + X_G2[4]) / 2, Y_G2 + 5.6, 1.15, FONT_BOLD, SOFT))
add(f'<rect x="{X_G2[5] - 6.0:.2f}" y="{Y_G2 - 7.4:.2f}" width="{X_G2[8] - X_G2[5] + 12.0:.2f}" height="13.4" rx="1.2" fill="{OUTBOX}"/>')
for s, x, c in [("TRIG", X_G2[5], TRIG_COL), ("ACT", X_G2[6], ACT_COL), ("GEN", X_G2[7], OUTINK), ("STATE", X_G2[8], STATE_COL)]:
    add(text(s, x, Y_G2 - 4.4, 1.45, FONT_BOLD, c))
add(text("POLY OUT", (X_G2[5] + X_G2[6]) / 2, Y_G2 + 5.4, 1.15, FONT_BOLD, "#9A9488"))

svg = (
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{W:.2f}mm" height="{H}mm" '
    f'viewBox="0 0 {W:.2f} {H}">\n' + "\n".join(parts) + "\n</svg>\n"
)
with open(os.path.join(HERE, "Rewrite.svg"), "w") as fh:
    fh.write(svg)
print("wrote Rewrite.svg")
