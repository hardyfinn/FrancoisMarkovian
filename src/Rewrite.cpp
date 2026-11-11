// Rewrite.cpp
// Panel, widgets and Rack glue for Rewrite, the per-track pattern grower. The
// logic is in RewriteEngine.hpp. Positions are in mm and must match
// res/make_panel_rewrite.py.
#include "plugin.hpp"
#include "RewriteEngine.hpp"

using rw::NT;
using rw::MAXL;

namespace rwp {
static const float ROW_Y0 = 19.f, ROW_H = 12.2f;                // track rows A..F
static const float HIT_X = 9.f, REST_X = 26.f, CARD_W = 15.f, CARD_H = 9.f;
static const float EVERY_X = 43.f, EVERY_W = 11.f;
static const float X_AMT = 62.f, X_LOCK = 74.5f;
static const float GEN_X = 80.f, GEN_W = 45.f, GEN_H = 11.f;     // generations display
static const float X_OUT = 133.f;
static const float Y_G1 = 104.5f, Y_G2 = 118.f;                  // global rows
static const float X_G1[10] = {8.f, 21.f, 35.f, 45.f, 62.f, 72.f, 91.f, 101.f, 120.f, 130.f};
static const float X_G2[9] = {8.f, 21.f, 32.f, 49.f, 61.f, 82.f, 95.f, 108.f, 121.f};
static const int GEN_ROWS = 4;
static const NVGcolor LANE_COL[NT] = {
	nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0xE8, 0xC5, 0x47), nvgRGB(0x4C, 0xD6, 0xE0),
	nvgRGB(0xB3, 0x8C, 0xF0), nvgRGB(0x7C, 0xD3, 0x6B), nvgRGB(0xF0, 0x6E, 0xA8)};
static const NVGcolor CELL_BG = nvgRGB(0x24, 0x24, 0x2B);
static const NVGcolor FRAME = nvgRGB(0x16, 0x16, 0x1B);
static const NVGcolor TEXT = nvgRGB(0xF4, 0xF0, 0xE8);
static const NVGcolor DIM = nvgRGB(0x6A, 0x66, 0x70);
} // namespace rwp

using namespace rwp;

static float rowY(int t) { return ROW_Y0 + t * ROW_H + ROW_H / 2; }

struct Rewrite : Module {
	enum ParamId {
		ENUMS(HIT_PARAM, NT),
		ENUMS(REST_PARAM, NT),
		ENUMS(EVERY_PARAM, NT),
		ENUMS(AMOUNT_PARAM, NT),
		ENUMS(LOCK_PARAM, NT),
		SEED_PARAM,
		NOW_PARAM,
		HOLD_PARAM,
		REWIND_PARAM,
		WINDOW_PARAM,
		FIT_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		SEED_INPUT,
		NOW_INPUT,
		HOLD_INPUT,
		REWIND_INPUT,
		WINDOW_INPUT,
		TRIG_INPUT,
		ACT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(TRACK_OUTPUT, NT),
		TRIG_POLY_OUTPUT,
		ACT_OUTPUT,
		GEN_OUTPUT,
		STATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		ENUMS(LOCK_LIGHT, NT),
		SEED_LIGHT,
		NOW_LIGHT,
		HOLD_LIGHT,
		REWIND_LIGHT,
		LIGHTS_LEN
	};

	rw::Engine engine;
	dsp::SchmittTrigger clockTrigger, resetTrigger, inTrigger[NT];
	dsp::SchmittTrigger seedTrigger, rewindTrigger, nowTrigger;
	dsp::BooleanTrigger seedButton, nowButton;
	dsp::ClockDivider settingsDivider, lightDivider;
	bool rewindWasHeld = false, rewindLongDone = false;
	float rewindHeld = 0.f, rewindFlash = 0.f;
	float flash[NT] = {};
	// requests from the menu, carried out on the audio thread
	bool reqUnseed = false, reqSeed = false, reqToSeed = false;

