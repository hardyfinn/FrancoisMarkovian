// MarkovEngine.hpp
// The sequencing core of Francois Markovian, with no VCV Rack dependencies so it
// can be tested offline (test/engine_test.cpp).
//
// One playhead walks an 8-step pattern with 6 trigger tracks (A to F). On every
// clock tick it picks its next step from the 8x8 jump matrix row for the step
// it is on, then each track rolls its own chance (0..100%) for the step it
// lands on and fires if the dice say so.
//
// Every matrix row holds chances that add up to 100% (or is empty, which means
// "go to the next step"). Editing one cell rescales the rest of its row.
//
// There are two snapshots, X and Y, each with its own matrix and trigger
// chances. EVOLVE (0..1) blends every matrix cell and every trigger chance
// from X to Y.
//
// FREEZE loops the last 8 ticks exactly as they were played (steps and triggers).
#pragma once
#include <cstdint>
#include <cmath>

namespace fm {

static const int NS = 8;  // steps
static const int NL = 6;  // trigger tracks A..F

// Small fast random generator (xorshift64*), so the engine needs no libraries.
struct Rng {
	uint64_t s = 0x9E3779B97F4A7C15ull;
	void seed(uint64_t v) { s = v ? v : 0x9E3779B97F4A7C15ull; }
	uint64_t next() {
		s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
		return s * 0x2545F4914F6CDD1Dull;
	}
	float uniform() { return (next() >> 40) * (1.f / 16777216.f); }  // [0, 1)
};

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Set cell c of a matrix row to chance v, taking the rest of the row from
// `start` (the row as it was when the edit began) and rescaling it so the row
// adds up to 1. Working from the starting row keeps the other cells' balance
// intact however far the drag goes.
inline void setRowChance(const float startIn[NS], float out[NS], int c, float v, int row) {
	v = clamp01(v);
	// an empty row plays as "next step at 100%", so edit it as that
	float start[NS], sum = 0.f;
	for (int k = 0; k < NS; k++) { start[k] = startIn[k]; sum += start[k]; }
	if (sum <= 1e-6f) start[(row + 1) % NS] = 1.f;
	float others = 0.f;
	for (int k = 0; k < NS; k++) if (k != c) others += start[k];
	if (others > 1e-6f) {
		float scale = (1.f - v) / others;
		for (int k = 0; k < NS; k++) out[k] = (k == c) ? v : start[k] * scale;
	}
	else {
		// nothing else in the row: the remainder goes to "next step",
		// or is spread over the other cells if c is itself the next step
		int next = (row + 1) % NS;
		for (int k = 0; k < NS; k++) out[k] = 0.f;
		out[c] = v;
		if (c != next) out[next] = 1.f - v;
		else for (int k = 0; k < NS; k++) if (k != c) out[k] = (1.f - v) / (NS - 1);
	}
}

struct Snapshot {
	float w[NS][NS];   // jump chances, row = from step, column = to step; rows add up to 1 (or are empty)
	float p[NL][NS];   // trigger chances per track and step, 0..1

