// Interlock.cpp
// Panel, widgets and Rack glue for Interlock, the influence-grid trigger
// processor. The logic is in InfluenceEngine.hpp. Positions are in mm and must
// match res/make_panel_interlock.py.
#include "plugin.hpp"
#include "InfluenceEngine.hpp"

using inf::NT;

namespace ilp {
static const float GX0 = 12.f, GY0 = 19.f, GC = 8.5f;        // 6x6 grid of GC mm cells
static const float NEXT_Y = 80.5f, NEXT_H = 4.f;               // "next step" meters
static const float Y_CTRL = 94.f, Y_IN = 104.5f, Y_OUT = 118.f;
static const float X_CLOCK = 10.f, X_RESET = 21.f, X_AMT = 33.f, X_AMT_CV = 44.f, X_MEM = 56.f;
static const float MEMTXT_X = 63.f, MEMTXT_W = 12.f, MEMTXT_H = 4.4f;
static const float JX0 = 10.f, JDX = 12.f;                    // 6 jacks per row
// right column: poly jacks. Inputs on the panel, outputs in the dark box
static const float PX = 72.f, PY_TRIG_IN = 25.f, PY_ACT_IN = 37.f;
static const float PY_TRIG_OUT = 57.5f, PY_ACT_OUT = 68.f, PY_STATE = 78.5f;
static const NVGcolor LANE_COL[NT] = {
	nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0xE8, 0xC5, 0x47), nvgRGB(0x4C, 0xD6, 0xE0),
	nvgRGB(0xB3, 0x8C, 0xF0), nvgRGB(0x7C, 0xD3, 0x6B), nvgRGB(0xF0, 0x6E, 0xA8)};
static const NVGcolor POS_COL = nvgRGB(0x7C, 0xD3, 0x6B);
static const NVGcolor NEG_COL = nvgRGB(0xE0, 0x50, 0x3A);
static const NVGcolor CELL_BG = nvgRGB(0x24, 0x24, 0x2B);
static const NVGcolor FRAME = nvgRGB(0x16, 0x16, 0x1B);
} // namespace ilp

using namespace ilp;

struct Interlock : Module {
	enum ParamId {
		AMOUNT_PARAM,
		MEMORY_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		AMOUNT_INPUT,
		ENUMS(TRIG_INPUT, NT),
		TRIG_POLY_INPUT,
		ACT_INPUT,
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(TRIG_OUTPUT, NT),
		TRIG_POLY_OUTPUT,
		ACT_OUTPUT,
		STATE_OUTPUT,
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	inf::Engine engine;
	fm::Rng uiRng;
	dsp::SchmittTrigger clockTrigger, resetTrigger, inTrigger[NT], polyTrigger[NT];
	float flash[NT] = {};
	uint8_t lastFired = 0;

	Interlock() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(AMOUNT_PARAM, 0.f, 1.f, 1.f, "Amount (scales the whole grid)", "%", 0.f, 100.f);
		configSwitch(MEMORY_PARAM, 0.f, inf::MAX_MEM, 0.f, "Memory (extra steps an influence lasts, fading)",
			{"0 (next step only)", "1", "2", "3", "4"});
		configInput(CLOCK_INPUT, "Clock (each pulse starts a step; unpatched, any input trigger does)");
		configInput(RESET_INPUT, "Reset (clears the memory)");
		configInput(AMOUNT_INPUT, "Amount CV (0 to 10 V adds 0 to 100%)");
		for (int y = 0; y < NT; y++) {
			configInput(TRIG_INPUT + y, string::f("Track %c trigger", 'A' + y));
			configOutput(TRIG_OUTPUT + y, string::f("Track %c trigger", 'A' + y));
		}
		configInput(TRIG_POLY_INPUT, "Triggers A to F (poly). A mono input above replaces its channel");
		configInput(ACT_INPUT, "Active tracks (poly: 10 V = playing, 0 V = muted). Unpatched: all playing");
		configOutput(TRIG_POLY_OUTPUT, "Triggers A to F (poly, 6 channels)");
		configOutput(ACT_OUTPUT, "Active tracks, passed through (poly, 6 channels)");
		configOutput(STATE_OUTPUT, "State (poly): the NOW meter of each track, A to F (+-5 V)");
		engine.rng.seed(random::u64());
		uiRng.seed(random::u64());
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.clearGrid();
		engine.reset();
	}