	Rewrite() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		std::vector<std::string> hitNames, restNames;
		for (int r = 0; r < rw::NRULE; r++) {
			hitNames.push_back(string::f("hit becomes %s", rw::HIT_RULES[r]));
			restNames.push_back(string::f("rest becomes %s", rw::REST_RULES[r]));
		}
		for (int t = 0; t < NT; t++) {
			char c = 'A' + t;
			configSwitch(HIT_PARAM + t, 0.f, rw::NRULE - 1, rw::DEFAULT_HIT, string::f("%c: hit becomes (x = hit, . = rest)", c), hitNames);
			configSwitch(REST_PARAM + t, 0.f, rw::NRULE - 1, rw::DEFAULT_REST, string::f("%c: rest becomes (x = hit, . = rest)", c), restNames);
			configSwitch(EVERY_PARAM + t, 0.f, 3.f, 1.f, string::f("%c: rewrite every", c), {"1 bar", "2 bars", "4 bars", "8 bars"});
			configParam(AMOUNT_PARAM + t, 0.f, 1.f, 0.5f, string::f("%c: amount (chance each step is rewritten)", c), "%", 0.f, 100.f);
			configSwitch(LOCK_PARAM + t, 0.f, 1.f, 0.f, string::f("%c: lock (stop growing, keep what it has)", c), {"Growing", "Locked"});
			configOutput(TRACK_OUTPUT + t, string::f("Track %c trigger", c));
		}
		configButton(SEED_PARAM, "Seed: the last bar that came in becomes the starting phrase");
		configButton(NOW_PARAM, "Rewrite now: grow every unlocked track on the next bar line");
		configSwitch(HOLD_PARAM, 0.f, 1.f, 0.f, "Hold (no growth while on)", {"Off", "On"});
		configButton(REWIND_PARAM, "Rewind: back one generation (hold for a second: back to the seed)");
		configParam(WINDOW_PARAM, 0.f, 1.f, 0.f, "Window (which slice of the grown phrase is kept)", "%", 0.f, 100.f);
		configSwitch(FIT_PARAM, 0.f, 1.f, 0.f, "Fit", {"Window (keep a one-bar slice)", "Squeeze (sample the whole phrase into one bar)"});
		configInput(CLOCK_INPUT, "Clock (one pulse per step; use the same clock as the chain)");
		configInput(RESET_INPUT, "Reset (next clock plays step 1; the phrase is kept)");
		configInput(SEED_INPUT, "Seed trigger");
		configInput(NOW_INPUT, "Rewrite now gate (grows on every bar line while high)");
		configInput(HOLD_INPUT, "Hold gate (no growth while high)");
		configInput(REWIND_INPUT, "Rewind trigger (back one generation)");
		configInput(WINDOW_INPUT, "Window CV (0 to 10 V adds 0 to 100%)");
		configInput(TRIG_INPUT, "Triggers A to F (poly). Fewer channels = fewer tracks");
		configInput(ACT_INPUT, "Active tracks (poly: 10 V = playing, 0 V = muted). Muted tracks freeze");
		configOutput(TRIG_POLY_OUTPUT, "Triggers A to F (poly, 6 channels)");
		configOutput(ACT_OUTPUT, "Active tracks, passed through (poly, 6 channels)");
		configOutput(GEN_OUTPUT, "Generation trigger (any track grew)");
		configOutput(STATE_OUTPUT, "State (poly): 1 to 6 how far each track has grown from its seed (0 to 10 V), "
			"7 generations (0.1 V each)");
		settingsDivider.setDivision(16);
		lightDivider.setDivision(64);
		engine.rng.seed(random::u64());
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.seeded = false;
		for (int t = 0; t < NT; t++) engine.tr[t] = rw::Track();
		for (int i = 0; i < MAXL; i++) engine.capture[i] = 0;
	}

	void updateSettings() {
		for (int t = 0; t < NT; t++) {
			engine.hitRule[t] = clamp((int) std::round(params[HIT_PARAM + t].getValue()), 0, rw::NRULE - 1);
			engine.restRule[t] = clamp((int) std::round(params[REST_PARAM + t].getValue()), 0, rw::NRULE - 1);
			engine.every[t] = rw::EVERY[clamp((int) std::round(params[EVERY_PARAM + t].getValue()), 0, 3)];
			engine.amount[t] = params[AMOUNT_PARAM + t].getValue();
			engine.locked[t] = params[LOCK_PARAM + t].getValue() > 0.5f;
		}
		engine.window = clamp(params[WINDOW_PARAM].getValue() + inputs[WINDOW_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.squeeze = params[FIT_PARAM].getValue() > 0.5f;
	}

	void process(const ProcessArgs& args) override {
		if (settingsDivider.process()) updateSettings();

		// which tracks play: missing poly channels don't exist, ACT mutes the rest
		bool polyIn = inputs[TRIG_INPUT].isConnected();
		int polyCh = inputs[TRIG_INPUT].getChannels();
		int actCh = inputs[ACT_INPUT].getChannels();
		uint8_t active = 0;
		float act[NT];
		for (int t = 0; t < NT; t++) {
			bool exists = !polyIn || t < polyCh;
			bool on = t >= actCh || inputs[ACT_INPUT].getVoltage(t) >= 1.f;
			act[t] = on ? 10.f : 0.f;
			if (exists && on) active |= 1 << t;
		}
		engine.active = active;
		engine.hold = params[HOLD_PARAM].getValue() > 0.5f || inputs[HOLD_INPUT].getVoltage() >= 1.f;
		engine.rewriteGate = inputs[NOW_INPUT].getVoltage() >= 1.f;

		if (reqUnseed) { engine.unseed(); reqUnseed = false; }
		if (reqSeed) { engine.seedNow(); reqSeed = false; }
		if (reqToSeed) { engine.rewind(true); reqToSeed = false; }

		bool seedEdge = seedButton.process(params[SEED_PARAM].getValue() > 0.f);
		if (seedTrigger.process(inputs[SEED_INPUT].getVoltage(), 0.1f, 1.f)) seedEdge = true;
		if (nowButton.process(params[NOW_PARAM].getValue() > 0.f)) engine.rewritePending = true;

		// rewind: a tap goes back one generation, holding for 0.6 s goes to the seed
		bool held = params[REWIND_PARAM].getValue() > 0.f;
		if (held) {
			if (!rewindWasHeld) { rewindHeld = 0.f; rewindLongDone = false; }
			rewindHeld += args.sampleTime;
			if (!rewindLongDone && rewindHeld > 0.6f) {
				engine.rewind(true);
				rewindLongDone = true;
				rewindFlash = 0.4f;
			}
		}
		else if (rewindWasHeld && !rewindLongDone) {
			engine.rewind(false);
			rewindFlash = 0.15f;
		}
		rewindWasHeld = held;
		if (rewindTrigger.process(inputs[REWIND_INPUT].getVoltage(), 0.1f, 1.f)) {
			engine.rewind(false);
			rewindFlash = 0.15f;
		}

		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		bool in[NT];
		for (int t = 0; t < NT; t++)
			in[t] = inTrigger[t].process(t < polyCh ? inputs[TRIG_INPUT].getVoltage(t) : 0.f, 0.1f, 1.f);
		uint8_t before = engine.lastFired;
		engine.process(args.sampleTime, clockEdge, resetEdge, in);
		// seed after this sample's inputs, so a trigger arriving with the press counts
		if (seedEdge) engine.seedNow();

		uint8_t fresh = clockEdge ? engine.lastFired : (uint8_t) (engine.lastFired & ~before);
		outputs[TRIG_POLY_OUTPUT].setChannels(NT);
		outputs[ACT_OUTPUT].setChannels(NT);
		for (int t = 0; t < NT; t++) {
			float v = engine.out[t].high ? 10.f : 0.f;
			outputs[TRACK_OUTPUT + t].setVoltage(v);
			outputs[TRIG_POLY_OUTPUT].setVoltage(v, t);
			outputs[ACT_OUTPUT].setVoltage(act[t], t);
			if (fresh & (1 << t)) flash[t] = 0.12f;
			else if (flash[t] > 0.f) flash[t] -= args.sampleTime;
		}
		outputs[GEN_OUTPUT].setVoltage(engine.genTrig.high ? 10.f : 0.f);
		outputs[STATE_OUTPUT].setChannels(7);
		for (int t = 0; t < NT; t++) outputs[STATE_OUTPUT].setVoltage(10.f * engine.distance(t), t);
		outputs[STATE_OUTPUT].setVoltage(std::min(10.f, 0.1f * engine.maxGen()), 6);
		if (rewindFlash > 0.f) rewindFlash -= args.sampleTime;

		if (lightDivider.process()) {
			for (int t = 0; t < NT; t++) lights[LOCK_LIGHT + t].setBrightness(engine.locked[t] ? 1.f : 0.f);
			lights[SEED_LIGHT].setBrightness(engine.seeded ? 1.f : 0.f);
			lights[NOW_LIGHT].setBrightness(engine.rewritePending || engine.rewriteGate ? 1.f : 0.f);
			lights[HOLD_LIGHT].setBrightness(engine.hold ? 1.f : 0.f);
			lights[REWIND_LIGHT].setBrightness(rewindFlash > 0.f ? 1.f : 0.f);
		}
	}

	// ---- saving: phrases as strings like "x..x" ----
	static std::string bitsToString(const uint8_t* b, int n) {
		std::string s(n, '.');
		for (int i = 0; i < n; i++) if (b[i]) s[i] = 'x';
		return s;
	}
	static void stringToBits(const char* s, uint8_t* b, int n) {
		for (int i = 0; i < n; i++) b[i] = (s && (int) strlen(s) > i && s[i] == 'x') ? 1 : 0;
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "seeded", json_boolean(engine.seeded));
		json_object_set_new(root, "length", json_integer(engine.length));
		json_t* cap = json_array();
		for (int i = 0; i < MAXL; i++) json_array_append_new(cap, json_integer(engine.capture[i]));
		json_object_set_new(root, "capture", cap);
		json_t* tracks = json_array();
		for (int t = 0; t < NT; t++) {
			const rw::Track& k = engine.tr[t];
			json_t* o = json_object();
			json_object_set_new(o, "seed", json_string(bitsToString(k.seed, MAXL).c_str()));
			json_object_set_new(o, "phrase", json_string(bitsToString(k.phrase, MAXL).c_str()));
			json_t* h = json_array();
			// oldest first
			for (int i = 0; i < k.histCount; i++) {
				int p = (k.histPos - k.histCount + i + 2 * rw::NHIST) % rw::NHIST;
				json_array_append_new(h, json_string(bitsToString(k.hist[p], MAXL).c_str()));
			}
			json_object_set_new(o, "history", h);
			json_object_set_new(o, "gen", json_integer(k.gen));
			json_object_set_new(o, "bars", json_integer(k.barCount));
			json_array_append_new(tracks, o);
		}
		json_object_set_new(root, "tracks", tracks);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* s = json_object_get(root, "seeded")) engine.seeded = json_boolean_value(s);
		if (json_t* l = json_object_get(root, "length")) engine.length = json_integer_value(l) <= 8 ? 8 : 16;
		if (json_t* cap = json_object_get(root, "capture"))
			for (int i = 0; i < MAXL && i < (int) json_array_size(cap); i++)
				engine.capture[i] = (uint8_t) (json_integer_value(json_array_get(cap, i)) & 0x3F);
		if (json_t* tracks = json_object_get(root, "tracks"))
			for (int t = 0; t < NT && t < (int) json_array_size(tracks); t++) {
				json_t* o = json_array_get(tracks, t);
				rw::Track& k = engine.tr[t];
				stringToBits(json_string_value(json_object_get(o, "seed")), k.seed, MAXL);
				stringToBits(json_string_value(json_object_get(o, "phrase")), k.phrase, MAXL);
				k.histCount = k.histPos = 0;
				if (json_t* h = json_object_get(o, "history")) {
					int n = std::min((int) json_array_size(h), rw::NHIST);
					for (int i = 0; i < n; i++)
						stringToBits(json_string_value(json_array_get(h, i)), k.hist[i], MAXL);
					k.histCount = n;
					k.histPos = n % rw::NHIST;
				}
				k.gen = std::max(0, (int) json_integer_value(json_object_get(o, "gen")));
				k.barCount = std::max(0, (int) json_integer_value(json_object_get(o, "bars")));
			}
	}

	void copyRules(int from) {
		for (int t = 0; t < NT; t++) {
			if (t == from) continue;
			params[HIT_PARAM + t].setValue(params[HIT_PARAM + from].getValue());
			params[REST_PARAM + t].setValue(params[REST_PARAM + from].getValue());
			params[EVERY_PARAM + t].setValue(params[EVERY_PARAM + from].getValue());
			params[AMOUNT_PARAM + t].setValue(params[AMOUNT_PARAM + from].getValue());
		}
	}
};

