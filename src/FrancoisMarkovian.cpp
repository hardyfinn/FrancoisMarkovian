// FrancoisMarkovian.cpp
// Panel, widgets and Rack glue for the Markov drum sequencer. The sequencing
// itself is in MarkovEngine.hpp. Positions are in mm and must match
// res/make_panel.py.
#include "plugin.hpp"
#include "MarkovEngine.hpp"

using fm::NS;
using fm::NL;

namespace fmp {
static const float GRID_X0 = 11.5f, CELL = 6.4f;      // matrix and hits share columns
static const float MATRIX_Y0 = 17.f;                   // 8 rows of CELL
static const float HITS_Y0 = 72.f, HIT_H = 4.6f;      // 4 lane rows
static const float EDIT_X = 46.f, EDIT_Y = 91.8f, EDIT_W = 16.f, EDIT_H = 3.6f;
static const float Y_CTRL = 105.5f, Y_OUT = 118.5f;
static const float X_CLOCK = 8.f, X_RESET = 18.f, X_EVOLVE = 30.5f, X_EVO_CV = 41.f, X_FREEZE = 52.f, X_FRZ_IN = 62.5f;
static const float OUT_X[NL] = {13.f, 28.f, 43.f, 58.f};
static const NVGcolor SNAP_COL[2] = {nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0x4C, 0xD6, 0xE0)};
static const NVGcolor LANE_COL[NL] = {
	nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0xE8, 0xC5, 0x47), nvgRGB(0x4C, 0xD6, 0xE0), nvgRGB(0xB3, 0x8C, 0xF0)};
} // namespace fmp

using namespace fmp;

struct FrancoisMarkovian : Module {
	enum ParamId {
		EVOLVE_PARAM,
		FREEZE_BUTTON,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		EVOLVE_INPUT,
		FREEZE_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(LANE_OUTPUT, NL),
		OUTPUTS_LEN
	};
	enum LightId {
		FREEZE_LIGHT,
		LIGHTS_LEN
	};

	fm::Engine engine;
	fm::Rng uiRng;  // for the menu's randomise, kept apart from the audio thread's
	dsp::SchmittTrigger clockTrigger, resetTrigger;
	dsp::BooleanTrigger freezeButton;
	dsp::ClockDivider lightDivider;
	bool freezeLatched = false;
	int editSnap = 0;          // which snapshot the grids show and edit: 0 = A, 1 = B
	float flash[NL] = {};      // display flash per lane after it fires
	float evolveNow = 0.f;     // knob + CV, for the display

