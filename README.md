# Francois Markovian

A VCV Rack 2 plugin with two modules: **Francois Markovian**, a Markov jump
trigger sequencer, and **Interlock**, an influence-grid trigger processor
(see below).

## Francois Markovian

A trigger sequencer (29 HP). One playhead walks an 8-step pattern
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

# Interlock

![Interlock panel](docs/interlock-preview.png)

A 16 HP trigger processor that makes parts **answer each other**. Six triggers
come in (A to F) and six go out. Patch Francois Markovian's outputs into it, or
Seuperclidean's, or anything else.

## The influence grid

The 6 × 6 grid says "when track X fires, track Y's chance on the **next** step
changes by this much", from −100 to +100. The **row** is the track that fired,
and the **column** is the track that gets pushed.

On every step, each track's chance is:

- **100%** if a trigger came in, **0%** if not,
- plus the influences of every track that fired recently,
- kept between 0 and 100.

So a negative influence can **hold back** an incoming trigger, and a positive one
can add a **ghost hit** where nothing came in.

Example (right-click > *Load the guide's example*):

| Cell | Value | Effect |
|---|---|---|
| A → B | −100 | B can never play straight after A |
| A → C | +50 | C is pushed to play after A |
| C → C | −40 | C damps its own rolls (the diagonal is a track acting on itself) |
| B → D | +60 | D gets ghost hits after B |
| D → A | −30 | A is less likely after D |

**Drag** a cell up (green, push) or down (red, hold back). Shift drags finely,
and **double-click** clears a cell. A row's outline flashes when its track fires.
The **NOW** meters under the grid show how much each track is being pushed on the current step.

## Controls

| Control | What it does |
|---|---|
| **CLOCK** | Each pulse starts a step. Unpatched, the first incoming trigger of a step starts it |
| **RESET** | Clears the memory |
| **AMOUNT** + **CV** | Scales the whole grid. 0 = triggers pass straight through, 100% = as drawn. 0 to 10 V adds 0 to 100% |
| **MEMORY** | 0 = an influence lasts only the next step. 1 to 4 = it lasts that many extra steps, fading out evenly (memory 2: 100%, 67%, 33%) |
| **IN A to F** | Incoming triggers |
| **OUT A to F** | 5 ms triggers |

Influences add up, so two tracks each pushing E by +50 give E a full 100%.

**Timing:** incoming triggers are passed on (or held back) the moment they arrive,
with no delay. Ghost hits are rolled 2 ms after the clock, so triggers coming
from another module (which reach Interlock a sample after the clock) count
before the ghosts are decided. Use the same clock for Interlock and the sequencer feeding it.

Right-click: Clear grid, Load the guide's example, Randomise.

# Building

GitHub Actions tests the engine and builds the Apple Silicon version on every
push to main (see `.github/workflows/build.yml`). The `.vcvplugin` file appears
on the repo's **latest** release page. Locally: download the Rack SDK and run
`make dist RACK_DIR=/path/to/Rack-SDK`.

The panel is drawn by `res/make_panel.py` (needs `pip install fonttools`).
The sequencing engine (`src/MarkovEngine.hpp`) has no Rack dependencies, and
`test/engine_test.cpp` checks it offline: row editing, jump odds, repeats, empty
rows, trigger chances, evolve blending, freeze looping, reset and the clock divider.
Interlock's engine (`src/InfluenceEngine.hpp`) is checked by `test/influence_test.cpp`,
and its panel is drawn by `res/make_panel_interlock.py`.
