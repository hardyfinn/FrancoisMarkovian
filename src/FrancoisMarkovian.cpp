// FrancoisMarkovian.cpp
// Panel, widgets and Rack glue for the Markov trigger sequencer. The sequencing
// itself is in MarkovEngine.hpp. Positions are in mm and must match
// res/make_panel.py.
#include "plugin.hpp"
#include "MarkovEngine.hpp"

using fm::NS;
using fm::NL;

namespace fmp {
// left: the jump matrix (narrow cells, 65% as wide as they are tall)
static const float MX0 = 11.f, MY0 = 18.f, MCW = 8.f, MCH = 12.4f;
// right: the trigger grid
static const float TX0 = 85.5f, TY0 = 18.f, TCW = 10.f, TCH = 7.6f;
static const float EDIT_X = 149.5f, EDIT_Y = 70.f, EDIT_W = 16.f, EDIT_H = 3.6f;
static const float Y_ROW1 = 85.f, Y_ROW2 = 98.f, Y_OUT = 116.f;
static const float X_COL[6] = {90.f, 104.f, 118.f, 132.f, 146.f, 160.f};
static const float DIVTXT_X = 113.f, DIVTXT_W = 22.f, DIVTXT_H = 4.4f;
static const float OUT_X0 = 89.f, OUT_DX = 71.f / 6.f;  // 7 outputs: A..F and the divider
static const NVGcolor SNAP_COL[2] = {nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0x4C, 0xD6, 0xE0)};
static const NVGcolor LANE_COL[NL] = {
	nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0xE8, 0xC5, 0x47), nvgRGB(0x4C, 0xD6, 0xE0),
	nvgRGB(0xB3, 0x8C, 0xF0), nvgRGB(0x7C, 0xD3, 0x6B), nvgRGB(0xF0, 0x6E, 0xA8)};
static const int DIVS[5] = {1, 2, 4, 8, 16};
static const int OFFS[4] = {0, 1, 2, 4};
static const NVGcolor CELL_BG = nvgRGB(0x24, 0x24, 0x2B);
static const NVGcolor CELL_HI = nvgRGB(0x34, 0x34, 0x3E);
static const NVGcolor FRAME = nvgRGB(0x16, 0x16, 0x1B);
} // namespace fmp

using namespace fmp;

struct FrancoisMarkovian : Module {
	enum ParamId {
		EVOLVE_PARAM,
		FREEZE_BUTTON,
		DIV_PARAM,
		OFFSET_PARAM,
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
		DIV_OUTPUT,
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
	int editSnap = 0;          // which snapshot the grids show and edit: 0 = X, 1 = Y
	float flash[NL] = {};      // display flash per track after it fires
	float evolveNow = 0.f;     // knob + CV, for the display