	FrancoisMarkovian() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(EVOLVE_PARAM, 0.f, 1.f, 0.f, "Evolve (A to B)", "% B", 0.f, 100.f);
		configButton(FREEZE_BUTTON, "Freeze: loop the last 8 steps played (toggle)");
		configInput(CLOCK_INPUT, "Clock (each pulse moves the playhead once)");
		configInput(RESET_INPUT, "Reset (next clock plays step 1)");
		configInput(EVOLVE_INPUT, "Evolve CV (0 to 10 V adds 0 to 100%)");
		configInput(FREEZE_INPUT, "Freeze gate (freezes while high)");
		const char* lanes[NL] = {"Bass drum", "Snare", "Closed hat", "Rim"};
		for (int l = 0; l < NL; l++) configOutput(LANE_OUTPUT + l, std::string(lanes[l]) + " trigger");
		lightDivider.setDivision(64);
		engine.rng.seed(random::u64());
		uiRng.seed(random::u64());
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.snap[0].straight();
		engine.snap[1].straight();
		engine.defaultHits();
		engine.started = false;
		engine.histCount = engine.loopLen = 0;
		freezeLatched = false;
		editSnap = 0;
	}

	void process(const ProcessArgs& args) override {
		if (freezeButton.process(params[FREEZE_BUTTON].getValue() > 0.f))
			freezeLatched = !freezeLatched;
		engine.freeze = freezeLatched || inputs[FREEZE_INPUT].getVoltage() >= 1.f;
		evolveNow = clamp(params[EVOLVE_PARAM].getValue() + inputs[EVOLVE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.evolve = evolveNow;

		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		engine.process(args.sampleTime, clockEdge, resetEdge);

		for (int l = 0; l < NL; l++) {
			outputs[LANE_OUTPUT + l].setVoltage(engine.trig[l].high ? 10.f : 0.f);
			if (clockEdge && (engine.lastMask & (1 << l))) flash[l] = 0.12f;
			else if (flash[l] > 0.f) flash[l] -= args.sampleTime;
		}

		if (lightDivider.process())
			lights[FREEZE_LIGHT].setBrightness(engine.freeze ? 1.f : 0.f);
	}

	// ---- menu helpers ----
	void copySnap(int from, int to) { engine.snap[to] = engine.snap[from]; }

	// ---- saving ----
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* snaps = json_array();
		for (int k = 0; k < 2; k++) {
			const fm::Snapshot& s = engine.snap[k];
			json_t* o = json_object();
			json_t* w = json_array();
			for (int r = 0; r < NS; r++)
				for (int c = 0; c < NS; c++) json_array_append_new(w, json_real(s.w[r][c]));
			json_object_set_new(o, "matrix", w);
			json_t* h = json_array();
			for (int l = 0; l < NL; l++) {
				std::string row;
				for (int i = 0; i < NS; i++) row += s.hit[l][i] ? 'x' : '.';
				json_array_append_new(h, json_string(row.c_str()));
			}
			json_object_set_new(o, "hits", h);
			json_array_append_new(snaps, o);
		}
		json_object_set_new(root, "snapshots", snaps);
		json_object_set_new(root, "edit", json_integer(editSnap));
		json_object_set_new(root, "freeze", json_boolean(freezeLatched));
		json_t* loop = json_array();
		for (int i = 0; i < engine.loopLen; i++)
			json_array_append_new(loop, json_integer(engine.loop[i].step | (engine.loop[i].mask << 8)));
		json_object_set_new(root, "loop", loop);
		json_object_set_new(root, "loopPos", json_integer(engine.loopPos));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* snaps = json_object_get(root, "snapshots"))
			for (int k = 0; k < 2 && k < (int) json_array_size(snaps); k++) {
				json_t* o = json_array_get(snaps, k);
				fm::Snapshot& s = engine.snap[k];
				if (json_t* w = json_object_get(o, "matrix"))
					for (int i = 0; i < NS * NS && i < (int) json_array_size(w); i++)
						s.w[i / NS][i % NS] = clamp((float) json_number_value(json_array_get(w, i)), 0.f, 1.f);
				if (json_t* h = json_object_get(o, "hits"))
					for (int l = 0; l < NL && l < (int) json_array_size(h); l++) {
						const char* row = json_string_value(json_array_get(h, l));
						for (int i = 0; i < NS; i++) s.hit[l][i] = row && (int) strlen(row) > i && row[i] == 'x';
					}
			}
		if (json_t* e = json_object_get(root, "edit"))
			editSnap = clamp((int) json_integer_value(e), 0, 1);
		if (json_t* f = json_object_get(root, "freeze"))
			freezeLatched = json_boolean_value(f);
		if (json_t* loop = json_object_get(root, "loop")) {
			engine.loopLen = std::min((int) json_array_size(loop), NS);
			for (int i = 0; i < engine.loopLen; i++) {
				int v = json_integer_value(json_array_get(loop, i));
				engine.loop[i].step = clamp(v & 0xFF, 0, NS - 1);
				engine.loop[i].mask = (v >> 8) & 0x0F;
			}
			// keep a reloaded freeze looping the same steps
			engine.wasFrozen = freezeLatched && engine.loopLen > 0;
		}
		if (json_t* p = json_object_get(root, "loopPos"))
			engine.loopPos = engine.loopLen > 0 ? clamp((int) json_integer_value(p), 0, engine.loopLen - 1) : 0;
	}
};

