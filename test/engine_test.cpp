// Offline checks for the Francois Markovian engine.
// Build and run:  c++ -std=c++11 -O2 -o test/engine_test test/engine_test.cpp && test/engine_test
#include "../src/MarkovEngine.hpp"
#include <cstdio>
#include <cassert>
#include <cmath>
#include <algorithm>
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

static float rowSum(const Snapshot& s, int r) {
	float t = 0.f;
	for (int c = 0; c < NS; c++) t += s.w[r][c];
	return t;
}

// set every trigger chance of a snapshot to 0 or 1 from strings
static void setPattern(Snapshot& s, const char* const rows[NL]) {
	for (int l = 0; l < NL; l++)
		for (int i = 0; i < NS; i++) s.p[l][i] = rows[l][i] == 'x' ? 1.f : 0.f;
}

int main() {
	// 1. Straight matrix plays 1..8 in order, starting on step 1
	{
		Engine e;
		for (int i = 0; i < 16; i++) { tick(e); assert(e.step == i % NS); }
		Engine p; printRun("straight", p, 16);
	}
	// 2. Default pattern: A on step 1, B on step 5, nothing else; triggers are 5 ms
	{
		Engine e;
		for (int i = 0; i < 64; i++) {
			e.process(DT, true, false);
			uint8_t want = e.step == 0 ? 0x1 : e.step == 4 ? 0x2 : 0;
			assert(e.lastMask == want);
			if (want) {
				int l = want == 1 ? 0 : 1;
				assert(e.trig[l].high);
				for (int k = 0; k < 230; k++) e.process(DT, false, false);  // ~4.8 ms
				assert(e.trig[l].high);
				for (int k = 0; k < 20; k++) e.process(DT, false, false);   // past 5 ms
				assert(!e.trig[l].high);
			}
			for (int k = 0; k < 2400; k++) e.process(DT, false, false);
		}
	}
	// 3. Editing a matrix cell rescales the row so it always adds up to 100%
	{
		Snapshot s; s.straight();
		float start[NS], out[NS];
		for (int c = 0; c < NS; c++) start[c] = s.w[3][c];
		// drag column 1 up to 30% in row 4: column 5 drops to 70%
		setRowChance(start, out, 0, 0.3f, 3);
		assert(std::fabs(out[0] - 0.3f) < 1e-6f && std::fabs(out[4] - 0.7f) < 1e-6f);
		for (int c = 0; c < NS; c++) s.w[3][c] = out[c];
		assert(std::fabs(rowSum(s, 3) - 1.f) < 1e-5f);
		// then column 7 up to 50%: the 30/70 keep their balance in the remaining 50%
		for (int c = 0; c < NS; c++) start[c] = s.w[3][c];
		setRowChance(start, out, 6, 0.5f, 3);
		assert(std::fabs(out[0] - 0.15f) < 1e-5f && std::fabs(out[4] - 0.35f) < 1e-5f && std::fabs(out[6] - 0.5f) < 1e-6f);
		// a drag that goes to 100% and comes back restores the balance (works from the start row)
		setRowChance(start, out, 6, 1.f, 3);
		assert(out[0] == 0.f && out[4] == 0.f);
		setRowChance(start, out, 6, 0.5f, 3);
		assert(std::fabs(out[0] - 0.15f) < 1e-5f);
		// lowering the only cell: the rest is spread over the other cells
		Snapshot t; t.straight();
		for (int c = 0; c < NS; c++) start[c] = t.w[2][c];
		setRowChance(start, out, 3, 0.65f, 2);
		float sum = 0.f;
		for (int c = 0; c < NS; c++) sum += out[c];
		assert(std::fabs(out[3] - 0.65f) < 1e-6f && std::fabs(sum - 1.f) < 1e-5f && out[0] > 0.f);
		// an empty row is edited as "next step 100%"
		for (int c = 0; c < NS; c++) start[c] = 0.f;
		setRowChance(start, out, 0, 0.2f, 2);
		assert(std::fabs(out[0] - 0.2f) < 1e-6f && std::fabs(out[3] - 0.8f) < 1e-6f);
	}
	// 4. Jump statistics: from step 4, 70% to 5 and 30% back to 1
	{
		Engine e;
		e.snap[0].w[3][4] = 0.7f;
		e.snap[0].w[3][0] = 0.3f;
		int toFive = 0, n = 0;
		for (int i = 0; i < 80000; i++) {
			int from = e.step;
			tick(e);
			if (i > 0 && from == 3) { n++; if (e.step == 4) toFive++; else assert(e.step == 0); }
		}
		printf("from 4: to 5 %.3f (want 0.700, n=%d)\n", (float) toFive / n, n);
		assert(std::fabs((float) toFive / n - 0.7f) < 0.02f);
		Engine p; p.snap[0].w[3][4] = 0.7f; p.snap[0].w[3][0] = 0.3f;
		printRun("step 4 jumps back sometimes", p, 32);
	}
	// 5. Repeats from the diagonal; an empty row goes to the next step
	{
		Engine e;
		e.snap[0].w[1][1] = 0.5f; e.snap[0].w[1][2] = 0.5f;
		for (int r = 0; r < NS; r++) e.snap[0].w[5][r] = 0.f;
		int repeats = 0;
		for (int i = 0; i < 20000; i++) {
			int from = e.step;
			tick(e);
			if (i > 0 && from == 1 && e.step == 1) repeats++;
			if (i > 0 && from == 5) assert(e.step == 6);
		}
		assert(repeats > 1000);
	}
	// 6. Trigger chances: a 25% cell fires about a quarter of the time
	{
		Engine e;
		e.snap[0].clearTriggers();
		e.snap[0].p[3][2] = 0.25f;  // track D, step 3
		int fires = 0, visits = 0;
		for (int i = 0; i < 80000; i++) { tick(e); if (e.step == 2) { visits++; if (e.lastMask & 8) fires++; } else assert(e.lastMask == 0); }
		printf("25%% trigger: %.3f\n", (float) fires / visits);
		assert(std::fabs((float) fires / visits - 0.25f) < 0.02f);
	}
	// 7. Evolve: blends matrix and trigger chances from X to Y
	{
		Engine e;
		e.snap[1].w[0][0] = 1.f; e.snap[1].w[0][1] = 0.f;  // Y: step 1 always repeats
		e.evolve = 0.f;  assert(std::fabs(e.chance(0, 1) - 1.f) < 1e-6f);
		e.evolve = 1.f;  assert(std::fabs(e.chance(0, 0) - 1.f) < 1e-6f);
		e.evolve = 0.25f; assert(std::fabs(e.chance(0, 0) - 0.25f) < 1e-6f);
		// 0% in X and 100% in Y gives 50% at halfway
		Engine d;
		d.snap[0].clearTriggers(); d.snap[1].clearTriggers();
		d.snap[1].p[5][0] = 1.f;  // F on step 1 only in Y
		d.evolve = 0.5f;
		assert(std::fabs(d.trigChance(5, 0) - 0.5f) < 1e-6f);
		int plays = 0, visits = 0;
		for (int i = 0; i < 80000; i++) { tick(d); if (d.step == 0) { visits++; if (d.lastMask & 32) plays++; } }
		printf("Y-only trigger at evolve 0.5: %.3f\n", (float) plays / visits);
		assert(std::fabs((float) plays / visits - 0.5f) < 0.02f);
		// at 0 and 1 a 0/100 pattern plays X or Y exactly
		const char* X[NL] = {"x...x...", "....x...", "..x...x.", "...x...x", "x.x.x.x.", "........"};
		const char* Y[NL] = {"x..x..x.", "....x..x", "x.xxx.xx", ".x...x..", "........", "xxxxxxxx"};
		Engine h;
		setPattern(h.snap[0], X); setPattern(h.snap[1], Y);
		for (int k = 0; k < 2; k++) {
			h.evolve = (float) k;
			const char* const* pat = k ? Y : X;
			for (int i = 0; i < 64; i++) {
				tick(h);
				uint8_t want = 0;
				for (int l = 0; l < NL; l++) if (pat[l][h.step] == 'x') want |= 1 << l;
				assert(h.lastMask == want);
			}
		}
	}
	// 8. Freeze loops the last 8 played ticks exactly, then lets go
	{
		Engine e;
		e.snap[0].randomise(e.rng, true);
		e.snap[1].randomise(e.rng, true);
		for (int k = 0; k < 2; k++) for (int l = 0; l < NL; l++) for (int s = 0; s < NS; s++) e.snap[k].p[l][s] = e.rng.uniform();
		e.evolve = 0.5f;
		Tick last[NS];
		for (int i = 0; i < 37; i++) { tick(e); last[i % NS].step = (uint8_t) e.step; last[i % NS].mask = e.lastMask; }
		Tick want[NS];
		for (int i = 0; i < NS; i++) want[i] = last[(37 + i) % NS];
		e.freeze = true;
		for (int rep = 0; rep < 5; rep++)
			for (int i = 0; i < NS; i++) {
				tick(e);
				assert(e.step == want[i].step && e.lastMask == want[i].mask);
			}
		for (int i = 0; i < 3; i++) tick(e);
		e.process(DT, false, true);
		tick(e);
		assert(e.step == want[0].step);
		e.freeze = false;
		tick(e);
		assert(e.chance(want[0].step, e.step) > 0.f);
		printf("freeze loop: ");
		for (int i = 0; i < NS; i++) printf("%d", want[i].step + 1);
		printf(" (replayed 5x exactly)\n");
	}
	// 9. Reset: the next tick is step 1
	{
		Engine e;
		for (int i = 0; i < 5; i++) tick(e);
		e.process(DT, false, true);
		assert(e.step == 4);
		tick(e); assert(e.step == 0);
		tick(e); assert(e.step == 1);
	}
	// 10. Randomise gives rows that add up to 100%; gentle keeps "next step" strongest
	{
		Engine e;
		for (int k = 0; k < 200; k++) {
			e.snap[0].randomise(e.rng, false);
			e.snap[1].randomise(e.rng, true);
			for (int r = 0; r < NS; r++) {
				assert(std::fabs(rowSum(e.snap[0], r) - 1.f) < 1e-5f);
				float s1 = rowSum(e.snap[1], r);
				assert(s1 == 0.f || std::fabs(s1 - 1.f) < 1e-5f);
				for (int c = 0; c < NS; c++) assert(e.snap[0].w[r][c] <= e.snap[0].w[r][(r + 1) % NS]);
			}
		}
		Engine p; p.snap[0].randomise(p.rng, false); printRun("gentle randomise", p, 32);
		Engine q; q.snap[0].randomise(q.rng, true); printRun("wild randomise", q, 32);
	}
	// 11. Clock divider: every div-th clock from reset, shifted by offset
	{
		const int divs[5] = {1, 2, 4, 8, 16}, offs[4] = {0, 1, 2, 4};
		for (int di = 0; di < 5; di++)
			for (int oi = 0; oi < 4; oi++) {
				Engine e;
				e.div = divs[di]; e.offset = offs[oi];
				e.snap[0].randomise(e.rng, true);  // the playhead jumping must not matter
				for (int i = 0; i < 70; i++) {
					if (i == 40) e.process(DT, false, true);  // reset restarts the count
					e.process(DT, true, false);
					int n = i < 40 ? i : i - 40;
					bool want = ((n - offs[oi]) % divs[di] + divs[di]) % divs[di] == 0;
					assert(e.divTrig.high == want);
					for (int k = 0; k < 2400; k++) e.process(DT, false, false);
					assert(!e.divTrig.high);
				}
			}
		Engine p; p.div = 4; p.offset = 2;
		printf("%-30s", "divide 4, offset 2");
		for (int i = 0; i < 16; i++) { p.process(DT, true, false); putchar(p.divTrig.high ? 'x' : '.'); for (int k = 0; k < 2400; k++) p.process(DT, false, false); }
		printf("\n");
	}
	// 12. Nudge: Gentle touches at most 3 cells, Wild at most 5, each by at most 50
	//     points (before its row rescales); rows still add up to 100%
	{
		for (int wild = 0; wild < 2; wild++) {
			int maxCells = wild ? 5 : 3, seenMax = 0;
			Engine e;
			for (int k = 0; k < 2000; k++) {
				Snapshot before = e.snap[0];
				e.snap[0].nudge(e.rng, maxCells);
				int rows = 0;
				for (int r = 0; r < NS; r++) {
					assert(std::fabs(rowSum(e.snap[0], r) - 1.f) < 1e-4f);
					bool changed = false;
					for (int c = 0; c < NS; c++) if (std::fabs(e.snap[0].w[r][c] - before.w[r][c]) > 1e-6f) changed = true;
					rows += changed;
				}
				assert(rows <= maxCells);
				if (rows > seenMax) seenMax = rows;
				if (k % 50 == 0) e.snap[0].straight();  // keep it close to straight, like real use
			}
			assert(seenMax >= 2);
		}
		// one cell from straight: it moves by at most 50 points
		for (int k = 0; k < 2000; k++) {
			Engine e1; e1.rng.seed(k + 1);
			e1.snap[0].nudge(e1.rng, 1);
			float biggest = 0.f;
			for (int r = 0; r < NS; r++)
				for (int c = 0; c < NS; c++) {
					float was = c == (r + 1) % NS ? 1.f : 0.f;
					biggest = std::max(biggest, std::fabs(e1.snap[0].w[r][c] - was));
				}
			assert(biggest <= 0.5f + 1e-4f);
		}
		Engine p; p.snap[0].nudge(p.rng, 3); printRun("gentle nudge", p, 32);
		Engine q; q.snap[0].nudge(q.rng, 5); printRun("wild nudge", q, 32);
	}
	// 13. Mutes silence a track's output but not its dice, jump size and phrase trigger
	{
		Engine e;
		e.muted = 0x1;  // A muted
		bool sawPhrase = false;
		for (int i = 0; i < 32; i++) {
			e.process(DT, true, false);
			if (e.step == 0) { assert(e.lastMask & 1); assert(!(e.lastOut & 1)); assert(!e.trig[0].high); }
			if (e.step == 4) { assert(e.lastOut & 2); assert(e.trig[1].high); }
			assert(e.lastJump == 0);  // straight: always the next step
			if (i % NS == 0) { assert(e.phraseTrig.high); sawPhrase = true; }
			else assert(!e.phraseTrig.high);
			for (int k = 0; k < 2400; k++) e.process(DT, false, false);
		}
		assert(sawPhrase);
		// a jump from step 1 to step 4 is +2, from 4 back to 1 is -4 -> folded to +4
		Engine j;
		for (int c = 0; c < NS; c++) { j.snap[0].w[0][c] = c == 3 ? 1.f : 0.f; j.snap[0].w[3][c] = c == 0 ? 1.f : 0.f; }
		tick(j); assert(j.step == 0);
		tick(j); assert(j.step == 3 && j.lastJump == 2);
		tick(j); assert(j.step == 0 && j.lastJump == 4);
	}
	printf("all engine tests passed\n");
	return 0;
}