	int memoryValue() { return clamp((int) std::round(params[MEMORY_PARAM].getValue()), 0, inf::MAX_MEM); }

	void process(const ProcessArgs& args) override {
		engine.amount = clamp(params[AMOUNT_PARAM].getValue() + inputs[AMOUNT_INPUT].getVoltage() / 10.f, 0.f, 1.f);
		engine.memory = memoryValue();

		bool clockEdge = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		bool resetEdge = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		// which tracks play: missing poly channels don't exist, ACT mutes the rest
		bool polyIn = inputs[TRIG_POLY_INPUT].isConnected();
		int polyCh = inputs[TRIG_POLY_INPUT].getChannels();
		int actCh = inputs[ACT_INPUT].getChannels();
		uint8_t active = 0;
		float act[NT];
		for (int y = 0; y < NT; y++) {
			bool exists = !polyIn || y < polyCh || inputs[TRIG_INPUT + y].isConnected();
			bool on = y >= actCh || inputs[ACT_INPUT].getVoltage(y) >= 1.f;
			act[y] = on ? 10.f : 0.f;
			if (exists && on) active |= 1 << y;
		}
		engine.active = active;

		bool in[NT], any = false;
		for (int y = 0; y < NT; y++) {
			// a mono jack replaces its poly channel
			float v = inputs[TRIG_INPUT + y].isConnected() ? inputs[TRIG_INPUT + y].getVoltage()
				: (y < polyCh ? inputs[TRIG_POLY_INPUT].getVoltage(y) : 0.f);
			in[y] = inTrigger[y].process(v, 0.1f, 1.f);
			any = any || in[y];
		}
		// no clock patched: the first input trigger of a step starts it
		if (!inputs[CLOCK_INPUT].isConnected() && any && (!engine.inStep || engine.ghostsDone))
			clockEdge = true;

		uint8_t before = engine.curMask;
		bool newStep = clockEdge;
		engine.process(args.sampleTime, clockEdge, resetEdge, in);

		uint8_t fresh = newStep ? engine.curMask : (uint8_t) (engine.curMask & ~before);
		outputs[TRIG_POLY_OUTPUT].setChannels(NT);
		outputs[ACT_OUTPUT].setChannels(NT);
		outputs[STATE_OUTPUT].setChannels(NT);
		for (int y = 0; y < NT; y++) {
			outputs[TRIG_OUTPUT + y].setVoltage(engine.out[y].high ? 10.f : 0.f);
			outputs[TRIG_POLY_OUTPUT].setVoltage(engine.out[y].high ? 10.f : 0.f, y);
			outputs[ACT_OUTPUT].setVoltage(act[y], y);
			outputs[STATE_OUTPUT].setVoltage(clamp(5.f * engine.delta[y], -5.f, 5.f), y);
			if (fresh & (1 << y)) flash[y] = 0.12f;
			else if (flash[y] > 0.f) flash[y] -= args.sampleTime;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* g = json_array();
		for (int i = 0; i < NT; i++)
			for (int j = 0; j < NT; j++) json_array_append_new(g, json_real(engine.grid[i][j]));
		json_object_set_new(root, "grid", g);
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* g = json_object_get(root, "grid"))
			for (int i = 0; i < NT * NT && i < (int) json_array_size(g); i++)
				engine.grid[i / NT][i % NT] = clamp((float) json_number_value(json_array_get(g, i)), -1.f, 1.f);
	}
};

static std::shared_ptr<window::Font> ilFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