	FrancoisMarkovian() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(EVOLVE_PARAM, 0.f, 1.f, 0.f, "Evolve (X to Y)", "% Y", 0.f, 100.f);
		configButton(FREEZE_BUTTON, "Freeze: loop the last 8 steps played (toggle)");
		configSwitch(DIV_PARAM, 0.f, 4.f, 2.f, "Clock divide", {"1", "2", "4", "8", "16"});
		configSwitch(OFFSET_PARAM, 0.f, 3.f, 0.f, "Clock divide offset (pulses)", {"0", "1", "2", "4"});
		configInput(CLOCK_INPUT, "Clock (each pulse moves the playhead once)");
		configInput(RESET_INPUT, "Reset (next clock plays step 1)");
		configInput(EVOLVE_INPUT, "Evolve CV (0 to 10 V adds 0 to 100%)");
		configInput(FREEZE_INPUT, "Freeze gate (freezes while high)");
		for (int l = 0; l < NL; l++) configOutput(LANE_OUTPUT + l, string::f("Track %c trigger", 'A' + l));
		configOutput(DIV_OUTPUT, "Divided clock");
		lightDivider.setDivision(64);
		engine.rng.seed(random::u64());
		uiRng.seed(random::u64());
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int k = 0; k < 2; k++) {
			engine.snap[k].straight();
			engine.snap[k].defaultTriggers();
		}
		engine.started = false;
		engine.histCount = engine.loopLen = 0;
		freezeLatched = false;
		editSnap = 0;
	}

	int divValue() { return DIVS[clamp((int) std::round(params[DIV_PARAM].getValue()), 0, 4)]; }
	int offsetValue() { return OFFS[clamp((int) std::round(params[OFFSET_PARAM].getValue()), 0, 3)]; }

	void process(const ProcessArgs& args) override {
		if (freezeButton.process(params[FREEZE_BUTTON].getValue() > 0.f))
			freezeLatched = !freezeLatched;
		engine.freeze = freezeLatched || inputs[FREEZE_INPUT].getVoltage() >= 1.f;
		evolveNow = clamp(params[EVOLVE_PARAM].getValue() + inputs[EVOLVE_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.evolve = evolveNow;
		engine.div = divValue();
		engine.offset = offsetValue();

		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		engine.process(args.sampleTime, clockEdge, resetEdge);

		for (int l = 0; l < NL; l++) {
			outputs[LANE_OUTPUT + l].setVoltage(engine.trig[l].high ? 10.f : 0.f);
			if (clockEdge && (engine.lastMask & (1 << l))) flash[l] = 0.12f;
			else if (flash[l] > 0.f) flash[l] -= args.sampleTime;
		}
		outputs[DIV_OUTPUT].setVoltage(engine.divTrig.high ? 10.f : 0.f);

		if (lightDivider.process())
			lights[FREEZE_LIGHT].setBrightness(engine.freeze ? 1.f : 0.f);
	}

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
			json_t* ch = json_array();
			for (int l = 0; l < NL; l++) {
				json_t* row = json_array();
				for (int i = 0; i < NS; i++) json_array_append_new(row, json_real(s.p[l][i]));
				json_array_append_new(ch, row);
			}
			json_object_set_new(o, "chances", ch);
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
						s.w[i / NS][i % NS] = (float) json_number_value(json_array_get(w, i));
				s.normalise();  // v2.0 saved weights; now every row adds up to 100%
				s.clearTriggers();
				if (json_t* ch = json_object_get(o, "chances")) {
					for (int l = 0; l < NL && l < (int) json_array_size(ch); l++) {
						json_t* row = json_array_get(ch, l);
						for (int i = 0; i < NS && i < (int) json_array_size(row); i++)
							s.p[l][i] = clamp((float) json_number_value(json_array_get(row, i)), 0.f, 1.f);
					}
				}
				else if (json_t* h = json_object_get(o, "hits")) {
					// v2.0 patches: on/off hits as strings like "x...x..."
					for (int l = 0; l < NL && l < (int) json_array_size(h); l++) {
						const char* row = json_string_value(json_array_get(h, l));
						for (int i = 0; i < NS; i++) s.p[l][i] = (row && (int) strlen(row) > i && row[i] == 'x') ? 1.f : 0.f;
					}
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
				engine.loop[i].mask = (v >> 8) & 0x3F;
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

static void drawFrame(const Widget::DrawArgs& args, Vec size) {
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, -mm2px(0.6f), -mm2px(0.6f), size.x + mm2px(1.2f), size.y + mm2px(1.2f), mm2px(1.2f));
	nvgFillColor(args.vg, FRAME);
	nvgFill(args.vg);
}

// One cell: background, a bar from the bottom for the value, the value as a
// number along the top, and a thin line at the other snapshot's value.
static void drawCell(const Widget::DrawArgs& args, float x, float y, float w, float h,
	float v, float other, NVGcolor col, bool highlight, float fontSize) {
	nvgBeginPath(args.vg);
	nvgRect(args.vg, x, y, w, h);
	nvgFillColor(args.vg, highlight ? CELL_HI : CELL_BG);
	nvgFill(args.vg);
	if (v > 0.f) {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x, y + h * (1.f - v), w, h * v);
		nvgFillColor(args.vg, nvgTransRGBAf(col, 0.35f + 0.5f * v));
		nvgFill(args.vg);
	}
	if (other > 0.f && std::fabs(other - v) > 0.005f) {
		nvgBeginPath(args.vg);
		float ly = y + h * (1.f - other);
		nvgRect(args.vg, x, std::min(ly, y + h - mm2px(0.3f)), w, mm2px(0.3f));
		nvgFillColor(args.vg, nvgRGB(0xF4, 0xF0, 0xE8));
		nvgFill(args.vg);
	}
	if (v > 0.f) {
		nvgFontSize(args.vg, fontSize);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xF4, 0xF0, 0xE8));
		nvgText(args.vg, x + w / 2, y + mm2px(0.5f) + fontSize / 2, string::f("%d", (int) std::round(100.f * v)).c_str(), NULL);
	}
}

// Shared drag handling: press picks a cell, vertical drag changes its value,
// double-click flips it between 0 and 100%.
struct DragGrid : OpaqueWidget {
	FrancoisMarkovian* module = nullptr;
	int rows = NS, cols = NS;
	int dragRow = -1, dragCol = -1;
	float dragVal = 0.f;

