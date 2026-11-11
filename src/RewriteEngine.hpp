// RewriteEngine.hpp
// The core of Rewrite, with no VCV Rack dependencies so it can be tested
// offline (test/rewrite_test.cpp).
//
// Rewrite holds a one-bar phrase (8 or 16 steps) for each of 6 tracks and grows
// it one generation at a time. At a rewrite, every step of a track's phrase is
// swapped for that track's replacement pattern: a hit becomes the HIT rule
// (for example hit-rest), a rest becomes the REST rule (for example rest-hit).
// AMOUNT is the chance that each step is actually swapped.
//
// The swapped phrase is longer than a bar, so FIT folds it back to one bar:
//   Window:  keep a one-bar slice of it, chosen by WINDOW (0 = the start)
//   Squeeze: sample the whole thing evenly back into one bar
//
// The default rules (hit -> hit rest, rest -> rest hit) are the Thue-Morse
// substitution: a famous pattern that never settles into a plain repeat.
//
// Before SEED is pressed, triggers pass straight through, and the last bar of
// them is remembered. SEED makes that bar the starting phrase of every track.
#pragma once
#include <cstdint>
#include "MarkovEngine.hpp"  // for Rng, Trigger

namespace rw {

using fm::Rng;
using fm::Trigger;

static const int NT = 6;       // tracks
static const int MAXL = 16;    // longest phrase
static const int NHIST = 16;   // generations kept for REWIND
static const int NRULE = 8;    // pictures per rule card
static const int EVERY[4] = {1, 2, 4, 8};

// x = hit, . = rest. The order matches the panel cards.
static const char* const HIT_RULES[NRULE] = {"x", "x.", "xx", "x.x", "xxx", ".x", "x..", "."};
static const char* const REST_RULES[NRULE] = {".", "..", ".x", "x.", "...", ".x.", "..x", "x"};
static const int DEFAULT_HIT = 1, DEFAULT_REST = 2;

struct Track {
	uint8_t seed[MAXL] = {};     // 1 = hit
	uint8_t phrase[MAXL] = {};   // what plays now
	uint8_t hist[NHIST][MAXL] = {};  // earlier generations, for REWIND
	int histCount = 0, histPos = 0;  // histPos = where the next one is written
	int gen = 0;                 // generations since the seed
	int barCount = 0;            // bars since the seed, for EVERY
};

struct Engine {
	// settings (set by the module)
	int length = 16;             // 8 or 16
	int hitRule[NT], restRule[NT];
	float amount[NT];            // 0..1
	int every[NT];               // bars between rewrites: 1, 2, 4 or 8
	bool locked[NT] = {};
	uint8_t active = 0x3F;       // muted or missing tracks: silent and frozen
	bool hold = false;           // no growth while true
	bool rewriteGate = false;    // REWRITE NOW gate: grow on every bar line while high
	bool squeeze = false;        // FIT: false = Window, true = Squeeze
	float window = 0.f;          // 0..1, which slice Window keeps
	float trigLen = 0.005f;

	// state
	Track tr[NT];
	bool seeded = false;
	uint8_t capture[MAXL] = {};  // last bar of incoming triggers, one bit per track
	uint8_t curIn = 0;           // triggers that came in during this step
	int pos = 0;                 // the step playing now
	bool started = false, resetPending = false;
	bool rewritePending = false; // REWRITE NOW pressed: grow on the next bar line
	uint8_t lastFired = 0;
	Trigger out[NT], genTrig;
	Rng rng;

	Engine() {
		for (int t = 0; t < NT; t++) {
			hitRule[t] = DEFAULT_HIT;
			restRule[t] = DEFAULT_REST;
			amount[t] = 0.5f;
			every[t] = 2;
		}
	}

	int len() const { return length <= 8 ? 8 : MAXL; }

	void reset() { resetPending = true; }

	// A trigger came in on track t.
	void input(int t) {
		curIn |= 1 << t;
		if (!seeded && (active & (1 << t))) {
			out[t].fire(trigLen);
			lastFired |= 1 << t;
		}
	}

	// SEED: the last bar of incoming triggers becomes every track's phrase.
	void seedNow() {
		uint8_t cap[MAXL];
		for (int i = 0; i < MAXL; i++) cap[i] = capture[i];
		if (started) cap[pos] = curIn;  // the step playing now counts as it stands
		for (int t = 0; t < NT; t++) {
			Track& k = tr[t];
			for (int i = 0; i < MAXL; i++) k.seed[i] = k.phrase[i] = (cap[i] >> t) & 1;
			k.histCount = k.histPos = 0;
			k.gen = 0;
			k.barCount = 0;
		}
		seeded = true;
	}

	void unseed() { seeded = false; }

