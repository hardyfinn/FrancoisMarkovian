# Francois Markovian

A VCV Rack 2 trigger sequencer (29 HP). One playhead walks an 8-step pattern
with six trigger tracks, **A to F**. On every clock pulse it picks its next step
from a hand-set **jump matrix**, then each track rolls its own chance for the
step it lands on. The pattern stays recognisable but gets reshuffled, like a
drummer skipping around the bar.

![panel](docs/panel-preview.png)

## The jump matrix (left)

Each **row** is the step the playhead is on (FROM), and each **column** is a
step it can go to (TO). Every cell shows its chance as a number and as a bar,
and **each row always adds up to 100**.

- **Drag a cell up or down** to set its chance. The rest of the row rescales to
  make room and keeps its own balance: with 30 / 70 in a row, dragging a third
  cell to 50 leaves 15 / 35 / 50. Hold Shift for fine control.
- **Double-click** a cell to switch it between 0 and 100.
- Lowering a row's only cell spreads the rest evenly over the other cells.
- A new module plays **straight** (each step goes to the next).
- The **diagonal** (a step going to itself) gives stutters and rolls.
- An empty row (after double-clicking its only cell to 0) falls back to the
  next step, shown as a small outline.
- The lighter row is where the playhead is now, and the white outline shows the jump it just took.

## Trigger chances (right)

Each of the 6 × 8 cells is the chance (0 to 100) that the track fires when the
playhead lands on that step. Drag it up or down, or double-click it for 0 / 100.
The playhead's column is lighter, and a cell flashes when it fires.

A new module starts with **A on step 1** and **B on step 5**, both at 100.

## Snapshots X and Y, and Evolve

There are two snapshots, each with its own **matrix and trigger chances**. The
**EDIT X / Y** switch chooses which one the grids show and edit. A **white line**
in a cell marks the other snapshot's value there.

**EVOLVE** (knob plus CV, 0 to 10 V adds 0 to 100%) blends every matrix cell and
every trigger chance from X to Y. A trigger at 0 in X and 100 in Y fires half the
time at the halfway point. The white tick under the X / Y switch shows where
Evolve is.

## Freeze

The **FREEZE** button (toggle) or **GATE** input (freezes while high) loops the
**last 8 steps exactly as they were played**, steps and triggers, dice and all.
Release it and the playhead carries on from where it is. Reset while frozen
restarts the loop from its start.

## Clock divider

**DIV** (1, 2, 4, 8, 16) and **OFFSET** (0, 1, 2, 4 pulses) set the **DIV** output:
a trigger on every Nth input clock, counted from Reset and shifted later by
the offset. The readout shows both, e.g. `/4 +2` fires on clocks 3, 7, 11 and so on.
It follows the input clock, not the jumping playhead, so it stays on the grid
for syncing other gear.

## Jacks

| Jack | What it does |
|---|---|
| **CLOCK** | Each pulse moves the playhead once |
| **RESET** | The next clock plays step 1 and restarts the divider count |
| **CV** | Evolve CV, added to the knob |
| **GATE** | Freeze gate |
| **A to F** | 5 ms triggers, 10 V |
| **DIV** | Divided clock, 5 ms triggers |

## Right-click menu

- **Straight (step by step)**: reset the matrix of X, Y or both.
- **Randomise**: *Gentle* keeps "go to the next step" strongest in every row
  and adds one or two jumps. *Wild* puts any chance anywhere.
- **Copy X to Y / Copy Y to X**: copies the matrix and triggers. A good way to start Y as a variation of X.
- **Clear triggers**: for X, Y or both.

Everything is saved with the patch. Patches made with version 2.0 load with their
matrix and hits (as 0 / 100 chances) intact.

## Building

GitHub Actions tests the engine and builds the Apple Silicon version on every
push to main (see `.github/workflows/build.yml`). The `.vcvplugin` file appears
on the repo's **latest** release page. Locally: download the Rack SDK and run
`make dist RACK_DIR=/path/to/Rack-SDK`.

The panel is drawn by `res/make_panel.py` (needs `pip install fonttools`).
The sequencing engine (`src/MarkovEngine.hpp`) has no Rack dependencies, and
`test/engine_test.cpp` checks it offline: row editing, jump odds, repeats, empty
rows, trigger chances, evolve blending, freeze looping, reset and the clock divider.