	virtual void beginEdit(int r, int c) = 0;
	virtual void setValue(float v) = 0;
	virtual float currentValue() = 0;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			dragCol = clamp((int) (e.pos.x / (box.size.x / cols)), 0, cols - 1);
			dragRow = clamp((int) (e.pos.y / (box.size.y / rows)), 0, rows - 1);
			if (module) {
				beginEdit(dragRow, dragCol);
				dragVal = currentValue();
			}
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || dragRow < 0 || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		float dy = e.mouseDelta.y / getAbsoluteZoom();
		float range = mm2px(20.f);  // drag 20 mm for the full 0..100%
		if (APP->window->getMods() & GLFW_MOD_SHIFT) range *= 5.f;
		dragVal = clamp(dragVal - dy / range, 0.f, 1.f);
		setValue(dragVal);
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!module || dragRow < 0) return;
		beginEdit(dragRow, dragCol);
		setValue(currentValue() > 0.f ? 0.f : 1.f);
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		drawFrame(args, box.size);
		OpaqueWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			std::shared_ptr<window::Font> font = displayFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			drawCells(args);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
	virtual void drawCells(const DrawArgs& args) = 0;
};

// Preview engine for the module browser: a couple of jumps and the default triggers.
static fm::Engine& previewEngine() {
	static fm::Engine e;
	static bool init = false;
	if (!init) {
		e.snap[0].w[3][4] = 0.7f; e.snap[0].w[3][0] = 0.3f;
		e.snap[0].w[6][7] = 0.4f; e.snap[0].w[6][6] = 0.6f;
		init = true;
	}
	return e;
}

// ---- the 8x8 jump matrix ----
struct MatrixGrid : DragGrid {
	float start[NS];

	void beginEdit(int r, int c) override {
		for (int k = 0; k < NS; k++) start[k] = module->engine.snap[module->editSnap].w[r][k];
	}
	float currentValue() override {
		// an empty row plays as "next step 100%"
		float sum = 0.f;
		for (int k = 0; k < NS; k++) sum += start[k];
		if (sum <= 1e-6f) return dragCol == (dragRow + 1) % NS ? 1.f : 0.f;
		return start[dragCol];
	}
	void setValue(float v) override {
		float out[NS];
		fm::setRowChance(start, out, dragCol, v, dragRow);
		float* row = module->engine.snap[module->editSnap].w[dragRow];
		for (int k = 0; k < NS; k++) row[k] = out[k];
	}

	void drawCells(const DrawArgs& args) override {
		const fm::Engine& e = module ? module->engine : previewEngine();
		int edit = module ? module->editSnap : 0;
		const fm::Snapshot& s = e.snap[edit];
		const fm::Snapshot& o = e.snap[1 - edit];
		const float cw = box.size.x / NS, ch = box.size.y / NS, pad = mm2px(0.35f);
		int cur = (module && e.started) ? e.step : -1;
		int prev = module ? e.prevStep : -1;
		for (int r = 0; r < NS; r++) {
			float sum = 0.f;
			for (int k = 0; k < NS; k++) sum += s.w[r][k];
			for (int k = 0; k < NS; k++) {
				float x = k * cw + pad, y = r * ch + pad, w = cw - 2 * pad, h = ch - 2 * pad;
				drawCell(args, x, y, w, h, s.w[r][k], o.w[r][k], SNAP_COL[edit], r == cur, mm2px(2.4f));
				if (r == prev && k == cur) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x - pad * 0.5f, y - pad * 0.5f, w + pad, h + pad);
					nvgStrokeColor(args.vg, nvgRGB(0xFF, 0xFF, 0xFF));
					nvgStrokeWidth(args.vg, mm2px(0.35f));
					nvgStroke(args.vg);
				}
			}
			// empty row: show the fallback to the next step as an outline
			if (sum <= 1e-6f) {
				int k = (r + 1) % NS;
				float x = k * cw + pad, y = r * ch + pad, w = cw - 2 * pad, h = ch - 2 * pad;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x + w * 0.25f, y + h * 0.3f, w * 0.5f, h * 0.4f);
				nvgStrokeColor(args.vg, nvgTransRGBAf(SNAP_COL[edit], 0.6f));
				nvgStrokeWidth(args.vg, mm2px(0.25f));
				nvgStroke(args.vg);
			}
		}
	}
};

// ---- the 6x8 trigger chances ----
struct TriggerGrid : DragGrid {
	TriggerGrid() { rows = NL; cols = NS; }

	void beginEdit(int r, int c) override {}
	float currentValue() override { return module->engine.snap[module->editSnap].p[dragRow][dragCol]; }
	void setValue(float v) override { module->engine.snap[module->editSnap].p[dragRow][dragCol] = v; }

	void drawCells(const DrawArgs& args) override {
		const fm::Engine& e = module ? module->engine : previewEngine();
		int edit = module ? module->editSnap : 0;
		const fm::Snapshot& s = e.snap[edit];
		const fm::Snapshot& o = e.snap[1 - edit];
		const float cw = box.size.x / NS, ch = box.size.y / NL, pad = mm2px(0.35f);
		int cur = (module && e.started) ? e.step : -1;
		for (int l = 0; l < NL; l++)
			for (int k = 0; k < NS; k++) {
				float x = k * cw + pad, y = l * ch + pad, w = cw - 2 * pad, h = ch - 2 * pad;
				drawCell(args, x, y, w, h, s.p[l][k], o.p[l][k], LANE_COL[l], k == cur, mm2px(2.1f));
				if (module && k == cur && module->flash[l] > 0.f) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x, y, w, h);
					nvgFillColor(args.vg, nvgRGBA(0xFF, 0xFF, 0xFF, 0xB0));
					nvgFill(args.vg);
				}
			}
	}
};

