# Francois Markovian

A VCV Rack 2 drum sequencer. One playhead walks an 8-step pattern with four
lanes (bass drum, snare, closed hat, rim). On every clock pulse it picks its
next step from a hand-set **jump matrix**, then fires whichever lanes are on at
the step it lands on. The pattern stays recognisable but gets reshuffled, like
a drummer skipping around the bar.

![panel](docs/panel-preview.png)

## The jump matrix

Each **row** is the step the playhead is on (FROM), and each **column** is a
step it can go to (TO). Cells hold weights, and the number in a cell is the
actual chance of that jump once the row is shared out.

- **Drag a cell up or down** to set its weight. Hold Shift for fine control.
- **Double-click** a cell to switch it between 0 and full.
- A new module plays **straight** (each step goes to the next), so it behaves
  like a normal sequencer until you add jumps.
- The **diagonal** (a step going to itself) gives stutters and rolls.
- A row with nothing in it falls back to the next step (shown as a small outlined square).
- The lighter row is where the playhead is now, and the white outline shows the jump it just took.

Example: in row 4, put 70 in column 5 and 30 in column 1. Step 4 then goes on
to step 5 most of the time and jumps back to the top of the bar about one time in three.

## Drum hits

Click a cell in the BD / SD / HH / RIM grid to switch a hit on or off. The
playhead's column is lighter, and a hit flashes white when it fires.

## Snapshots A and B, and Evolve

There are two snapshots, each with its own **matrix and hits**. The **EDIT A / B**
switch chooses which one the grids show and edit. A small dot in a hit cell
means the *other* snapshot has a hit there.

**EVOLVE** (knob plus CV, 0 to 10 V adds 0 to 100%) moves from A to B:

- The matrix **blends smoothly**: at 50% every weight is halfway between A and B.
- Hits that are the same in A and B always play (or never play).
- Hits that differ **roll dice on every pass**: at 30%, a hit that's only in B
  plays 30% of the time and a hit that's only in A plays 70% of the time.

The white tick under the A / B switch shows where Evolve is.

## Freeze

The **FREEZE** button (toggle) or **GATE** input (freezes while high) loops the
**last 8 steps exactly as they were played**, steps and hits, dice and all.
Release it and the playhead carries on from where it is, back under the
matrix's control. Reset while frozen restarts the loop from its start.

## Clock, reset and outputs

| Jack | What it does |
|---|---|
| **CLOCK** | Each pulse moves the playhead once (send 16ths for a half-bar pattern, or 8ths for a full bar) |
| **RESET** | The next clock plays step 1 |
| **CV** | Evolve CV, added to the knob |
| **GATE** | Freeze gate |
| **BD / SD / HH / RIM** | 5 ms triggers, 10 V |

## Right-click menu

- **Straight (step by step)**: reset the matrix of A, B or both.
- **Randomise**: *Gentle* keeps "go to the next step" strongest in every row
  and adds one or two jumps. *Wild* puts any weight anywhere.
- **Copy A to B / Copy B to A**: copies matrix and hits. A good way to start B as a variation of A.
- **Clear hits**: for A, B or both.

Everything (both snapshots, Edit, Freeze and the frozen loop) is saved with the patch.

## Building

GitHub Actions tests the engine and builds the Apple Silicon version on every
push to main (see `.github/workflows/build.yml`). The `.vcvplugin` file appears
on the repo's **latest** release page. Locally: download the Rack SDK and run
`make dist RACK_DIR=/path/to/Rack-SDK`.

The panel is drawn by `res/make_panel.py` (needs `pip install fonttools`).
The sequencing engine (`src/MarkovEngine.hpp`) has no Rack dependencies, and
`test/engine_test.cpp` checks it offline: jump odds, repeats, empty rows,
evolve blending and dice, freeze looping, and reset.