static void ilFrame(const Widget::DrawArgs& args, Vec size) {
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg, -mm2px(0.6f), -mm2px(0.6f), size.x + mm2px(1.2f), size.y + mm2px(1.2f), mm2px(1.2f));
	nvgFillColor(args.vg, FRAME);
	nvgFill(args.vg);
}

// A bipolar bar from the cell's middle: up (green) raises, down (red) lowers.
static void bipolarCell(const Widget::DrawArgs& args, float x, float y, float w, float h, float v, bool label) {
	nvgBeginPath(args.vg);
	nvgRect(args.vg, x, y, w, h);
	nvgFillColor(args.vg, CELL_BG);
	nvgFill(args.vg);
	nvgBeginPath(args.vg);
	nvgRect(args.vg, x, y + h / 2 - mm2px(0.1f), w, mm2px(0.2f));
	nvgFillColor(args.vg, nvgRGB(0x44, 0x44, 0x4E));
	nvgFill(args.vg);
	float bh = std::fabs(v) * h / 2;
	if (bh > 0.f) {
		nvgBeginPath(args.vg);
		nvgRect(args.vg, x, v > 0.f ? y + h / 2 - bh : y + h / 2, w, bh);
		nvgFillColor(args.vg, nvgTransRGBAf(v > 0.f ? POS_COL : NEG_COL, 0.85f));
		nvgFill(args.vg);
	}
	if (label && std::fabs(v) >= 0.005f) {
		nvgFontSize(args.vg, mm2px(2.2f));
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgFillColor(args.vg, nvgRGB(0xF4, 0xF0, 0xE8));
		float ty = v > 0.f ? y + h - mm2px(1.7f) : y + mm2px(1.7f);  // on the empty half
		nvgText(args.vg, x + w / 2, ty, string::f("%+d", (int) std::round(100.f * v)).c_str(), NULL);
	}
}

// ---- the 6x6 influence grid: drag up or down, double-click to clear a cell ----
struct InfluenceGrid : OpaqueWidget {
	Interlock* module = nullptr;
	int dragRow = -1, dragCol = -1;
	float dragVal = 0.f;

