// InfluenceEngine.hpp
// The core of Interlock, with no VCV Rack dependencies so it can be tested
// offline (test/influence_test.cpp).
//
// Six trigger tracks come in (A to F) and six go out. Each clock starts a new
// step. A track's chance on a step is its base chance (100% if a trigger came
// in, 0% if not) plus the influence of every track that fired on recent steps:
//
//     grid[X][Y] = how much track X firing changes track Y's chance, -1..+1
//
// MEMORY sets how many steps an influence lasts: 0 = only the next step,
// n = the next step plus n more, fading out evenly. AMOUNT scales everything
// (0 = triggers pass straight through).
//
// Timing: incoming triggers are decided the moment they arrive, so they add no
// delay. A track with no incoming trigger can still get a "ghost" hit from a
// positive influence; those are rolled a moment after the clock (WINDOW), to
// give triggers patched from another module time to arrive first.
#pragma once
#include <cstdint>
#include "MarkovEngine.hpp"  // for Rng, Trigger, clamp01

namespace inf {

using fm::Rng;
using fm::Trigger;

static const int NT = 6;       // tracks
static const int MAX_MEM = 4;  // memory knob 0..4

struct Engine {
	float grid[NT][NT] = {};   // row = fired track, column = influenced track
	float amount = 1.f;        // 0..1
	int memory = 0;            // 0..MAX_MEM
	float trigLen = 0.005f;    // 5 ms
	float window = 0.002f;     // ghost hits are rolled 2 ms after the clock
	uint8_t active = 0x3F;     // bit y = track y is playing; a muted (or missing) track
	                           // passes nothing, gets no ghost hits and pushes nothing

	uint8_t hist[MAX_MEM + 1] = {};  // hist[0] = tracks fired on the previous step, hist[1] the one before...
	uint8_t curMask = 0;       // tracks fired on this step so far
	uint8_t gotInput = 0;      // tracks that had an incoming trigger this step
	bool inStep = false;       // a clock has started a step
	bool ghostsDone = true;
	float windowLeft = 0.f;
	float delta[NT] = {};      // change to each track's chance on this step
	Trigger out[NT];
	Rng rng;

	void clearGrid() {
		for (int i = 0; i < NT; i++)
			for (int j = 0; j < NT; j++) grid[i][j] = 0.f;
	}

	// The example from the guide: A blocks B, A pushes C, C damps itself,
	// B gives D ghost hits, D lowers A.
	void example() {
		clearGrid();
		grid[0][1] = -1.f;
		grid[0][2] = 0.5f;
		grid[2][2] = -0.4f;
		grid[1][3] = 0.6f;
		grid[3][0] = -0.3f;
	}

	void randomise(Rng& r) {
		clearGrid();
		int n = 4 + (int) (r.uniform() * 5);  // 4..8 cells
		for (int k = 0; k < n; k++) {
			int i = (int) (r.uniform() * NT) % NT, j = (int) (r.uniform() * NT) % NT;
			grid[i][j] = (r.uniform() * 2.f - 1.f);
		}
	}

	// How strongly a firing `age` steps ago still counts (age 1 = previous step).
	float fade(int age) const {
		int m = memory < 0 ? 0 : (memory > MAX_MEM ? MAX_MEM : memory);
		if (age < 1 || age > m + 1) return 0.f;
		return 1.f - (float) (age - 1) / (float) (m + 1);
	}

	void computeDelta() {
		for (int y = 0; y < NT; y++) delta[y] = 0.f;
		for (int age = 1; age <= MAX_MEM + 1; age++) {
			float f = fade(age);
			if (f <= 0.f) continue;
			uint8_t m = hist[age - 1];
			for (int x = 0; x < NT; x++)
				if (m & (1 << x))
					for (int y = 0; y < NT; y++) delta[y] += grid[x][y] * f;
		}
		for (int y = 0; y < NT; y++) delta[y] = (active & (1 << y)) ? delta[y] * amount : 0.f;
	}

	float chanceWith(int y, bool input) const { return fm::clamp01((input ? 1.f : 0.f) + delta[y]); }

	void reset() {
		for (int i = 0; i <= MAX_MEM; i++) hist[i] = 0;
		inStep = false;
		curMask = gotInput = 0;
		ghostsDone = true;
	}

	void clock() {
		if (inStep) {
			// the step that just ended becomes history
			for (int i = MAX_MEM; i > 0; i--) hist[i] = hist[i - 1];
			hist[0] = curMask;
		}
		inStep = true;
		curMask = gotInput = 0;
		ghostsDone = false;
		windowLeft = window;
		computeDelta();
	}

	void fire(int y) {
		curMask |= 1 << y;
		out[y].fire(trigLen);
	}

	void input(int y) {
		if (!(active & (1 << y))) return;  // muted: nothing gets through
		if (!inStep) {
			// no clock yet: pass straight through
			fire(y);
			return;
		}
		gotInput |= 1 << y;
		if (curMask & (1 << y)) return;  // already fired this step (a ghost, or a double trigger)
		float p = chanceWith(y, true);
		if (p > 0.f && rng.uniform() < p) fire(y);
	}

	void rollGhosts() {
		ghostsDone = true;
		for (int y = 0; y < NT; y++) {
			if (gotInput & (1 << y)) continue;
			if (!(active & (1 << y))) continue;
			float p = chanceWith(y, false);
			if (p > 0.f && rng.uniform() < p) fire(y);
		}
	}

	// clockEdge first, then each input edge, in the same sample
	void process(float dt, bool clockEdge, bool resetEdge, const bool inEdge[NT]) {
		if (resetEdge) reset();
		if (clockEdge) clock();
		for (int y = 0; y < NT; y++)
			if (inEdge[y]) input(y);
		if (inStep && !ghostsDone) {
			windowLeft -= dt;
			if (windowLeft <= 0.f) rollGhosts();
		}
		for (int y = 0; y < NT; y++) out[y].process(dt);
	}
};

} // namespace inf