// ---- X | Y: which snapshot the grids edit ----
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
		nvgFillColor(args.vg, FRAME);
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
				nvgFillColor(args.vg, k == edit ? FRAME : nvgTransRGBAf(SNAP_COL[k], 0.8f));
				nvgText(args.vg, k * half + half / 2, box.size.y / 2 + mm2px(0.1f), k == 0 ? "X" : "Y", NULL);
			}
			// where evolve sits between X and Y
			float x = pad + evo * (box.size.x - 2 * pad);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x - mm2px(0.3f), box.size.y - mm2px(0.5f), mm2px(0.6f), mm2px(0.5f));
			nvgFillColor(args.vg, nvgRGB(0xFF, 0xFF, 0xFF));
			nvgFill(args.vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

// ---- readout for the clock divider: "÷4  +2" ----
struct DivReadout : Widget {
	FrancoisMarkovian* module = nullptr;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.8f));
		nvgFillColor(args.vg, FRAME);
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int d = module ? module->divValue() : 4, o = module ? module->offsetValue() : 0;
			std::shared_ptr<window::Font> font = displayFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(2.8f));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, nvgRGB(0xF0, 0xC0, 0x40));
			nvgText(args.vg, box.size.x / 2, box.size.y / 2 + mm2px(0.1f), string::f("/%d  +%d", d, o).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
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

		MatrixGrid* m = createWidget<MatrixGrid>(mm2px(Vec(MX0, MY0)));
		m->box.size = mm2px(Vec(NS * MCW, NS * MCH));
		m->module = module;
		addChild(m);

		TriggerGrid* t = createWidget<TriggerGrid>(mm2px(Vec(TX0, TY0)));
		t->box.size = mm2px(Vec(NS * TCW, NL * TCH));
		t->module = module;
		addChild(t);

		EditToggle* ed = createWidget<EditToggle>(mm2px(Vec(EDIT_X, EDIT_Y)));
		ed->box.size = mm2px(Vec(EDIT_W, EDIT_H));
		ed->module = module;
		addChild(ed);

		using F = FrancoisMarkovian;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_COL[0], Y_ROW1)), module, F::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_COL[1], Y_ROW1)), module, F::RESET_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_COL[2], Y_ROW1)), module, F::EVOLVE_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_COL[3], Y_ROW1)), module, F::EVOLVE_INPUT));
		addParam(createLightParamCentered<VCVLightBezel<WhiteLight>>(mm2px(Vec(X_COL[4], Y_ROW1)), module, F::FREEZE_BUTTON, F::FREEZE_LIGHT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_COL[5], Y_ROW1)), module, F::FREEZE_INPUT));

		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_COL[0], Y_ROW2)), module, F::DIV_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_COL[1], Y_ROW2)), module, F::OFFSET_PARAM));
		DivReadout* dr = createWidget<DivReadout>(mm2px(Vec(DIVTXT_X, Y_ROW2 - DIVTXT_H / 2)));
		dr->box.size = mm2px(Vec(DIVTXT_W, DIVTXT_H));
		dr->module = module;
		addChild(dr);

		for (int l = 0; l < NL; l++)
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X0 + l * OUT_DX, Y_OUT)), module, F::LANE_OUTPUT + l));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X0 + NL * OUT_DX, Y_OUT)), module, F::DIV_OUTPUT));
	}

	void appendContextMenu(Menu* menu) override {
		FrancoisMarkovian* module = getModule<FrancoisMarkovian>();
		const char* names[3] = {"X", "Y", "X and Y"};
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
		menu->addChild(createMenuItem("Copy X to Y (matrix and triggers)", "", [=]() { module->copySnap(0, 1); }));
		menu->addChild(createMenuItem("Copy Y to X (matrix and triggers)", "", [=]() { module->copySnap(1, 0); }));
		menu->addChild(createSubmenuItem("Clear triggers", "", [=](Menu* sub) {
			for (int k = 0; k < 3; k++)
				sub->addChild(createMenuItem(names[k], "", [=]() {
					if (k != 1) module->engine.snap[0].clearTriggers();
					if (k != 0) module->engine.snap[1].clearTriggers();
				}));
		}));
	}
};

Model* modelFrancoisMarkovian = createModel<FrancoisMarkovian, FrancoisMarkovianWidget>("FrancoisMarkovian");