	// One generation for track t.
	void grow(int t) {
		Track& k = tr[t];
		int L = len();
		// remember this generation for REWIND
		for (int i = 0; i < MAXL; i++) k.hist[k.histPos][i] = k.phrase[i];
		k.histPos = (k.histPos + 1) % NHIST;
		if (k.histCount < NHIST) k.histCount++;
		// swap every step for its rule (with chance AMOUNT)
		uint8_t r[MAXL * 3];
		int m = 0;
		int h = hitRule[t] < 0 || hitRule[t] >= NRULE ? 0 : hitRule[t];
		int s = restRule[t] < 0 || restRule[t] >= NRULE ? 0 : restRule[t];
		for (int i = 0; i < L; i++) {
			bool hit = k.phrase[i] != 0;
			if (amount[t] > 0.f && rng.uniform() < amount[t]) {
				for (const char* c = hit ? HIT_RULES[h] : REST_RULES[s]; *c; c++) r[m++] = *c == 'x';
			}
			else r[m++] = hit;
		}
		// fold back to one bar (every rule is at least one step, so m >= L)
		if (squeeze) {
			for (int i = 0; i < L; i++) k.phrase[i] = r[(i * m) / L];
		}
		else {
			float w = window < 0.f ? 0.f : (window > 1.f ? 1.f : window);
			int off = (int) (w * (m - L) + 0.5f);
			for (int i = 0; i < L; i++) k.phrase[i] = r[(off + i) % m];
		}
		k.gen++;
	}

	// REWIND: back one generation, or all the way to the seed. Locked tracks stay.
	void rewind(bool toSeed) {
		if (!seeded) return;
		for (int t = 0; t < NT; t++) {
			if (locked[t]) continue;
			Track& k = tr[t];
			if (toSeed) {
				for (int i = 0; i < MAXL; i++) k.phrase[i] = k.seed[i];
				k.histCount = 0;
				k.gen = 0;
			}
			else if (k.histCount > 0) {
				k.histPos = (k.histPos - 1 + NHIST) % NHIST;
				for (int i = 0; i < MAXL; i++) k.phrase[i] = k.hist[k.histPos][i];
				k.histCount--;
				if (k.gen > 0) k.gen--;
			}
		}
	}

	// The bar line: grow the tracks whose time has come.
	void barLine() {
		bool now = rewritePending || rewriteGate;
		rewritePending = false;
		bool any = false;
		for (int t = 0; t < NT; t++) {
			Track& k = tr[t];
			k.barCount++;
			if (hold || locked[t] || !(active & (1 << t))) continue;
			int e = every[t] < 1 ? 1 : every[t];
			if (now || k.barCount % e == 0) {
				grow(t);
				any = true;
			}
		}
		if (any) genTrig.fire(trigLen);
	}

	void clock() {
		int L = len();
		if (started && pos < MAXL) capture[pos] = curIn;
		curIn = 0;
		int next;
		bool bar = false;
		if (resetPending || !started) next = 0;
		else {
			next = (pos + 1) % L;
			bar = next == 0;
		}
		resetPending = false;
		started = true;
		pos = next;
		if (bar && seeded) barLine();
		lastFired = 0;
		if (seeded) {
			for (int t = 0; t < NT; t++)
				if ((active & (1 << t)) && tr[t].phrase[pos]) {
					out[t].fire(trigLen);
					lastFired |= 1 << t;
				}
		}
	}

	// clock first, then the inputs, in the same sample
	void process(float dt, bool clockEdge, bool resetEdge, const bool inEdge[NT]) {
		if (resetEdge) reset();
		if (clockEdge) clock();
		for (int t = 0; t < NT; t++)
			if (inEdge[t]) input(t);
		for (int t = 0; t < NT; t++) out[t].process(dt);
		genTrig.process(dt);
	}

	// How far a track has moved from its seed: share of steps that differ, 0..1.
	float distance(int t) const {
		int L = len(), d = 0;
		for (int i = 0; i < L; i++) d += tr[t].phrase[i] != tr[t].seed[i];
		return (float) d / (float) L;
	}

	int maxGen() const {
		int g = 0;
		for (int t = 0; t < NT; t++) if (tr[t].gen > g) g = tr[t].gen;
		return g;
	}

	// The phrase k generations back (0 = now), for the display. False if there is none.
	bool generation(int t, int back, uint8_t outRow[MAXL]) const {
		const Track& k = tr[t];
		if (back == 0) {
			for (int i = 0; i < MAXL; i++) outRow[i] = k.phrase[i];
			return true;
		}
		if (back > k.histCount) return false;
		int p = (k.histPos - back + 2 * NHIST) % NHIST;
		for (int i = 0; i < MAXL; i++) outRow[i] = k.hist[p][i];
		return true;
	}
};

} // namespace rw
