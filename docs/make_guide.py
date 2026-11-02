#!/usr/bin/env python3
# Builds docs/FrancoisMarkovian-Guide.html: one self-contained page with the
# panel pictures embedded.   python3 docs/make_guide.py
import base64, os
HERE = os.path.dirname(os.path.abspath(__file__))

def img(name):
    with open(os.path.join(HERE, name), "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()

FM, IL, GV, EX = img("panel-preview.png"), img("interlock-preview.png"), img("governor-preview.png"), img("influence-example.png")

def table(rows):
    out = '<table><thead><tr><th>Control</th><th>What it does</th></tr></thead><tbody>'
    for a, b in rows:
        out += f"<tr><td><b>{a}</b></td><td>{b}</td></tr>"
    return out + "</tbody></table>"

# signal-flow diagram for the full system
FLOW = """
<svg class="flow" viewBox="0 0 760 320" role="img" aria-label="Francois Markovian sends triggers A to F into Interlock, which sends them to the drums. Governor listens to Interlock's outputs and sends density control to Markovian's Evolve and variety control to Interlock's Amount.">
  <defs><marker id="ar" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <g font-family="Inter, system-ui, sans-serif" font-size="14" fill="currentColor">
    <text x="20" y="34" class="sm">One clock and one reset go to all three modules' CLOCK and RESET inputs.</text>
    <rect x="40" y="80" width="190" height="64" rx="10" class="box fm"/><text x="135" y="107" text-anchor="middle" font-weight="700">Francois Markovian</text><text x="135" y="127" text-anchor="middle" class="sm">jumps + chances</text>
    <rect x="330" y="80" width="170" height="64" rx="10" class="box il"/><text x="415" y="107" text-anchor="middle" font-weight="700">Interlock</text><text x="415" y="127" text-anchor="middle" class="sm">parts answer</text>
    <rect x="600" y="80" width="140" height="64" rx="10" class="box"/><text x="670" y="107" text-anchor="middle" font-weight="700">Drums</text><text x="670" y="127" text-anchor="middle" class="sm">voices / NT</text>
    <rect x="330" y="230" width="170" height="64" rx="10" class="box gv"/><text x="415" y="257" text-anchor="middle" font-weight="700">Governor</text><text x="415" y="277" text-anchor="middle" class="sm">listens, steers</text>
    <g fill="none" stroke="currentColor" stroke-width="2.5">
      <path d="M230 112 H326" marker-end="url(#ar)"/>
      <path d="M500 112 H596" marker-end="url(#ar)"/>
      <path d="M550 112 V262 H504" marker-end="url(#ar)" stroke-width="2"/>
    </g>
    <g fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 5">
      <path d="M330 278 H135 V148" marker-end="url(#ar)"/>
      <path d="M415 230 V148" marker-end="url(#ar)"/>
    </g>
    <text x="278" y="104" text-anchor="middle" class="sm">A–F</text>
    <text x="548" y="104" text-anchor="middle" class="sm">A–F</text>
    <text x="560" y="200" class="sm">listens to A–F</text>
    <text x="150" y="300" class="sm">density CTRL → EVOLVE CV</text>
    <text x="405" y="196" text-anchor="end" class="sm">variety CTRL → AMOUNT CV</text>
  </g>
</svg>
"""

html = f"""<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Francois Markovian Guide</title>
<style>
:root {{
  --bg: #f3efe6; --surface: #ffffff; --ink: #1e1e24; --soft: #5f5a52; --line: #ddd5c6;
  --accent: #c8502d; --orange: #e0692e; --cyan: #1f9aa4; --gold: #b8901a; --tip: #e7f1e0; --chip: #ece5d8;
}}
@media (prefers-color-scheme: dark) {{
  :root:not([data-theme="light"]) {{
    --bg: #15151a; --surface: #1f1f26; --ink: #efeae1; --soft: #aaa397; --line: #34343d;
    --accent: #f07a3e; --orange: #f07a3e; --cyan: #4cd6e0; --gold: #f0c040; --tip: #1f2a1c; --chip: #2a2a32;
  }}
}}
:root[data-theme="dark"] {{
  --bg: #15151a; --surface: #1f1f26; --ink: #efeae1; --soft: #aaa397; --line: #34343d;
  --accent: #f07a3e; --orange: #f07a3e; --cyan: #4cd6e0; --gold: #f0c040; --tip: #1f2a1c; --chip: #2a2a32;
}}
* {{ box-sizing: border-box; }}
html {{ scroll-behavior: smooth; }}
body {{ margin: 0; background: var(--bg); color: var(--ink); font: 16px/1.6 Inter, system-ui, -apple-system, "Segoe UI", sans-serif; }}
a {{ color: var(--accent); }}
.wrap {{ max-width: 1040px; margin: 0 auto; padding: 0 16px; }}
header.hero {{ background: #1e1e24; color: #ece6da; padding: 40px 0 44px; border-bottom: 6px solid #c8502d; }}
header.hero h1 {{ margin: 0; font-size: clamp(28px, 6vw, 50px); letter-spacing: .06em; line-height: 1.1; }}
header.hero p {{ margin: 10px 0 0; color: #f0c040; font-size: 18px; max-width: 680px; }}
nav.toc {{ display: flex; flex-wrap: wrap; gap: 8px; margin: 24px 0 8px; }}
nav.toc a {{ text-decoration: none; background: var(--surface); border: 1px solid var(--line); color: var(--ink); padding: 6px 12px; border-radius: 999px; font-size: 14px; }}
h2 {{ font-size: 28px; margin: 56px 0 12px; padding-top: 10px; border-top: 3px solid var(--accent); }}
h3 {{ font-size: 20px; margin: 28px 0 8px; }}
p, li {{ max-width: 74ch; }}
.lead {{ font-size: 18px; }}
.layout {{ display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); gap: 32px; align-items: start; }}
.layout.narrow {{ grid-template-columns: minmax(0, 360px) minmax(0, 1fr); }}
.panel {{ width: 100%; height: auto; border-radius: 6px; box-shadow: 0 6px 24px rgba(0,0,0,.22); display: block; }}
table {{ border-collapse: collapse; width: 100%; margin: 10px 0 18px; background: var(--surface); border-radius: 10px; overflow: hidden; font-size: 15px; }}
th, td {{ text-align: left; padding: 9px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }}
th {{ background: var(--chip); font-size: 13px; text-transform: uppercase; letter-spacing: .05em; color: var(--soft); }}
td:first-child {{ white-space: nowrap; }}
.tip {{ background: var(--tip); border-radius: 10px; padding: 14px 18px; margin: 16px 0; }}
.tip b:first-child {{ color: var(--accent); }}
.card {{ background: var(--surface); border: 1px solid var(--line); border-left: 5px solid var(--accent); border-radius: 10px; padding: 16px 20px; margin: 14px 0; }}
.card.il {{ border-left-color: var(--gold); }} .card.gv {{ border-left-color: var(--cyan); }}
.card h4 {{ margin: 0 0 6px; font-size: 17px; }}
ol.steps li {{ margin-bottom: 6px; }}
code {{ background: var(--chip); padding: 1px 6px; border-radius: 4px; font-size: 14px; }}
.flow {{ width: 100%; height: auto; color: var(--ink); background: var(--surface); border: 1px solid var(--line); border-radius: 12px; padding: 8px; }}
.flow .box {{ fill: var(--chip); stroke: var(--line); stroke-width: 1.5; }}
.flow .fm {{ stroke: var(--orange); stroke-width: 2.5; }} .flow .il {{ stroke: var(--gold); stroke-width: 2.5; }} .flow .gv {{ stroke: var(--cyan); stroke-width: 2.5; }}
.flow .sm {{ font-size: 12px; fill: var(--soft); }}
.mod {{ display: inline-block; font-size: 12px; font-weight: 700; letter-spacing: .06em; padding: 2px 10px; border-radius: 999px; color: #fff; vertical-align: middle; margin-left: 8px; }}
.mod.fm {{ background: #c8502d; }} .mod.il {{ background: #a07b10; }} .mod.gv {{ background: #1f8a93; }}
footer {{ color: var(--soft); font-size: 14px; padding: 48px 0 64px; }}
@media (max-width: 760px) {{ .layout, .layout.narrow {{ grid-template-columns: 1fr; }} td:first-child {{ white-space: normal; }} }}
</style>
</head>
<body>
<header class="hero"><div class="wrap">
  <h1>FRANCOIS MARKOVIAN</h1>
  <p>Three VCV Rack modules for patterns that jump, answer each other and steer themselves.</p>
</div></header>
<main class="wrap">
<nav class="toc">
  <a href="#overview">Overview</a><a href="#install">Install</a><a href="#start">Quick start</a>
  <a href="#fm">Francois Markovian</a><a href="#il">Interlock</a><a href="#gv">Governor</a>
  <a href="#system">The full system</a><a href="#ideas">Patch ideas</a><a href="#arrange">Arrangement</a>
</nav>

<h2 id="overview">Overview</h2>
<p class="lead">The plugin holds three modules that work on their own and get more interesting together.</p>
<div class="card"><h4>Francois Markovian <span class="mod fm">29 HP</span></h4>
A trigger sequencer whose playhead <b>jumps</b> between 8 steps by chances you draw in a matrix. Six tracks, A to F, each with its own chance per step. Two snapshots (X and Y) and an Evolve control morph between them, Freeze locks what just played, and a clock divider output keeps other gear on the grid.</div>
<div class="card il"><h4>Interlock <span class="mod il">16 HP</span></h4>
A trigger processor where tracks <b>answer each other</b>: when one fires, it raises or lowers the others' chances on the next steps. It can hold hits back or add ghost hits.</div>
<div class="card gv"><h4>Governor <span class="mod gv">18 HP</span></h4>
A feedback controller that <b>listens</b> to the triggers, measures how busy (density) and how changeable (variety) they are, and outputs control CVs that push the music back towards targets you set.</div>

<h2 id="install">Install</h2>
<ol class="steps">
  <li>Copy <code>FrancoisMarkovian-2.3.0-mac-arm64.vcvplugin</code> into your Rack 2 plugins folder (Rack: <i>Help &gt; Open user folder</i>, then <code>plugins-mac-arm64</code>). Replace any older version.</li>
  <li>Restart Rack. The three modules appear under the brand <b>Haer</b>.</li>
</ol>

<h2 id="start">Quick start</h2>
<ol class="steps">
  <li>Add <b>Francois Markovian</b>. Patch a 16th-note clock into <b>CLOCK</b> (from Live via the disting NT and CV Tools, or VCV's Clocked).</li>
  <li>Patch outputs <b>A</b> and <b>B</b> to two drum voices. You'll hear A on step 1 and B on step 5.</li>
  <li>In the matrix, drag the cell at row 4, column 1 up to about 30. Now step 4 sometimes jumps back to step 1.</li>
  <li>Drag some trigger cells on track C up to 40–70% and listen to it come and go.</li>
  <li>Click <b>EDIT Y</b>, draw a busier pattern, and turn <b>EVOLVE</b> slowly from X to Y.</li>
</ol>
<div class="tip"><b>Tip.</b> A new module plays straight (1 to 8 in order), so it behaves like a normal sequencer until you add jumps. Add one jump at a time and listen.</div>

<h2 id="fm">Francois Markovian</h2>
<div class="layout">
  <div><img class="panel" src="{FM}" alt="Francois Markovian panel"></div>
  <div>
    <h3>The jump matrix (left)</h3>
    <p>Each <b>row</b> is the step the playhead is on (FROM), and each <b>column</b> is where it can go next (TO). Every cell shows its chance as a number and a bar, and <b>each row always adds up to 100</b>.</p>
    <ul>
      <li><b>Drag</b> a cell up or down to set its chance. The rest of the row rescales and keeps its own balance: with 30 / 70 in a row, dragging a third cell to 50 leaves 15 / 35 / 50. <b>Shift</b> drags finely.</li>
      <li><b>Double-click</b> switches a cell between 0 and 100.</li>
      <li>The <b>diagonal</b> (a step going to itself) gives stutters and rolls.</li>
      <li>A row with nothing in it falls back to the next step, shown as a small outline.</li>
      <li>The lighter row is where the playhead is now. A white outline marks the jump it just took.</li>
    </ul>
    <h3>Trigger chances (right)</h3>
    <p>Each of the 6 × 8 cells is the chance that the track fires when the playhead lands on that step. Drag or double-click it like the matrix. The playhead's column is lighter, and a cell flashes when it fires.</p>
  </div>
</div>

<h3>Snapshots X and Y, and Evolve</h3>
<p>There are two complete snapshots, each with its own matrix and trigger chances. <b>EDIT X / Y</b> chooses which one the grids show and edit, and a <b>white line</b> in a cell marks the other snapshot's value. <b>EVOLVE</b> blends every cell from X to Y. A trigger at 0 in X and 100 in Y plays half the time at the halfway point. The tick under the X / Y switch shows where Evolve is.</p>

<h3>Freeze</h3>
<p><b>FREEZE</b> (button toggle, or the <b>GATE</b> input while high) loops the <b>last 8 steps exactly as they were played</b>, jumps, triggers and dice included. Let go and the playhead carries on under the matrix again. Reset while frozen restarts the loop.</p>

<h3>Clock divider</h3>
<p><b>DIV</b> (1, 2, 4, 8, 16) and <b>OFFSET</b> (0, 1, 2, 4 pulses) set the <b>DIV</b> output: a trigger on every Nth input clock, counted from Reset and shifted later by the offset. <code>/4 +2</code> fires on clocks 3, 7, 11 and so on. It follows the input clock, not the jumping playhead.</p>

{table([
 ("CLOCK", "Each pulse moves the playhead once."),
 ("RESET", "The next clock plays step 1 and restarts the divider."),
 ("EVOLVE + CV", "Morph X to Y. 0 to 10 V adds 0 to 100%."),
 ("FREEZE + GATE", "Loop the last 8 steps played."),
 ("A to F", "5 ms triggers, 10 V."),
 ("DIV", "Divided clock, 5 ms triggers."),
])}
<p><b>Right-click menu:</b> Straight (reset the matrix of X, Y or both) · Randomise (<i>Gentle</i> keeps "next step" strongest and adds one or two jumps, <i>Wild</i> puts chances anywhere) · Copy X to Y / Y to X · Clear triggers.</p>

<h2 id="il">Interlock</h2>
<div class="layout narrow">
  <div><img class="panel" src="{IL}" alt="Interlock panel"></div>
  <div>
    <p class="lead">The 6 × 6 grid says: <b>when track X fires, track Y's chance on the next step changes by this much</b> (−100 to +100). The row is the track that fired, and the column is the track that gets pushed.</p>
    <p>On every step, each track starts at <b>100%</b> if a trigger came in and <b>0%</b> if not. The influences of recently fired tracks are added on top, kept between 0 and 100. So a red cell can <b>hold back</b> an incoming hit, and a green cell can add a <b>ghost hit</b> where nothing came in.</p>
    <ul>
      <li><b>Drag</b> up (green, push) or down (red, hold back). <b>Double-click</b> clears a cell.</li>
      <li>The <b>NOW</b> meters show how much each track is being pushed on the current step.</li>
      <li>Influences add up: two tracks each pushing E by +50 give E a full 100%.</li>
    </ul>
  </div>
</div>
<h3>Worked example</h3>
<p>Right-click and choose <i>Load the guide's example</i> to get this grid:</p>
<img class="panel" src="{EX}" alt="Influence grid example with an 8-step timeline">
{table([
 ("CLOCK", "Each pulse starts a step. Unpatched, the first incoming trigger of a step starts it. Use the same clock as the sequencer."),
 ("RESET", "Clears the memory."),
 ("AMOUNT + CV", "Scales the whole grid. 0 = straight through, 100% = as drawn."),
 ("MEMORY", "0 = an influence lasts only the next step. 1 to 4 = that many extra steps, fading out (memory 2: 100%, 67%, 33%)."),
 ("IN / OUT A to F", "Triggers in, 5 ms triggers out."),
])}
<div class="tip"><b>Timing.</b> Incoming triggers are passed on (or held back) the moment they arrive, so there's no delay. Ghost hits come 2 ms after the clock, which gives triggers from another module time to arrive first.</div>

<h2 id="gv">Governor</h2>
<div class="layout narrow">
  <div><img class="panel" src="{GV}" alt="Governor panel"></div>
  <div>
    <p class="lead">Governor is a <b>feedback loop</b>: it listens, compares with a target and pushes, over and over. Patch its control CVs into whatever shapes the pattern, and the system steers itself.</p>
    <h3>What it measures</h3>
    <p>Over the last <b>WINDOW</b> clocks (8, 16, 32 or 64, default 16):</p>
    <ul>
      <li><b>Density</b> is the share of possible hits that played.</li>
      <li><b>Variety</b> is how different this window is from the one before it: 0% is an exact repeat and 100% is nothing in common. It starts measuring after two full windows.</li>
    </ul>
    <p>Only <b>connected</b> trigger inputs count.</p>
    <h3>Reading the graphs</h3>
    <p>One point per clock. The coloured line is the measurement, the white line the target, and the yellow line the control it's sending.</p>
  </div>
</div>
<h3>Each loop (density row and variety row)</h3>
{table([
 ("TARGET + CV", "The level to hold. 0 to 10 V adds 0 to 100%."),
 ("POL", "Down (+): raising the control raises the measurement. Up (–): raising the control lowers it. Set this to match what you patched the control into."),
 ("CTRL / INV", "The control, 0 to 10 V, and upside down, 10 to 0 V."),
 ("ERR", "Target minus measured, ±5 V. Positive means it wants more."),
 ("BUSY / SPARSE", "Gates while density is more than 5% above or below target."),
 ("VARIED / SAME", "Gates while variety is more than 5% above or below target."),
])}
<h3>Shared controls</h3>
{table([
 ("CLOCK / RESET", "One pulse per step, the same clock as the sequencer. Reset clears the history."),
 ("WINDOW", "8, 16, 32 or 64 clocks."),
 ("RESPONSE", "How quickly the controls correct, from about 4 clocks (fast) to 256 (slow)."),
 ("WOBBLE", "0 settles smoothly. Towards full it pushes harder with less damping, so it overshoots and <b>hunts</b> around the target."),
])}
<div class="tip"><b>Getting it to steer.</b> Governor can only hold a target if its control actually changes the thing it measures. If a line drifts away from the target and the control pins at 0 or 10 V, flip <b>POL</b>, or make sure the patched control has room to move (for example, make sure Y really is busier than X).</div>

<h2 id="system">The full system</h2>
<p>All three together: the sequencer jumps, Interlock makes the parts answer each other, and Governor listens to the result and steers both.</p>
{FLOW}
<ol class="steps">
  <li>One clock (16ths) goes to all three modules' <b>CLOCK</b> inputs, and one reset goes to all three <b>RESET</b> inputs.</li>
  <li>Markovian <b>A–F</b> to Interlock <b>IN A–F</b>, and Interlock <b>OUT A–F</b> to your drum voices.</li>
  <li>Interlock <b>OUT A–F</b> also go to Governor's <b>A–F</b> inputs (use a multiple, or stacked cables).</li>
  <li>Governor density <b>CTRL</b> to Markovian <b>EVOLVE CV</b>, with EVOLVE knob at 0, and Y as the busier snapshot.</li>
  <li>Governor variety <b>CTRL</b> to Interlock <b>AMOUNT CV</b>, with AMOUNT knob at 0.</li>
</ol>

<h2 id="ideas">Patch ideas</h2>
<div class="card"><h4>Drunk drummer</h4>Straight matrix, then a 20–30% "back to step 1" in rows 4 and 8, and a little diagonal on the hat's steps. Recognisable, but never quite the same bar twice.</div>
<div class="card"><h4>Breath in, breath out</h4>X sparse, Y dense. A slow LFO (or Live automation) into EVOLVE CV gives a pattern that fills and empties over 16–32 bars.</div>
<div class="card il"><h4>Call and response</h4>In Interlock, set A to B at −100 and B to C at +60, with Memory 1. The kick shuts the snare up, and the snare sets off percussion just after.</div>
<div class="card gv"><h4>Self-locking loop</h4>Governor variety <b>VARIED</b> gate to Markovian <b>FREEZE</b> gate. When things get too chaotic it locks into the last bar. Once the loop gets too repetitive, the gate drops and it lets go again.</div>
<div class="card gv"><h4>Hunting groove</h4>Governor density with <b>WOBBLE</b> high and <b>RESPONSE</b> medium. The pattern swells and thins around the target on its own, like a band that can't quite settle.</div>
<div class="card"><h4>Polyrhythm from the divider</h4>DIV at /4 with offset +2 into a hat or a filter envelope: an off-beat pulse that stays on the grid while the main pattern jumps around.</div>

<h2 id="arrange">Arrangement with Live</h2>
<p>Here is how these modules help with loop fatigue: instead of copying clips, you automate a handful of targets.</p>
<ul>
  <li>Send Live's clock and reset to the rig through the disting NT (CV Tools), and use the same pulses in VCV.</li>
  <li>Automate <b>Governor density TARGET</b> per section (intro 20%, groove 45%, peak 75%, breakdown 15%). The system finds its own way to each level, so every pass sounds different.</li>
  <li>Automate <b>Governor variety TARGET</b> low for sections that should lock in, and high for transitions.</li>
  <li>Hit <b>FREEZE</b> (via a Live MIDI-to-gate) for drops and hooks, then let go to release it.</li>
  <li>Record long takes and arrange afterwards: cut the best 8–16-bar moments into scenes.</li>
</ul>

<footer>Francois Markovian 2.3 · made by Haer · source: github.com/hardyfinn/FrancoisMarkovian</footer>
</main>
</body>
</html>
"""
out = os.path.join(HERE, "FrancoisMarkovian-Guide.html")
with open(out, "w") as f:
    f.write(html)
print("wrote", out, len(html) // 1024, "KB")