static std::shared_ptr<window::Font> displayFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

// ---- the 8x8 jump matrix: drag a cell up or down, double-click for 0/100% ----
struct MatrixGrid : OpaqueWidget {
	FrancoisMarkovian* module = nullptr;
	int dragRow = -1, dragCol = -1;

	float cellPx() { return box.size.x / NS; }

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			dragCol = clamp((int) (e.pos.x / cellPx()), 0, NS - 1);
			dragRow = clamp((int) (e.pos.y / cellPx()), 0, NS - 1);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || dragRow < 0 || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		float dy = e.mouseDelta.y / getAbsoluteZoom();
		float range = mm2px(18.f);  // drag 18 mm for the full 0..100%
		if (APP->window->getMods() & GLFW_MOD_SHIFT) range *= 5.f;
		float& w = module->engine.snap[module->editSnap].w[dragRow][dragCol];
		w = clamp(w - dy / range, 0.f, 1.f);
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!module || dragRow < 0) return;
		float& w = module->engine.snap[module->editSnap].w[dragRow][dragCol];
		w = w > 0.f ? 0.f : 1.f;
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, -mm2px(0.6f), -mm2px(0.6f), box.size.x + mm2px(1.2f), box.size.y + mm2px(1.2f), mm2px(1.2f));
		nvgFillColor(args.vg, nvgRGB(0x16, 0x16, 0x1B));
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) drawCells(args);
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawCells(const DrawArgs& args) {
		fm::Engine preview;  // module browser: show a straight matrix with a couple of jumps
		preview.snap[0].w[3][0] = 0.4f;
		preview.snap[0].w[6][6] = 0.6f;
		const fm::Engine& e = module ? module->engine : preview;
		int edit = module ? module->editSnap : 0;
		const fm::Snapshot& s = e.snap[edit];
		const float c = cellPx(), pad = mm2px(0.35f);
		const NVGcolor col = SNAP_COL[edit];
		int cur = (module && e.started) ? e.step : -1;
		int prev = module ? e.prevStep : -1;

		std::shared_ptr<window::Font> font = displayFont();
		if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
		nvgFontSize(args.vg, mm2px(1.9f));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

		for (int r = 0; r < NS; r++) {
			float sum = 0.f;
			for (int k = 0; k < NS; k++) sum += s.w[r][k];
			for (int k = 0; k < NS; k++) {
				float x = k * c + pad, y = r * c + pad, sz = c - 2 * pad;
				// cell background; the playhead's current row is lighter
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x, y, sz, sz);
				nvgFillColor(args.vg, r == cur ? nvgRGB(0x34, 0x34, 0x3E) : nvgRGB(0x24, 0x24, 0x2B));
				nvgFill(args.vg);
				// weight as a bar from the bottom
				float w = s.w[r][k];
				if (w > 0.f) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x, y + sz * (1.f - w), sz, sz * w);
					nvgFillColor(args.vg, nvgTransRGBAf(col, 0.35f + 0.5f * w));
					nvgFill(args.vg);
					// the chance of this jump, after the row is shared out
					int pct = (int) std::round(100.f * w / sum);
					nvgFillColor(args.vg, nvgRGB(0xF4, 0xF0, 0xE8));
					nvgText(args.vg, x + sz / 2, y + sz / 2, string::f("%d", pct).c_str(), NULL);
				}
				// the jump just taken
				if (r == prev && k == cur) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x - pad * 0.5f, y - pad * 0.5f, sz + pad, sz + pad);
					nvgStrokeColor(args.vg, nvgRGB(0xFF, 0xFF, 0xFF));
					nvgStrokeWidth(args.vg, mm2px(0.35f));
					nvgStroke(args.vg);
				}
			}
			// empty row: show the fallback to the next step as a dashed hint
			if (sum <= 1e-6f) {
				int k = (r + 1) % NS;
				float x = k * c + pad, y = r * c + pad, sz = c - 2 * pad;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x + sz * 0.3f, y + sz * 0.3f, sz * 0.4f, sz * 0.4f);
				nvgStrokeColor(args.vg, nvgTransRGBAf(col, 0.6f));
				nvgStrokeWidth(args.vg, mm2px(0.25f));
				nvgStroke(args.vg);
			}
		}
	}
};

