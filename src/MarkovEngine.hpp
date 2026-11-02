// MarkovEngine.hpp
// The sequencing core of Francois Markovian, with no VCV Rack dependencies so it
// can be tested offline (test/engine_test.cpp).
//
// One playhead walks an 8-step, 4-lane drum pattern. On every clock tick it
// picks its next step from the 8x8 jump matrix row for the step it is on, then
// fires whichever lanes are switched on at the step it lands on.
//
// There are two snapshots, A and B, each with its own jump matrix and its own
// hits. EVOLVE (0..1) blends the matrices smoothly, and for each hit that
// differs between A and B it rolls dice: at evolve 0.3 that hit comes from B
// 30% of the time.
//
// FREEZE loops the last 8 ticks exactly as they were played (steps and hits).
#pragma once
#include <cstdint>
#include <cmath>

namespace fm {

static const int NS = 8;  // steps
static const int NL = 4;  // lanes: bass drum, snare, closed hat, rim

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

struct Snapshot {
	float w[NS][NS];     // jump weights, row = from step, column = to step, 0..1
	bool hit[NL][NS];    // drum hits

	void straight() {
		for (int r = 0; r < NS; r++)
			for (int c = 0; c < NS; c++)
				w[r][c] = (c == (r + 1) % NS) ? 1.f : 0.f;
	}
	void clearHits() {
		for (int l = 0; l < NL; l++)
			for (int s = 0; s < NS; s++)
				hit[l][s] = false;
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
	uint8_t mask = 0;  // bit l = lane l fired
};

struct Engine {
	Snapshot snap[2];
	float evolve = 0.f;      // 0 = A, 1 = B
	bool freeze = false;     // set by the module every block
	float trigLen = 0.005f;  // 5 ms triggers

	// play state
	int step = 0;            // the step the playhead is on (last played)
	int prevStep = -1;       // the step it came from (for the display)
	bool started = false;    // false until the first clock after load/reset
	bool resetPending = false;
	uint8_t lastMask = 0;    // lanes fired on the last tick
	Trigger trig[NL];
	Rng rng;

	// history of the last NS ticks, and the frozen loop
	Tick hist[NS];
	int histCount = 0, histPos = 0;  // histPos = where the next tick is written
	Tick loop[NS];
	int loopLen = 0, loopPos = 0;
	bool wasFrozen = false;

	Engine() {
		snap[0].straight();
		snap[1].straight();
		defaultHits();
	}

	void defaultHits() {
		// A: four to the floor style; B: broken, busier
		const char* a[NL] = {"x...x...", "....x...", "..x...x.", "...x...x"};
		const char* b[NL] = {"x..x..x.", "....x..x", "x.xxx.xx", ".x...x.."};
		for (int l = 0; l < NL; l++)
			for (int s = 0; s < NS; s++) {
				snap[0].hit[l][s] = a[l][s] == 'x';
				snap[1].hit[l][s] = b[l][s] == 'x';
			}
	}

	void reset() { resetPending = true; }

	// The blended jump weight for one cell at the current evolve position.
	float weight(int from, int to) const {
		float e = evolve < 0.f ? 0.f : (evolve > 1.f ? 1.f : evolve);
		return snap[0].w[from][to] * (1.f - e) + snap[1].w[from][to] * e;
	}

	// The chance (0..1) of jumping from -> to, after normalising the row.
	// An empty row means "go to the next step".
	float chance(int from, int to) const {
		float sum = 0.f;
		for (int c = 0; c < NS; c++) sum += weight(from, c);
		if (sum <= 1e-6f) return to == (from + 1) % NS ? 1.f : 0.f;
		return weight(from, to) / sum;
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

	// Which lanes fire on step s: shared hits always, differing hits by dice.
	uint8_t rollHits(int s) {
		float e = evolve < 0.f ? 0.f : (evolve > 1.f ? 1.f : evolve);
		uint8_t m = 0;
		for (int l = 0; l < NL; l++) {
			bool a = snap[0].hit[l][s], b = snap[1].hit[l][s];
			bool on = (a == b) ? a : (rng.uniform() < e ? b : a);
			if (on) m |= 1 << l;
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
			t.mask = rollHits(next);
			// remember it (frozen ticks are not recorded, so the loop stays put)
			hist[histPos] = t;
			histPos = (histPos + 1) % NS;
			if (histCount < NS) histCount++;
		}
		prevStep = playedBefore ? step : -1;
		step = t.step;
		lastMask = t.mask;
		for (int l = 0; l < NL; l++)
			if (t.mask & (1 << l)) trig[l].fire(trigLen);
	}

	void process(float dt, bool clockEdge, bool resetEdge) {
		if (resetEdge) reset();
		if (clockEdge) clock();
		for (int l = 0; l < NL; l++) trig[l].process(dt);
	}
};

} // namespace fm
