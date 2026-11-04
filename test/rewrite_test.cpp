// Offline checks for the Rewrite engine.
// Build and run:  c++ -std=c++11 -O2 -o test/rewrite_test test/rewrite_test.cpp && test/rewrite_test
#include "../src/RewriteEngine.hpp"
#include <cstdio>
#include <cassert>
#include <cstring>
using namespace rw;

static const float DT = 1.f / 48000.f;
static const bool NONE[NT] = {};

// one clock, with the given tracks triggering just after it (like a sequencer upstream)
static void step(Engine& e, uint8_t in = 0) {
	e.process(DT, true, false, NONE);
	bool edge[NT];
	for (int t = 0; t < NT; t++) edge[t] = (in >> t) & 1;
	e.process(DT, false, false, edge);
	for (int i = 0; i < 2400; i++) e.process(DT, false, false, NONE);
}

static void setPhrase(Engine& e, int t, const char* s) {
	for (int i = 0; i < MAXL; i++) e.tr[t].phrase[i] = e.tr[t].seed[i] = s[i] == 'x';
}

static bool phraseIs(const Engine& e, int t, const char* s) {
	for (int i = 0; i < e.len(); i++) if ((e.tr[t].phrase[i] != 0) != (s[i] == 'x')) return false;
	return true;
}

static void print(const char* name, const Engine& e, int t) {
	printf("%-28s", name);
	for (int i = 0; i < e.len(); i++) putchar(e.tr[t].phrase[i] ? 'x' : '.');
	printf("\n");
}

// a seeded engine where every track grows on every bar with AMOUNT 100%
static Engine sure() {
	Engine e;
	e.seeded = true;
	for (int t = 0; t < NT; t++) { e.amount[t] = 1.f; e.every[t] = 1; }
	return e;
}

