// GovernorEngine.hpp
// The core of Governor, with no VCV Rack dependencies so it can be tested
// offline (test/governor_test.cpp).
//
// Governor listens to up to 6 trigger tracks and measures, over the last
// WINDOW clocks:
//   density = share of possible hits that played (hits / (window x tracks))
//   variety = how different this window is from the window before it:
//             changed hits / hits in either window (0 = exact repeat, 1 = nothing in common)
//
// For each, a feedback controller moves a control signal (0..1) until the
// measurement sits at its target. RESPONSE sets how quickly it corrects, and
// WOBBLE how hard it pushes: low settles smoothly, high overshoots and hunts.
#pragma once
#include <cstdint>
#include <cmath>

namespace gov {

static const int NT = 6;         // trigger inputs
static const int MAX_WIN = 64;   // largest window
static const int HIST = 2 * MAX_WIN;

inline float clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// One feedback loop: target vs measurement -> control.
struct Loop {
	float target = 0.5f;
	float measured = 0.f;
	float control = 0.5f;   // 0..1, what goes out
	float integral = 0.5f;  // the slow part of the control
	float error = 0.f;      // target - measured
	bool invert = false;    // raising the control lowers the measurement
	int over = 0;           // +1 above target (outside the deadband), -1 below, 0 inside
	float deadband = 0.05f;

	// One step of the controller. response 0..1 (fast..slow), wobble 0..1.
	void update(float response, float wobble) {
		error = target - measured;
		float e = invert ? -error : error;
		// time constant from 4 to 256 clocks
		float T = 4.f * std::pow(64.f, clamp01(response));
		float ki = (1.f + 7.f * wobble) / T;   // more wobble = harder integral push
		float kp = 0.6f * (1.f - wobble);      // and less damping
		integral = clamp01(integral + ki * e);
		control = clamp01(integral + kp * e);
		if (error < -deadband) over = 1;
		else if (error > deadband) over = -1;
		else if (std::fabs(error) < deadband * 0.5f) over = 0;  // hysteresis
	}

	void reset() { integral = control = 0.5f; error = 0.f; over = 0; }
};

struct Engine {
	int window = 16;     // 8, 16, 32 or 64 clocks
	uint8_t trackMask = 0x3F;  // which inputs count (the connected ones)
	bool allMuted = false;     // every listened track is muted: hold the controls where they are
	float response = 0.5f, wobble = 0.f;
	Loop dens, vari;

	uint8_t hist[HIST] = {};  // hist[(pos - 1)] = the last finished step
	int pos = 0, filled = 0;
	uint8_t cur = 0;          // tracks that fired since the last clock
	bool started = false;
	uint32_t clocks = 0;      // for the display

	void reset() {
		for (int i = 0; i < HIST; i++) hist[i] = 0;
		pos = filled = 0;
		cur = 0;
		started = false;
	}

	static int bits(uint8_t m) { int n = 0; while (m) { m &= m - 1; n++; } return n; }

	uint8_t stepAgo(int k) const { return hist[(pos - 1 - k + 2 * HIST) % HIST]; }  // k = 0: last finished step

	void measure() {
		int w = window < 1 ? 1 : (window > MAX_WIN ? MAX_WIN : window);
		int n = filled < w ? filled : w;
		uint8_t mask = trackMask ? trackMask : 0x3F;
		int tracks = bits(mask);
		// density over the last n steps
		int hits = 0;
		for (int k = 0; k < n; k++) hits += bits(stepAgo(k) & mask);
		dens.measured = n > 0 ? (float) hits / (float) (w * tracks) : 0.f;
		// variety: this window against the one before, step by step
		if (filled >= 2 * w) {
			int changed = 0, either = 0;
			for (int k = 0; k < w; k++) {
				uint8_t a = stepAgo(k) & mask, b = stepAgo(k + w) & mask;
				changed += bits(a ^ b);
				either += bits(a | b);
			}
			vari.measured = either > 0 ? (float) changed / (float) either : 0.f;
		}
	}

	void trigger(int track) { cur |= 1 << track; }

	// A clock ends the current step and starts a new one.
	void clock() {
		if (started) {
			hist[pos] = cur;
			pos = (pos + 1) % HIST;
			if (filled < HIST) filled++;
			if (!allMuted) {
				measure();
				dens.update(response, wobble);
				if (filled >= 2 * window) vari.update(response, wobble);  // needs two full windows
			}
		}
		started = true;
		cur = 0;
		clocks++;
	}
};

} // namespace gov