	void straight() {
		for (int r = 0; r < NS; r++)
			for (int c = 0; c < NS; c++)
				w[r][c] = (c == (r + 1) % NS) ? 1.f : 0.f;
	}
	void clearTriggers() {
		for (int l = 0; l < NL; l++)
			for (int s = 0; s < NS; s++)
				p[l][s] = 0.f;
	}
	void defaultTriggers() {
		clearTriggers();
		p[0][0] = 1.f;  // A on step 1
		p[1][4] = 1.f;  // B on step 5
	}
	// Make every non-empty row add up to 1 (used after loading or randomising).
	void normalise() {
		for (int r = 0; r < NS; r++) {
			float sum = 0.f;
			for (int c = 0; c < NS; c++) { w[r][c] = clamp01(w[r][c]); sum += w[r][c]; }
			if (sum > 1e-6f) for (int c = 0; c < NS; c++) w[r][c] /= sum;
		}
	}
	// "wild" = false: each row keeps "go to next step" strongest and gets one
	// or two other jumps. "wild" = true: any cell can get any weight.
	void randomise(Rng& rng, bool wild) {
		for (int r = 0; r < NS; r++) {
			for (int c = 0; c < NS; c++) w[r][c] = 0.f;
			if (wild) {
				for (int c = 0; c < NS; c++)
					w[r][c] = rng.uniform() < 0.45f ? rng.uniform() : 0.f;
			}
			else {
				w[r][(r + 1) % NS] = 1.f;
				int extra = 1 + (rng.uniform() < 0.5f);
				for (int k = 0; k < extra; k++) {
					int c = (int) (rng.uniform() * NS) % NS;
					if (c != (r + 1) % NS) w[r][c] = 0.15f + 0.45f * rng.uniform();
				}
			}
		}
		normalise();
	}
	// A small, controlled change: pick 1 to maxCells jump-matrix cells at random
	// and move each up or down by up to `maxChange` (0.5 = 50 points). Each
	// touched row then rescales to 100% the way a drag does.
	void nudge(Rng& rng, int maxCells, float maxChange = 0.5f) {
		if (maxCells < 1) maxCells = 1;
		if (maxCells > NS * NS) maxCells = NS * NS;
		int n = 1 + (int) (rng.uniform() * maxCells);
		if (n > maxCells) n = maxCells;
		int used[NS * NS];
		int nUsed = 0;
		for (int k = 0; k < n; k++) {
			int cell = 0;
			for (int tries = 0; tries < 64; tries++) {
				cell = (int) (rng.uniform() * NS * NS) % (NS * NS);
				bool dup = false;
				for (int i = 0; i < nUsed; i++) if (used[i] == cell) dup = true;
				if (!dup) break;
			}
			used[nUsed++] = cell;
			int r = cell / NS, c = cell % NS;
			float sum = 0.f;
			for (int i = 0; i < NS; i++) sum += w[r][i];
			// an empty row plays as "next step 100%", so nudge it as that
			float cur = sum <= 1e-6f ? (c == (r + 1) % NS ? 1.f : 0.f) : w[r][c];
			// a change of 10 to 50 points (with maxChange 0.5), up or down
			float mag = maxChange * (0.2f + 0.8f * rng.uniform());
			float d = rng.uniform() < 0.5f ? -mag : mag;
			// if the change would hit the end of the range, go the other way
			if (cur + d < 0.f || cur + d > 1.f) d = -d;
			if (cur + d < 0.f) d = -cur;
			if (cur + d > 1.f) d = 1.f - cur;
			int nonZero = 0;
			for (int i = 0; i < NS; i++) if (w[r][i] > 1e-6f) nonZero++;
			if (sum <= 1e-6f || (nonZero == 1 && cur > 1e-6f && d < 0.f)) {
				// lowering the only cell of a row: the freed chance goes to one
				// other step picked at random, so the change stays small
				int c2 = (c + 1 + (int) (rng.uniform() * (NS - 1))) % NS;
				for (int i = 0; i < NS; i++) w[r][i] = 0.f;
				if (d < 0.f) { w[r][c] = cur + d; w[r][c2] = -d; }
				else {
					// an empty row raised on a cell other than the next step
					int next = (r + 1) % NS;
					w[r][c] = d;
					w[r][next] += 1.f - d;
				}
				continue;
			}
			float out[NS];
			setRowChance(w[r], out, c, cur + d, r);
			for (int i = 0; i < NS; i++) w[r][i] = out[i];
		}
	}
};

// A short trigger: high for a fixed time after it fires.
struct Trigger {
	float remaining = 0.f;
	bool high = false;
	void fire(float len) { remaining = len; high = true; }
	void process(float dt) {
		if (remaining > 0.f) {
			remaining -= dt;
			high = true;
			if (remaining <= 0.f) { remaining = 0.f; high = false; }
		}
	}
};

struct Tick {
	uint8_t step = 0;
	uint8_t mask = 0;  // bit l = track l fired
};

struct Engine {
	Snapshot snap[2];        // 0 = X, 1 = Y
	float evolve = 0.f;      // 0 = X, 1 = Y
	bool freeze = false;     // set by the module every block
	float trigLen = 0.005f;  // 5 ms triggers

	// play state
	int step = 0;            // the step the playhead is on (last played)
	int prevStep = -1;       // the step it came from (for the display)
	bool started = false;    // false until the first clock after load/reset
	bool resetPending = false;
	uint8_t lastMask = 0;    // tracks the dice chose on the last tick (muted or not)
	uint8_t lastOut = 0;     // tracks that actually fired on the last tick (not muted)
	uint8_t muted = 0;       // bit l = track l muted (set by the module every block)
	int lastJump = 0;        // how far the last tick jumped: 0 = to the next step, -3..+4
	Trigger phraseTrig;      // fires every NS clocks counted from reset
	Trigger trig[NL];
	Rng rng;

	// history of the last NS ticks, and the frozen loop
	Tick hist[NS];
	int histCount = 0, histPos = 0;  // histPos = where the next tick is written
	Tick loop[NS];
	int loopLen = 0, loopPos = 0;
	bool wasFrozen = false;