int main() {
	// 1. Not seeded: triggers pass straight through, and the last bar is captured
	{
		Engine e;
		const uint8_t pat[16] = {1, 0, 2, 0, 1, 0, 0, 4, 1, 0, 2, 0, 1, 0, 0, 0};
		for (int bar = 0; bar < 2; bar++)
			for (int i = 0; i < 16; i++) {
				e.process(DT, true, false, NONE);
				bool edge[NT];
				for (int t = 0; t < NT; t++) edge[t] = (pat[i] >> t) & 1;
				e.process(DT, false, false, edge);
				for (int t = 0; t < NT; t++) assert(e.out[t].high == edge[t]);
				for (int k = 0; k < 2400; k++) e.process(DT, false, false, NONE);
			}
		// seed lands mid-bar on step 0 of the third bar: that step counts as it stands
		e.process(DT, true, false, NONE);
		bool edge[NT] = {true};
		e.process(DT, false, false, edge);
		e.seedNow();
		for (int i = 0; i < 16; i++)
			for (int t = 0; t < NT; t++) assert(e.tr[t].seed[i] == ((pat[i] >> t) & 1));
		assert(e.seeded);
	}
	// 2. Seeded: the phrase plays on the clock, inputs are ignored
	{
		Engine e = sure();
		for (int t = 0; t < NT; t++) e.every[t] = 8;  // no growth in the first bar
		setPhrase(e, 0, "x...x...x...x...");
		for (int i = 0; i < 16; i++) {
			e.process(DT, true, false, NONE);
			assert(e.out[0].high == (i % 4 == 0));
			bool edge[NT] = {false, true};
			e.process(DT, false, false, edge);
			assert(!e.out[1].high);
			for (int k = 0; k < 2400; k++) e.process(DT, false, false, NONE);
		}
	}
	// 3. Identity rules never change anything
	{
		Engine e = sure();
		e.hitRule[0] = 0; e.restRule[0] = 0;
		setPhrase(e, 0, "x..x..x.x...x.x.");
		for (int i = 0; i < 16 * 10 + 1; i++) step(e);
		assert(phraseIs(e, 0, "x..x..x.x...x.x."));
		assert(e.tr[0].gen == 10);
	}
	// 4. One generation of the default (Thue-Morse) rules, Window at the start
	{
		Engine e = sure();
		setPhrase(e, 0, "x...............");
		for (int i = 0; i < 17; i++) step(e);  // 16 clocks + the one that crosses the bar line
		// x -> x. and . -> .x : x. .x .x .x .x .x .x .x  (first 16 of 32)
		assert(phraseIs(e, 0, "x..x.x.x.x.x.x.x"));
		print("thue-morse gen 1", e, 0);
		for (int i = 0; i < 16 * 3; i++) step(e);
		print("thue-morse gen 4", e, 0);
	}
	// 5. Squeeze folds the whole grown phrase back: with Thue-Morse at 100%, every
	//    pair starts with the old step, so the phrase stays the same
	{
		Engine e = sure();
		e.squeeze = true;
		setPhrase(e, 0, "x...x.x...x..x..");
		for (int i = 0; i < 16 * 4 + 1; i++) step(e);
		assert(phraseIs(e, 0, "x...x.x...x..x.."));
		// a roll rule grows it unevenly and squeeze keeps the whole shape
		Engine r = sure();
		r.squeeze = true;
		r.hitRule[0] = 4;  // xxx
		setPhrase(r, 0, "x...x...x...x...");
		for (int i = 0; i < 17; i++) step(r);
		// 4 hits x 3 + 12 rests = 24 steps, sampled every 1.5 steps
		print("squeeze, hit -> roll", r, 0);
		assert(r.tr[0].phrase[0] && r.tr[0].phrase[1]);
	}
	// 6. Window picks the slice: at the end it keeps the last 16 steps
	{
		Engine e = sure();
		e.window = 1.f;
		e.hitRule[0] = 1;   // x.
		e.restRule[0] = 0;  // .
		setPhrase(e, 0, "x...............");
		for (int i = 0; i < 17; i++) step(e);
		// grown = x. + 15 rests = 17 steps; the last 16 are all rests
		assert(phraseIs(e, 0, "................"));
	}
	// 7. EVERY: track A grows every bar, B every 4 bars
	{
		Engine e = sure();
		e.every[1] = 4;
		setPhrase(e, 0, "x...x...x...x...");
		setPhrase(e, 1, "x...x...x...x...");
		step(e);  // first clock after start is step 1, no bar line
		for (int bar = 1; bar <= 8; bar++) {
			for (int i = 0; i < 16; i++) step(e);
			assert(e.tr[0].gen == bar);
			assert(e.tr[1].gen == bar / 4);
		}
	}
	// 8. LOCK, HOLD and mutes stop growth; REWRITE NOW forces it on the next bar
	{
		Engine e = sure();
		for (int t = 0; t < NT; t++) e.every[t] = 8;
		e.locked[0] = true;
		e.active = 0x3F & ~0x2;  // B muted
		step(e);
		for (int i = 0; i < 16; i++) step(e);
		assert(e.tr[0].gen == 0 && e.tr[1].gen == 0 && e.tr[2].gen == 0);
		e.rewritePending = true;
		for (int i = 0; i < 16; i++) step(e);
		assert(e.tr[0].gen == 0 && e.tr[1].gen == 0 && e.tr[2].gen == 1);
		assert(!e.rewritePending);
		e.rewriteGate = true;
		e.hold = true;
		for (int i = 0; i < 16; i++) step(e);
		assert(e.tr[2].gen == 1);
		e.hold = false;
		for (int i = 0; i < 16; i++) step(e);
		assert(e.tr[2].gen == 2);
		// a muted track plays nothing
		setPhrase(e, 1, "xxxxxxxxxxxxxxxx");
		e.process(DT, true, false, NONE);
		assert(!e.out[1].high);
	}
	// 9. REWIND: back one generation at a time, or straight to the seed
	{
		Engine e = sure();
		setPhrase(e, 0, "x.......x.......");
		uint8_t gens[5][MAXL];
		step(e);
		memcpy(gens[0], e.tr[0].phrase, MAXL);
		for (int g = 1; g < 5; g++) {
			for (int i = 0; i < 16; i++) step(e);
			memcpy(gens[g], e.tr[0].phrase, MAXL);
		}
		assert(e.tr[0].gen == 4);
		e.rewind(false);
		assert(memcmp(e.tr[0].phrase, gens[3], MAXL) == 0 && e.tr[0].gen == 3);
		e.rewind(false);
		assert(memcmp(e.tr[0].phrase, gens[2], MAXL) == 0);
		uint8_t row[MAXL];
		assert(e.generation(0, 1, row) && memcmp(row, gens[1], MAXL) == 0);
		e.rewind(true);
		assert(phraseIs(e, 0, "x.......x.......") && e.tr[0].gen == 0);
		assert(!e.generation(0, 1, row));
		assert(e.distance(0) == 0.f);
	}
	// 10. AMOUNT 0 keeps the phrase, 50% lands in between; length 8 works
	{
		Engine e = sure();
		e.amount[0] = 0.f;
		setPhrase(e, 0, "x...x...x...x...");
		for (int i = 0; i < 16 * 5 + 1; i++) step(e);
		assert(phraseIs(e, 0, "x...x...x...x..."));
		Engine h = sure();
		h.length = 8;
		h.amount[0] = 0.5f;
		setPhrase(h, 0, "x...x...x...x...");
		for (int i = 0; i < 8 * 6 + 1; i++) step(h);
		assert(h.tr[0].gen == 6);
		print("length 8, amount 50%, gen 6", h, 0);
	}
	// 11. Reset: the next clock plays step 1, the phrase is kept
	{
		Engine e = sure();
		for (int t = 0; t < NT; t++) e.every[t] = 8;
		setPhrase(e, 0, "x...............");
		for (int i = 0; i < 5; i++) step(e);
		e.process(DT, false, true, NONE);
		e.process(DT, true, false, NONE);
		assert(e.pos == 0 && e.out[0].high);
	}
	printf("all rewrite tests passed\n");
	return 0;
}