static std::shared_ptr<window::Font> rwFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

static void roundBox(NVGcontext* vg, float x, float y, float w, float h, NVGcolor col) {
	nvgBeginPath(vg);
	nvgRoundedRect(vg, x, y, w, h, mm2px(0.9f));
	nvgFillColor(vg, col);
	nvgFill(vg);
}

// A step drawn as a dot: a big coloured one for a hit, a small grey one for a rest.
static void stepDot(NVGcontext* vg, float cx, float cy, bool hit, NVGcolor col, float big) {
	nvgBeginPath(vg);
	nvgCircle(vg, cx, cy, hit ? big : big * 0.35f);
	nvgFillColor(vg, hit ? col : DIM);
	nvgFill(vg);
}

// ---- rule card: click to step through the pictures, shift-click to go back ----
struct RuleCard : ParamWidget {
	int lane = 0;
	bool forHit = true;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (ParamQuantity* pq = getParamQuantity()) {
				int n = (int) pq->getMaxValue() + 1;
				int v = (int) std::round(pq->getValue());
				v = (e.mods & GLFW_MOD_SHIFT) ? (v + n - 1) % n : (v + 1) % n;
				pq->setValue((float) v);
			}
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		roundBox(args.vg, 0, 0, box.size.x, box.size.y, FRAME);
		ParamWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			ParamQuantity* pq = getParamQuantity();
			int v = pq ? clamp((int) std::round(pq->getValue()), 0, rw::NRULE - 1) : (forHit ? rw::DEFAULT_HIT : rw::DEFAULT_REST);
			const char* rule = forHit ? rw::HIT_RULES[v] : rw::REST_RULES[v];
			NVGcolor col = LANE_COL[lane];
			float cy = box.size.y / 2, big = mm2px(0.85f);
			// what is rewritten, an arrow, then what it becomes
			stepDot(args.vg, mm2px(1.9f), cy, forHit, col, big);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, mm2px(3.6f), cy - mm2px(0.8f));
			nvgLineTo(args.vg, mm2px(4.5f), cy);
			nvgLineTo(args.vg, mm2px(3.6f), cy + mm2px(0.8f));
			nvgStrokeColor(args.vg, TEXT);
			nvgStrokeWidth(args.vg, mm2px(0.25f));
			nvgStroke(args.vg);
			int n = (int) strlen(rule);
			float x0 = mm2px(5.6f), x1 = box.size.x - mm2px(1.2f);
			float dx = std::min(mm2px(2.4f), (x1 - x0) / n);
			float start = (x0 + x1) / 2 - dx * (n - 1) / 2;
			for (int i = 0; i < n; i++) stepDot(args.vg, start + i * dx, cy, rule[i] == 'x', col, big);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