	void onButton(const ButtonEvent& e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			dragCol = clamp((int) (e.pos.x / (box.size.x / NT)), 0, NT - 1);
			dragRow = clamp((int) (e.pos.y / (box.size.y / NT)), 0, NT - 1);
			if (module) dragVal = module->engine.grid[dragRow][dragCol];
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void onDragMove(const DragMoveEvent& e) override {
		if (!module || dragRow < 0 || e.button != GLFW_MOUSE_BUTTON_LEFT) return;
		float dy = e.mouseDelta.y / getAbsoluteZoom();
		float range = mm2px(20.f);  // 20 mm of drag = 100
		if (APP->window->getMods() & GLFW_MOD_SHIFT) range *= 5.f;
		dragVal = clamp(dragVal - dy / range, -1.f, 1.f);
		// a little stickiness at zero so it's easy to land on
		module->engine.grid[dragRow][dragCol] = std::fabs(dragVal) < 0.03f ? 0.f : dragVal;
	}

	void onDoubleClick(const DoubleClickEvent& e) override {
		if (!module || dragRow < 0) return;
		module->engine.grid[dragRow][dragCol] = 0.f;
		e.consume(this);
	}

	void draw(const DrawArgs& args) override {
		ilFrame(args, box.size);
		OpaqueWidget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			std::shared_ptr<window::Font> font = ilFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			static inf::Engine preview;
			static bool init = false;
			if (!init) { preview.example(); init = true; }
			const inf::Engine& en = module ? module->engine : preview;
			const float c = box.size.x / NT, pad = mm2px(0.35f);
			for (int i = 0; i < NT; i++) {
				bool lit = module && module->flash[i] > 0.f;
				for (int j = 0; j < NT; j++) {
					float x = j * c + pad, y = i * c + pad, s = c - 2 * pad;
					bipolarCell(args, x, y, s, s, en.grid[i][j], true);
					// a row outlines briefly when its track fires
					if (lit) {
						nvgBeginPath(args.vg);
						nvgRect(args.vg, x, y, s, s);
						nvgStrokeColor(args.vg, LANE_COL[i]);
						nvgStrokeWidth(args.vg, mm2px(0.3f));
						nvgStroke(args.vg);
					}
				}
			}
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

// ---- "next step" meters: how much each track's chance is being pushed right now ----
struct NextMeters : Widget {
	Interlock* module = nullptr;

	void draw(const DrawArgs& args) override {
		ilFrame(args, box.size);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) {
			const float c = box.size.x / NT, pad = mm2px(0.35f);
			for (int j = 0; j < NT; j++)
				bipolarCell(args, j * c + pad, pad, c - 2 * pad, box.size.y - 2 * pad, clamp(module->engine.delta[j], -1.f, 1.f), false);
		}
		else if (layer == 1) {
			const float c = box.size.x / NT, pad = mm2px(0.35f);
			for (int j = 0; j < NT; j++)
				bipolarCell(args, j * c + pad, pad, c - 2 * pad, box.size.y - 2 * pad, 0.f, false);
		}
		Widget::drawLayer(args, layer);
	}
};

struct MemoryReadout : Widget {
	Interlock* module = nullptr;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.8f));
		nvgFillColor(args.vg, FRAME);
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int m = module ? module->memoryValue() : 0;
			std::shared_ptr<window::Font> font = ilFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(2.8f));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, nvgRGB(0xF0, 0xC0, 0x40));
			nvgText(args.vg, box.size.x / 2, box.size.y / 2 + mm2px(0.1f), string::f("+%d", m).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

struct InterlockWidget : ModuleWidget {
	InterlockWidget(Interlock* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Interlock.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		InfluenceGrid* g = createWidget<InfluenceGrid>(mm2px(Vec(GX0, GY0)));
		g->box.size = mm2px(Vec(NT * GC, NT * GC));
		g->module = module;
		addChild(g);

		NextMeters* nm = createWidget<NextMeters>(mm2px(Vec(GX0, NEXT_Y)));
		nm->box.size = mm2px(Vec(NT * GC, NEXT_H));
		nm->module = module;
		addChild(nm);

		using I = Interlock;
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, Y_CTRL)), module, I::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, Y_CTRL)), module, I::RESET_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_AMT, Y_CTRL)), module, I::AMOUNT_PARAM));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_AMT_CV, Y_CTRL)), module, I::AMOUNT_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_MEM, Y_CTRL)), module, I::MEMORY_PARAM));
		MemoryReadout* mr = createWidget<MemoryReadout>(mm2px(Vec(MEMTXT_X, Y_CTRL - MEMTXT_H / 2)));
		mr->box.size = mm2px(Vec(MEMTXT_W, MEMTXT_H));
		mr->module = module;
		addChild(mr);

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PX, PY_TRIG_IN)), module, I::TRIG_POLY_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(PX, PY_ACT_IN)), module, I::ACT_INPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(PX, PY_TRIG_OUT)), module, I::TRIG_POLY_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(PX, PY_ACT_OUT)), module, I::ACT_OUTPUT));
		addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(PX, PY_STATE)), module, I::STATE_OUTPUT));

		for (int y = 0; y < NT; y++) {
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(JX0 + y * JDX, Y_IN)), module, I::TRIG_INPUT + y));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(JX0 + y * JDX, Y_OUT)), module, I::TRIG_OUTPUT + y));
		}
	}

	void appendContextMenu(Menu* menu) override {
		Interlock* module = getModule<Interlock>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Clear grid", "", [=]() { module->engine.clearGrid(); }));
		menu->addChild(createMenuItem("Load the guide's example", "", [=]() { module->engine.example(); }));
		menu->addChild(createMenuItem("Randomise", "", [=]() { module->engine.randomise(module->uiRng); }));
	}
};

Model* modelInterlock = createModel<Interlock, InterlockWidget>("Interlock");