// ---- the 4x8 drum hits: click to toggle ----
struct HitGrid : OpaqueWidget {
	FrancoisMarkovian* module = nullptr;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (module) {
				int col = clamp((int) (e.pos.x / (box.size.x / NS)), 0, NS - 1);
				int lane = clamp((int) (e.pos.y / (box.size.y / NL)), 0, NL - 1);
				bool& h = module->engine.snap[module->editSnap].hit[lane][col];
				h = !h;
			}
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, -mm2px(0.6f), -mm2px(0.6f), box.size.x + mm2px(1.2f), box.size.y + mm2px(1.2f), mm2px(1.2f));
		nvgFillColor(args.vg, nvgRGB(0x16, 0x16, 0x1B));
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) drawCells(args);
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawCells(const DrawArgs& args) {
		fm::Engine preview;
		const fm::Engine& e = module ? module->engine : preview;
		int edit = module ? module->editSnap : 0;
		const fm::Snapshot& s = e.snap[edit];
		const fm::Snapshot& other = e.snap[1 - edit];
		const float cw = box.size.x / NS, ch = box.size.y / NL, pad = mm2px(0.35f);
		int cur = (module && e.started) ? e.step : -1;
		for (int l = 0; l < NL; l++)
			for (int k = 0; k < NS; k++) {
				float x = k * cw + pad, y = l * ch + pad, w = cw - 2 * pad, h = ch - 2 * pad;
				bool on = s.hit[l][k];
				bool lit = module && k == cur && module->flash[l] > 0.f;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x, y, w, h);
				NVGcolor bg = k == cur ? nvgRGB(0x34, 0x34, 0x3E) : nvgRGB(0x24, 0x24, 0x2B);
				nvgFillColor(args.vg, lit ? nvgRGB(0xFF, 0xFF, 0xFF) : on ? nvgTransRGBAf(LANE_COL[l], k == cur ? 1.f : 0.75f) : bg);
				nvgFill(args.vg);
				// a small dot where the other snapshot has a hit
				if (other.hit[l][k]) {
					nvgBeginPath(args.vg);
					nvgCircle(args.vg, x + w - mm2px(0.8f), y + mm2px(0.8f), mm2px(0.42f));
					nvgFillColor(args.vg, on || lit ? nvgRGB(0x16, 0x16, 0x1B) : SNAP_COL[1 - edit]);
					nvgFill(args.vg);
				}
			}
	}
};