// ---- EVERY card: click to step 1, 2, 4, 8 bars ----
struct EveryCard : ParamWidget {
	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (ParamQuantity* pq = getParamQuantity()) {
				int v = (int) std::round(pq->getValue());
				v = (e.mods & GLFW_MOD_SHIFT) ? (v + 3) % 4 : (v + 1) % 4;
				pq->setValue((float) v);
			}
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		roundBox(args.vg, 0, 0, box.size.x, box.size.y, FRAME);
		ParamWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			ParamQuantity* pq = getParamQuantity();
			int v = rw::EVERY[pq ? clamp((int) std::round(pq->getValue()), 0, 3) : 1];
			std::shared_ptr<window::Font> font = rwFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, nvgRGB(0xF0, 0xC0, 0x40));
			nvgFontSize(args.vg, mm2px(3.4f));
			nvgText(args.vg, box.size.x / 2, box.size.y * 0.42f, string::f("%d", v).c_str(), NULL);
			nvgFillColor(args.vg, DIM);
			nvgFontSize(args.vg, mm2px(1.8f));
			nvgText(args.vg, box.size.x / 2, box.size.y * 0.8f, v == 1 ? "bar" : "bars", NULL);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

// ---- the last few generations of one track, newest at the bottom ----
struct GenDisplay : Widget {
	Rewrite* module = nullptr;
	int lane = 0;

