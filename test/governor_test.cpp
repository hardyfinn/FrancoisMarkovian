// Offline checks for the Governor engine, including closed loops with a toy
// "band" that responds to the control signals.
// Build and run:  c++ -std=c++11 -O2 -o test/governor_test test/governor_test.cpp && test/governor_test
#include "../src/GovernorEngine.hpp"
#include "../src/MarkovEngine.hpp"  // Rng
#include <cstdio>
#include <cassert>
#include <cmath>
using namespace gov;

static fm::Rng rng;

static void play(Engine& e, uint8_t mask) {
	e.clock();
	for (int t = 0; t < NT; t++) if (mask & (1 << t)) e.trigger(t);
}

int main() {
	// 1. Density: 2 of 4 tracks every step = 50%; window and track count respected
	{
		Engine e; e.trackMask = 0x0F; e.window = 16;
		for (int i = 0; i < 40; i++) play(e, 0x3);
		play(e, 0x3);  // closes the last step
		assert(std::fabs(e.dens.measured - 0.5f) < 1e-6f);
		e.trackMask = 0x05;  // only A and C count: A always, C never = 50%
		play(e, 0x3);
		assert(std::fabs(e.dens.measured - 0.5f) < 1e-6f);
		Engine f; f.window = 8;
		for (int i = 0; i < 20; i++) play(f, 0x3F);
		play(f, 0x3F);
		assert(std::fabs(f.dens.measured - 1.f) < 1e-6f);
	}
	// 2. Variety: a repeating 16-step pattern = 0; a pattern that changes completely = 1
	{
		Engine e; e.trackMask = 0x0F; e.window = 16;
		const uint8_t pat[16] = {1, 0, 4, 0, 3, 0, 4, 8, 1, 0, 4, 0, 3, 2, 4, 0};
		for (int i = 0; i < 80; i++) play(e, pat[i % 16]);
		assert(e.vari.measured == 0.f);
		Engine f; f.trackMask = 0x0F; f.window = 8;
		for (int i = 0; i < 80; i++) play(f, (i / 8) % 2 ? 0x1 : 0x2);  // alternate whole windows
		assert(std::fabs(f.vari.measured - 1.f) < 1e-6f);
	}
	// 3. Closed loop on density: a band where each track fires with chance = control.
	//    The measurement should settle on the target.
	const float targets[3] = {0.2f, 0.5f, 0.8f};
	for (float tgt : targets) {
		Engine e; e.window = 16; e.response = 0.4f; e.wobble = 0.f;
		e.dens.target = tgt;
		float sum = 0.f; int n = 0;
		for (int i = 0; i < 4000; i++) {
			uint8_t m = 0;
			for (int t = 0; t < NT; t++) if (rng.uniform() < e.dens.control) m |= 1 << t;
			play(e, m);
			if (i > 1000) { sum += e.dens.measured; n++; }
		}
		printf("density target %.2f -> settles at %.3f (control %.2f)\n", tgt, sum / n, e.dens.control);
		assert(std::fabs(sum / n - tgt) < 0.03f);
	}
	// 4. Inverted polarity: the band gets sparser as the control rises
	{
		Engine e; e.dens.invert = true; e.dens.target = 0.3f;
		float sum = 0.f; int n = 0;
		for (int i = 0; i < 4000; i++) {
			uint8_t m = 0;
			for (int t = 0; t < NT; t++) if (rng.uniform() < 1.f - e.dens.control) m |= 1 << t;
			play(e, m);
			if (i > 1000) { sum += e.dens.measured; n++; }
		}
		printf("inverted, target 0.30 -> %.3f\n", sum / n);
		assert(std::fabs(sum / n - 0.3f) < 0.03f);
	}
	// 5. Wobble: more wobble makes the control swing more around the same target
	{
		float spread[2];
		for (int k = 0; k < 2; k++) {
			Engine e; e.window = 16; e.response = 0.5f; e.wobble = k ? 1.f : 0.f; e.dens.target = 0.5f;
			float mn = 1.f, mx = 0.f;
			for (int i = 0; i < 4000; i++) {
				uint8_t m = 0;
				for (int t = 0; t < NT; t++) if (rng.uniform() < e.dens.control) m |= 1 << t;
				play(e, m);
				if (i > 1000) { mn = std::fmin(mn, e.dens.measured); mx = std::fmax(mx, e.dens.measured); }
			}
			spread[k] = mx - mn;
		}
		printf("density swing: wobble 0 %.2f, wobble 1 %.2f\n", spread[0], spread[1]);
		assert(spread[1] > spread[0] * 1.3f);
	}
	// 6. Closed loop on variety: each step repeats the one a window ago, or is
	//    re-rolled with chance = control. Variety should settle on the target.
	{
		Engine e; e.window = 16; e.response = 0.4f; e.vari.target = 0.4f;
		uint8_t past[16] = {};
		float sum = 0.f; int n = 0;
		for (int i = 0; i < 6000; i++) {
			uint8_t m = past[i % 16];
			if (rng.uniform() < e.vari.control) {
				m = 0;
				for (int t = 0; t < NT; t++) if (rng.uniform() < 0.4f) m |= 1 << t;
			}
			past[i % 16] = m;
			play(e, m);
			if (i > 2000) { sum += e.vari.measured; n++; }
		}
		printf("variety target 0.40 -> %.3f (control %.2f)\n", sum / n, e.vari.control);
		assert(std::fabs(sum / n - 0.4f) < 0.05f);
	}
	// 7. Over/under gates with a deadband
	{
		Loop l; l.target = 0.5f;
		l.measured = 0.7f; l.update(0.5f, 0.f); assert(l.over == 1);
		l.measured = 0.3f; l.update(0.5f, 0.f); assert(l.over == -1);
		l.measured = 0.53f; l.update(0.5f, 0.f); assert(l.over == -1);  // inside the band, holds until near target
		l.measured = 0.5f; l.update(0.5f, 0.f); assert(l.over == 0);
	}
	printf("all governor tests passed\n");
	return 0;
}