// ---- A | B: which snapshot the grids edit ----
struct EditToggle : OpaqueWidget {
	FrancoisMarkovian* module = nullptr;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (module) module->editSnap = e.pos.x < box.size.x / 2 ? 0 : 1;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.8f));
		nvgFillColor(args.vg, nvgRGB(0x16, 0x16, 0x1B));
		nvgFill(args.vg);
		OpaqueWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int edit = module ? module->editSnap : 0;
			float evo = module ? module->evolveNow : 0.f;
			float half = box.size.x / 2, pad = mm2px(0.4f);
			std::shared_ptr<window::Font> font = displayFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(2.6f));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			for (int k = 0; k < 2; k++) {
				if (k == edit) {
					nvgBeginPath(args.vg);
					nvgRoundedRect(args.vg, k * half + pad, pad, half - 2 * pad, box.size.y - 2 * pad, mm2px(0.6f));
					nvgFillColor(args.vg, SNAP_COL[k]);
					nvgFill(args.vg);
				}
				nvgFillColor(args.vg, k == edit ? nvgRGB(0x16, 0x16, 0x1B) : nvgTransRGBAf(SNAP_COL[k], 0.8f));
				nvgText(args.vg, k * half + half / 2, box.size.y / 2 + mm2px(0.1f), k == 0 ? "A" : "B", NULL);
			}
			// where evolve sits between A and B
			float x = pad + evo * (box.size.x - 2 * pad);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x - mm2px(0.3f), box.size.y - mm2px(0.5f), mm2px(0.6f), mm2px(0.5f));
			nvgFillColor(args.vg, nvgRGB(0xFF, 0xFF, 0xFF));
			nvgFill(args.vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

struct FrancoisMarkovianWidget : ModuleWidget {
	FrancoisMarkovianWidget(FrancoisMarkovian* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/FrancoisMarkovian.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		MatrixGrid* m = createWidget<MatrixGrid>(mm2px(Vec(GRID_X0, MATRIX_Y0)));
		m->box.size = mm2px(Vec(NS * CELL, NS * CELL));
		m->module = module;
		addChild(m);

		HitGrid* h = createWidget<HitGrid>(mm2px(Vec(GRID_X0, HITS_Y0)));
		h->box.size = mm2px(Vec(NS * CELL, NL * HIT_H));
		h->module = module;
		addChild(h);

		EditToggle* t = createWidget<EditToggle>(mm2px(Vec(EDIT_X, EDIT_Y)));
		t->box.size = mm2px(Vec(EDIT_W, EDIT_H));
		t->module = module;
		addChild(t);

		using F = FrancoisMarkovian;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, Y_CTRL)), module, F::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, Y_CTRL)), module, F::RESET_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_EVOLVE, Y_CTRL)), module, F::EVOLVE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_EVO_CV, Y_CTRL)), module, F::EVOLVE_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(X_FREEZE, Y_CTRL)), module, F::FREEZE_BUTTON, F::FREEZE_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_FRZ_IN, Y_CTRL)), module, F::FREEZE_INPUT));
		for (int l = 0; l < NL; l++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[l], Y_OUT)), module, F::LANE_OUTPUT + l));
	}

	void appendContextMenu(Menu* menu) override {
		FrancoisMarkovian* module = getModule<FrancoisMarkovian>();
		const char* names[3] = {"A", "B", "A and B"};
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Matrix"));
		menu->addChild(createSubmenuItem("Straight (step by step)", "", [=](Menu* sub) {
			for (int k = 0; k < 3; k++)
				sub->addChild(createMenuItem(names[k], "", [=]() {
					if (k != 1) module->engine.snap[0].straight();
					if (k != 0) module->engine.snap[1].straight();
				}));
		}));
		menu->addChild(createSubmenuItem("Randomise", "", [=](Menu* sub) {
			for (int wild = 0; wild < 2; wild++)
				for (int k = 0; k < 2; k++)
					sub->addChild(createMenuItem(string::f("%s %s", wild ? "Wild" : "Gentle", names[k]), "", [=]() {
						module->engine.snap[k].randomise(module->uiRng, wild);
					}));
		}));
		menu->addChild(createMenuLabel("Snapshots"));
		menu->addChild(createMenuItem("Copy A to B (matrix and hits)", "", [=]() { module->copySnap(0, 1); }));
		menu->addChild(createMenuItem("Copy B to A (matrix and hits)", "", [=]() { module->copySnap(1, 0); }));
		menu->addChild(createSubmenuItem("Clear hits", "", [=](Menu* sub) {
			for (int k = 0; k < 3; k++)
				sub->addChild(createMenuItem(names[k], "", [=]() {
					if (k != 1) module->engine.snap[0].clearHits();
					if (k != 0) module->engine.snap[1].clearHits();
				}));
		}));
	}
};

Model* modelFrancoisMarkovian = createModel<FrancoisMarkovian, FrancoisMarkovianWidget>("FrancoisMarkovian");