	void draw(const DrawArgs& args) override {
		roundBox(args.vg, -mm2px(0.5f), -mm2px(0.5f), box.size.x + mm2px(1.f), box.size.y + mm2px(1.f), FRAME);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			const rw::Engine* e = module ? &module->engine : nullptr;
			int L = e ? e->len() : MAXL;
			float cw = box.size.x / MAXL, rh = box.size.y / GEN_ROWS;
			bool on = e ? (e->active & (1 << lane)) != 0 : true;
			NVGcolor col = LANE_COL[lane];
			// playhead column
			if (e && e->started) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, e->pos * cw, 0, cw, box.size.y);
				nvgFillColor(args.vg, nvgRGB(0x30, 0x30, 0x3A));
				nvgFill(args.vg);
			}
			for (int row = 0; row < GEN_ROWS; row++) {
				int back = GEN_ROWS - 1 - row;
				uint8_t bits[MAXL] = {};
				bool have;
				if (!e) {
					// browser preview: a Thue-Morse-ish example
					static const char* demo[GEN_ROWS] = {"x...x...x...x...", "x..x..x.x..x..x.", "x..x.x.xx..x.x.x", "x..x.xx..xx.x..x"};
					for (int i = 0; i < MAXL; i++) bits[i] = demo[row][i] == 'x';
					have = true;
				}
				else if (!e->seeded) {
					// not seeded: show what is coming in, on the bottom row
					have = back == 0;
					if (have) for (int i = 0; i < MAXL; i++) bits[i] = (e->capture[i] >> lane) & 1;
				}
				else have = e->generation(lane, back, bits);
				if (!have) continue;
				float alpha = back == 0 ? 1.f : 0.25f + 0.2f * (GEN_ROWS - 1 - back);
				if (!on || (e && !e->seeded)) alpha *= 0.35f;
				for (int i = 0; i < L; i++)
					stepDot(args.vg, (i + 0.5f) * cw, (row + 0.5f) * rh, bits[i], nvgTransRGBAf(col, alpha),
						std::min(cw, rh) * 0.36f);
			}
			// flash the playing step when it fires
			if (module && module->flash[lane] > 0.f && e->started) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, e->pos * cw, (GEN_ROWS - 1) * rh, cw, rh);
				nvgFillColor(args.vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0x90));
				nvgFill(args.vg);
			}
			if (e && !e->seeded && lane == 0) {
				std::shared_ptr<window::Font> font = rwFont();
				if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, mm2px(2.f));
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgFillColor(args.vg, TEXT);
				nvgText(args.vg, mm2px(0.6f), mm2px(0.3f), "passing through: press SEED", NULL);
			}
			// generation count, top right
			if (e && e->seeded) {
				std::shared_ptr<window::Font> font = rwFont();
				if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
				nvgFontSize(args.vg, mm2px(1.8f));
				nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
				nvgFillColor(args.vg, DIM);
				const char* tag = !on ? "muted" : (e->locked[lane] ? "locked" : "");
				nvgText(args.vg, box.size.x - mm2px(0.4f), mm2px(0.1f),
					string::f("%s g%d", tag, e->tr[lane].gen).c_str(), NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}
};

