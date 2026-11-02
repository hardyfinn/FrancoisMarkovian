// Offline checks for the Francois Markovian engine.
// Build and run:  c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
#include "../src/MarkovEngine.hpp"
#include <cstdio>
#include <cassert>
#include <cmath>
using namespace fm;

static const float DT = 1.f / 48000.f;

static void tick(Engine& e) {
	e.process(DT, true, false);
	for (int i = 0; i < 2400; i++) e.process(DT, false, false);  // 50 ms between ticks
}

static void printRun(const char* name, Engine& e, int n) {
	printf("%-30s", name);
	for (int i = 0; i < n; i++) { tick(e); printf("%d", e.step + 1); }
	printf("\n");
}

int main() {
	// 1. Straight matrix plays 1..8 in order, starting on step 1
	{
		Engine e;
		for (int i = 0; i < 16; i++) { tick(e); assert(e.step == i % NS); }
		Engine p; printRun("straight", p, 16);
	}
	// 2. Hits: the default A pattern fires the right lanes, and triggers are 5 ms
	{
		Engine e;
		e.process(DT, true, false);
		assert(e.step == 0 && e.lastMask == 0x1);  // bass drum on step 1
		assert(e.trig[0].high && !e.trig[1].high);
		for (int i = 0; i < 230; i++) e.process(DT, false, false);  // ~4.8 ms
		assert(e.trig[0].high);
		for (int i = 0; i < 20; i++) e.process(DT, false, false);   // past 5 ms
		assert(!e.trig[0].high);
	}
	// 3. Jump statistics: from step 4 (index 3), 70% to 5 and 30% back to 1
	{
		Engine e;
		e.snap[0].w[3][4] = 0.7f;
		e.snap[0].w[3][0] = 0.3f;
		int toFive = 0, toOne = 0, n = 0;
		for (int i = 0; i < 80000; i++) {
			int from = e.step;
			tick(e);
			if (i > 0 && from == 3) { n++; if (e.step == 4) toFive++; else if (e.step == 0) toOne++; else assert(false); }
		}
		printf("from 4: to 5 %.3f, to 1 %.3f (want 0.700 / 0.300, n=%d)\n", (float) toFive / n, (float) toOne / n, n);
		assert(std::fabs((float) toFive / n - 0.7f) < 0.02f);
		Engine p; p.snap[0].w[3][4] = 0.7f; p.snap[0].w[3][0] = 0.3f;
		printRun("step 4 jumps back sometimes", p, 32);
	}
	// 4. Repeats: the diagonal gives stutters; an empty row goes to the next step
	{
		Engine e;
		e.snap[0].w[1][1] = 1.f;  // step 2: 50% repeat, 50% next
		for (int r = 0; r < NS; r++) e.snap[0].w[5][r] = 0.f;  // step 6: empty row
		int repeats = 0;
		for (int i = 0; i < 20000; i++) {
			int from = e.step;
			tick(e);
			if (i > 0 && from == 1 && e.step == 1) repeats++;
			if (i > 0 && from == 5) assert(e.step == 6);
		}
		assert(repeats > 1000);
		Engine p; p.snap[0].w[1][1] = 1.f; printRun("step 2 stutters", p, 32);
	}
	// 5. Evolve: 0 plays A exactly, 1 plays B exactly, between blends
	{
		Engine e;
		e.snap[1].w[0][0] = 1.f; e.snap[1].w[0][1] = 0.f;  // B: step 1 always repeats
		e.evolve = 0.f;
		assert(std::fabs(e.chance(0, 1) - 1.f) < 1e-6f);
		e.evolve = 1.f;
		assert(std::fabs(e.chance(0, 0) - 1.f) < 1e-6f);
		e.evolve = 0.25f;
		assert(std::fabs(e.chance(0, 0) - 0.25f) < 1e-6f);
		// hits at 0 and 1 are exactly A and B
		Engine h;
		h.evolve = 0.f;
		for (int i = 0; i < 64; i++) {
			tick(h);
			uint8_t want = 0;
			for (int l = 0; l < NL; l++) if (h.snap[0].hit[l][h.step]) want |= 1 << l;
			assert(h.lastMask == want);
		}
		h.evolve = 1.f;
		for (int i = 0; i < 64; i++) {
			tick(h);
			uint8_t want = 0;
			for (int l = 0; l < NL; l++) if (h.snap[1].hit[l][h.step]) want |= 1 << l;
			assert(h.lastMask == want);
		}
		// at 0.3, a hit that is only in B plays about 30% of the time
		Engine d;
		d.snap[0].clearHits(); d.snap[1].clearHits();
		d.snap[1].hit[2][0] = true;  // hat on step 1 only in B
		d.evolve = 0.3f;
		int plays = 0, visits = 0;
		for (int i = 0; i < 80000; i++) { tick(d); if (d.step == 0) { visits++; if (d.lastMask & 4) plays++; } }
		printf("B-only hit at evolve 0.3: %.3f (want 0.300)\n", (float) plays / visits);
		assert(std::fabs((float) plays / visits - 0.3f) < 0.02f);
		// a hit in both always plays, a hit in neither never does
		Engine f; f.evolve = 0.5f;
		f.snap[0].clearHits(); f.snap[1].clearHits();
		f.snap[0].hit[0][0] = f.snap[1].hit[0][0] = true;
		for (int i = 0; i < 800; i++) { tick(f); assert(((f.lastMask & 1) != 0) == (f.step == 0)); }
	}
	// 6. Freeze loops the last 8 played ticks exactly, then lets go
	{
		Engine e;
		e.snap[0].randomise(e.rng, true);
		e.evolve = 0.f;
		e.snap[1].randomise(e.rng, true);
		e.evolve = 0.5f;
		Tick last[NS];
		for (int i = 0; i < 37; i++) { tick(e); last[i % NS].step = (uint8_t) e.step; last[i % NS].mask = e.lastMask; }
		// order of the last 8, oldest first
		Tick want[NS];
		for (int i = 0; i < NS; i++) want[i] = last[(37 + i) % NS];
		e.freeze = true;
		for (int rep = 0; rep < 5; rep++)
			for (int i = 0; i < NS; i++) {
				tick(e);
				assert(e.step == want[i].step && e.lastMask == want[i].mask);
			}
		// reset while frozen restarts the loop
		for (int i = 0; i < 3; i++) tick(e);
		e.process(DT, false, true);
		tick(e);
		assert(e.step == want[0].step);
		// unfreeze: carries on from the current step using the matrix
		e.freeze = false;
		tick(e);
		assert(e.chance(want[0].step, e.step) > 0.f);
		printf("freeze loop: ");
		for (int i = 0; i < NS; i++) printf("%d", want[i].step + 1);
		printf(" (replayed 5x exactly)\n");
	}
	// 7. Freeze straight after load (no history) just keeps playing
	{
		Engine e;
		e.freeze = true;
		for (int i = 0; i < 10; i++) { tick(e); assert(e.step == i % NS || e.loopLen > 0); }
	}
	// 8. Reset: the next tick is step 1
	{
		Engine e;
		for (int i = 0; i < 5; i++) tick(e);
		e.process(DT, false, true);
		assert(e.step == 4);  // nothing moves until the clock
		tick(e);
		assert(e.step == 0);
		tick(e);
		assert(e.step == 1);
	}
	// 9. Gentle randomise keeps "next step" strongest in every row
	{
		Engine e;
		for (int k = 0; k < 200; k++) {
			e.snap[0].randomise(e.rng, false);
			for (int r = 0; r < NS; r++)
				for (int c = 0; c < NS; c++)
					assert(e.snap[0].w[r][c] <= e.snap[0].w[r][(r + 1) % NS]);
		}
		Engine p; p.snap[0].randomise(p.rng, false); printRun("gentle randomise", p, 32);
		Engine q; q.snap[0].randomise(q.rng, true); printRun("wild randomise", q, 32);
	}
	printf("all engine tests passed\n");
	return 0;
}