	// clock divider: fires on every div-th clock, shifted later by offset clocks.
	// It counts input clocks from reset, whatever the playhead or freeze is doing.
	int div = 1, offset = 0;
	int divCount = 0;
	Trigger divTrig;

	Engine() {
		for (int k = 0; k < 2; k++) {
			snap[k].straight();
			snap[k].defaultTriggers();
		}
	}

	void reset() { resetPending = true; }

	float e() const { return clamp01(evolve); }

	// The blended jump chance for one cell at the current evolve position.
	float weight(int from, int to) const {
		return snap[0].w[from][to] * (1.f - e()) + snap[1].w[from][to] * e();
	}

	// The chance (0..1) of jumping from -> to. An empty row means "go to the next step".
	float chance(int from, int to) const {
		float sum = 0.f;
		for (int c = 0; c < NS; c++) sum += weight(from, c);
		if (sum <= 1e-6f) return to == (from + 1) % NS ? 1.f : 0.f;
		return weight(from, to) / sum;
	}

	// The blended trigger chance for one track and step.
	float trigChance(int lane, int s) const {
		return snap[0].p[lane][s] * (1.f - e()) + snap[1].p[lane][s] * e();
	}

	int chooseNext(int from) {
		float sum = 0.f;
		for (int c = 0; c < NS; c++) sum += weight(from, c);
		if (sum <= 1e-6f) return (from + 1) % NS;
		float r = rng.uniform() * sum;
		for (int c = 0; c < NS; c++) {
			float w = weight(from, c);
			if (w <= 0.f) continue;
			if (r < w) return c;
			r -= w;
		}
		// rounding fell off the end: take the last non-zero cell
		for (int c = NS - 1; c >= 0; c--)
			if (weight(from, c) > 0.f) return c;
		return (from + 1) % NS;
	}

	// Which tracks fire on step s: each rolls its own blended chance.
	uint8_t rollTriggers(int s) {
		uint8_t m = 0;
		for (int l = 0; l < NL; l++) {
			float pr = trigChance(l, s);
			if (pr > 0.f && rng.uniform() < pr) m |= 1 << l;
		}
		return m;
	}

	void startFreeze() {
		// copy the history oldest-first, so the loop replays it in order
		loopLen = histCount;
		for (int i = 0; i < loopLen; i++)
			loop[i] = hist[(histPos - histCount + i + 2 * NS) % NS];
		loopPos = 0;
	}

	void clock() {
		// freeze switches on or off between ticks
		if (freeze && !wasFrozen) startFreeze();
		wasFrozen = freeze;
		bool playedBefore = started && !resetPending;

		divCount = playedBefore ? divCount + 1 : 0;
		int d = div < 1 ? 1 : div;
		if (((divCount - offset) % d + d) % d == 0) divTrig.fire(trigLen);
		if (divCount % NS == 0) phraseTrig.fire(trigLen);

		Tick t;
		if (freeze && loopLen > 0) {
			if (resetPending) loopPos = 0;
			t = loop[loopPos];
			loopPos = (loopPos + 1) % loopLen;
			resetPending = false;
			started = true;
		}
		else {
			int next;
			if (resetPending || !started) next = 0;
			else next = chooseNext(step);
			resetPending = false;
			started = true;
			t.step = (uint8_t) next;
			t.mask = rollTriggers(next);
			// remember it (frozen ticks are not recorded, so the loop stays put)
			hist[histPos] = t;
			histPos = (histPos + 1) % NS;
			if (histCount < NS) histCount++;
		}
		prevStep = playedBefore ? step : -1;
		step = t.step;
		if (prevStep >= 0) {
			int j = ((step - prevStep - 1) % NS + NS) % NS;  // 0 = went to the next step
			lastJump = j > NS / 2 ? j - NS : j;
		}
		else lastJump = 0;
		lastMask = t.mask;
		// muting only silences the output: the dice and the freeze history carry on,
		// so an unmuted track comes back in the same place
		lastOut = t.mask & ~muted;
		for (int l = 0; l < NL; l++)
			if (lastOut & (1 << l)) trig[l].fire(trigLen);
	}

	void process(float dt, bool clockEdge, bool resetEdge) {
		if (resetEdge) reset();
		if (clockEdge) clock();
		for (int l = 0; l < NL; l++) trig[l].process(dt);
		divTrig.process(dt);
		phraseTrig.process(dt);
	}
};

} // namespace fm