struct RewriteWidget : ModuleWidget {
	RewriteWidget(Rewrite* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Rewrite.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		using R = Rewrite;
		for (int t = 0; t < NT; t++) {
			float cy = rowY(t);
			RuleCard* h = createParam<RuleCard>(mm2px(Vec(HIT_X, cy - CARD_H / 2)), module, R::HIT_PARAM + t);
			h->box.size = mm2px(Vec(CARD_W, CARD_H));
			h->lane = t;
			h->forHit = true;
			addParam(h);
			RuleCard* r = createParam<RuleCard>(mm2px(Vec(REST_X, cy - CARD_H / 2)), module, R::REST_PARAM + t);
			r->box.size = mm2px(Vec(CARD_W, CARD_H));
			r->lane = t;
			r->forHit = false;
			addParam(r);
			EveryCard* ev = createParam<EveryCard>(mm2px(Vec(EVERY_X, cy - CARD_H / 2)), module, R::EVERY_PARAM + t);
			ev->box.size = mm2px(Vec(EVERY_W, CARD_H));
			addParam(ev);
			addParam(createParamCentered<Trimpot>(mm2px(Vec(X_AMT, cy)), module, R::AMOUNT_PARAM + t));
			addParam(createLightParamCentered<VCVLightLatch<MediumSimpleLight<WhiteLight>>>(mm2px(Vec(X_LOCK, cy)), module, R::LOCK_PARAM + t, R::LOCK_LIGHT + t));
			GenDisplay* g = createWidget<GenDisplay>(mm2px(Vec(GEN_X, cy - GEN_H / 2)));
			g->box.size = mm2px(Vec(GEN_W, GEN_H));
			g->module = module;
			g->lane = t;
			addChild(g);
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_OUT, cy)), module, R::TRACK_OUTPUT + t));
		}

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G1[0], Y_G1)), module, R::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G1[1], Y_G1)), module, R::RESET_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(X_G1[2], Y_G1)), module, R::SEED_PARAM, R::SEED_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G1[3], Y_G1)), module, R::SEED_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(X_G1[4], Y_G1)), module, R::NOW_PARAM, R::NOW_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G1[5], Y_G1)), module, R::NOW_INPUT));
		addParam(createLightParamCentered<VCVLightBezelLatch<WhiteLight>>(mm2px(Vec(X_G1[6], Y_G1)), module, R::HOLD_PARAM, R::HOLD_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G1[7], Y_G1)), module, R::HOLD_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(X_G1[8], Y_G1)), module, R::REWIND_PARAM, R::REWIND_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G1[9], Y_G1)), module, R::REWIND_INPUT));

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_G2[0], Y_G2)), module, R::WINDOW_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G2[1], Y_G2)), module, R::WINDOW_INPUT));
		addParam(createParamCentered<CKSS>(mm2px(Vec(X_G2[2], Y_G2)), module, R::FIT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G2[3], Y_G2)), module, R::TRIG_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_G2[4], Y_G2)), module, R::ACT_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_G2[5], Y_G2)), module, R::TRIG_POLY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_G2[6], Y_G2)), module, R::ACT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_G2[7], Y_G2)), module, R::GEN_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(X_G2[8], Y_G2)), module, R::STATE_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		Rewrite* module = getModule<Rewrite>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Length", {"8 steps", "16 steps"},
			[=]() { return module->engine.length <= 8 ? 0 : 1; },
			[=](int i) { module->engine.length = i == 0 ? 8 : 16; }));
		menu->addChild(createMenuItem("Seed now", "", [=]() { module->reqSeed = true; }));
		menu->addChild(createMenuItem("Rewind all tracks to the seed", "", [=]() { module->reqToSeed = true; }));
		menu->addChild(createMenuItem("Unseed (pass triggers through again)", "", [=]() { module->reqUnseed = true; }));
		menu->addChild(createSubmenuItem("Copy a track's rules to all tracks", "", [=](Menu* sub) {
			for (int t = 0; t < NT; t++)
				sub->addChild(createMenuItem(string::f("From %c", 'A' + t), "", [=]() { module->copyRules(t); }));
		}));
	}
};

Model* modelRewrite = createModel<Rewrite, RewriteWidget>("Rewrite");
