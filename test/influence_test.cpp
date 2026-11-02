// Offline checks for the Interlock influence engine.
// Build and run:  c++ -std=c++11 -O2 -o test/influence_test test/influence_test.cpp && test/influence_test
#include "../src/InfluenceEngine.hpp"
#include <cstdio>
#include <cassert>
#include <cmath>
using namespace inf;

static const float DT = 1.f / 48000.f;

// One step: clock, then the given input pattern one sample later (like a cable
// from another module), then 50 ms of nothing. Returns the tracks that fired.
static uint8_t step(Engine& e, uint8_t inputs) {
	bool none[NT] = {}, in[NT];
	for (int y = 0; y < NT; y++) in[y] = inputs & (1 << y);
	uint8_t fired = 0;
	e.process(DT, true, false, none);
	e.process(DT, false, false, in);
	for (int i = 0; i < 2400; i++) {
		e.process(DT, false, false, none);
		for (int y = 0; y < NT; y++) if (e.out[y].high) fired |= 1 << y;
	}
	return fired;
}

static const char* bits(uint8_t m) {
	static char b[NT + 1];
	for (int y = 0; y < NT; y++) b[y] = (m & (1 << y)) ? 'A' + y : '.';
	b[NT] = 0;
	return b;
}

int main() {
	const uint8_t A = 1, B = 2, C = 4, D = 8, E = 16;
	// 1. Empty grid: everything passes straight through, nothing extra
	{
		Engine e;
		for (int i = 0; i < 64; i++) { uint8_t in = (uint8_t) (i * 37 % 64); assert(step(e, in) == in); }
	}
	// 2. A -> B at -100 blocks B on the step after A, but not on A's own step
	{
		Engine e; e.grid[0][1] = -1.f;
		assert(step(e, A | B) == (A | B));
		assert(step(e, B) == 0);
		assert(step(e, B) == B);
	}
	// 3. Ghost hits: B -> D at +60 gives D about 60% of the time on the next step
	{
		Engine e; e.grid[1][3] = 0.6f;
		int n = 0, hits = 0;
		for (int i = 0; i < 20000; i++) {
			step(e, B);
			uint8_t f = step(e, 0);
			n++; if (f & D) hits++;
			assert((f & ~D) == 0);
		}
		printf("ghost D after B: %.3f (want 0.600)\n", (float) hits / n);
		assert(std::fabs((float) hits / n - 0.6f) < 0.02f);
	}
	// 4. Ghost hits arrive ~2 ms after the clock; passed triggers have no delay
	{
		Engine e; e.grid[0][4] = 1.f;
		step(e, A);
		bool none[NT] = {}, in[NT] = {};
		in[0] = true;
		e.process(DT, true, false, none);
		e.process(DT, false, false, in);
		assert(e.out[0].high && !e.out[4].high);  // A passes at once
		for (int i = 0; i < 90; i++) e.process(DT, false, false, none);  // 1.9 ms
		assert(!e.out[4].high);
		for (int i = 0; i < 10; i++) e.process(DT, false, false, none);  // past 2 ms
		assert(e.out[4].high);
	}
	// 5. Self influence: C -> C at -40 makes a C roll 100%, 60%, ...
	{
		Engine e; e.grid[2][2] = -0.4f;
		int n = 0, hits = 0;
		for (int i = 0; i < 20000; i++) {
			step(e, 0); step(e, 0);  // clear memory
			assert(step(e, C) == C);
			uint8_t f = step(e, C);
			n++; if (f & C) hits++;
		}
		printf("C straight after C: %.3f (want 0.600)\n", (float) hits / n);
		assert(std::fabs((float) hits / n - 0.6f) < 0.02f);
	}
	// 6. Memory: with memory 2, A -> B at -100 lasts 3 steps and fades 100, 67, 33
	{
		Engine e; e.grid[0][1] = -1.f; e.memory = 2;
		assert(std::fabs(e.fade(1) - 1.f) < 1e-6f && std::fabs(e.fade(2) - 2.f / 3) < 1e-6f && std::fabs(e.fade(3) - 1.f / 3) < 1e-6f && e.fade(4) == 0.f);
		int hits[4] = {};
		const int N = 20000;
		for (int i = 0; i < N; i++) {
			for (int k = 0; k < 4; k++) step(e, 0);
			step(e, A);
			for (int k = 0; k < 4; k++) if (step(e, B) & B) hits[k]++;
		}
		// B fires reset nothing (B has no row); chances after A: 0, 1/3, 2/3, 1
		printf("memory 2, B after A: %.2f %.2f %.2f %.2f (want 0.00 0.33 0.67 1.00)\n",
			(float) hits[0] / N, (float) hits[1] / N, (float) hits[2] / N, (float) hits[3] / N);
		assert(hits[0] == 0 && hits[3] == N);
		assert(std::fabs((float) hits[1] / N - 1.f / 3) < 0.02f && std::fabs((float) hits[2] / N - 2.f / 3) < 0.02f);
		// memory 0: only the next step
		Engine z; z.grid[0][1] = -1.f;
		step(z, A);
		assert(step(z, B) == 0);
		assert(step(z, B) == B);
	}
	// 7. Influences add up: two tracks pushing E by +50 each give 100%
	{
		Engine e; e.grid[0][4] = 0.5f; e.grid[1][4] = 0.5f;
		for (int i = 0; i < 200; i++) { step(e, 0); step(e, A | B); assert(step(e, 0) & E); }
	}
	// 8. Amount: 0 passes straight through, 50% halves every influence
	{
		Engine e; e.grid[0][1] = -1.f; e.grid[0][3] = 1.f; e.amount = 0.f;
		for (int i = 0; i < 200; i++) { step(e, A); assert(step(e, B) == B); }
		e.amount = 0.5f;
		int nb = 0, nd = 0;
		for (int i = 0; i < 20000; i++) { step(e, 0); step(e, A); uint8_t f = step(e, B); if (f & B) nb++; if (f & D) nd++; }
		printf("amount 50%%: B %.3f, ghost D %.3f (want 0.500 / 0.500)\n", nb / 20000.f, nd / 20000.f);
		assert(std::fabs(nb / 20000.f - 0.5f) < 0.02f && std::fabs(nd / 20000.f - 0.5f) < 0.02f);
	}
	// 9. Reset clears the memory
	{
		Engine e; e.grid[0][1] = -1.f; e.memory = 4;
		step(e, A);
		bool none[NT] = {};
		e.process(DT, false, true, none);
		assert(step(e, B) == B);
	}
	// 10. The guide's example, printed
	{
		Engine e; e.example();
		const uint8_t in[8] = {A, C, C, 0, A | B, B | C, C, C};
		printf("example  in : ");
		for (int s = 0; s < 8; s++) printf("%s ", bits(in[s]));
		printf("\nexample out : ");
		for (int s = 0; s < 8; s++) printf("%s ", bits(step(e, in[s])));
		printf("\n");
	}
	printf("all influence tests passed\n");
	return 0;
}
