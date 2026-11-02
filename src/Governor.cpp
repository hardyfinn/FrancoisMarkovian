// Governor.cpp
// Panel, widgets and Rack glue for Governor, the feedback module that holds
// density and variety at a target. The logic is in GovernorEngine.hpp.
// Positions are in mm and must match res/make_panel_governor.py.
#include "plugin.hpp"
#include "GovernorEngine.hpp"

using gov::NT;

namespace gvp {
static const float GRAPH_X = 5.f, GRAPH_W = 81.44f, GRAPH_H = 16.f;
static const float GRAPH_Y[2] = {15.f, 46.f};
static const float ROW_Y[2] = {40.f, 71.f};
static const float X_TGT = 9.f, X_TGT_CV = 19.5f, X_POL = 30.f;
static const float OUT_X[5] = {44.f, 54.f, 64.f, 74.f, 84.f};  // CTRL, INV, ERR, over, under
static const float Y_GLOBAL = 85.f, Y_IN = 108.f;
static const float X_CLOCK = 9.f, X_RESET = 19.5f, X_WIN = 31.f, X_RESP = 43.f, X_WOB = 55.f;
static const float WINTXT_X = 63.f, WINTXT_W = 22.f, WINTXT_H = 4.4f;
static const float IN_X0 = 9.f, IN_DX = 11.f;
static const int WINDOWS[4] = {8, 16, 32, 64};
static const int GHIST = 128;
static const NVGcolor LOOP_COL[2] = {nvgRGB(0xF0, 0x7A, 0x3E), nvgRGB(0x4C, 0xD6, 0xE0)};
static const NVGcolor CTRL_COL = nvgRGB(0xF0, 0xC0, 0x40);
static const NVGcolor FRAME = nvgRGB(0x16, 0x16, 0x1B);
} // namespace gvp

using namespace gvp;

struct Governor : Module {
	enum ParamId {
		ENUMS(TARGET_PARAM, 2),
		ENUMS(POL_PARAM, 2),
		WINDOW_PARAM,
		RESPONSE_PARAM,
		WOBBLE_PARAM,
		PARAMS_LEN
	};
	enum InputId {
		CLOCK_INPUT,
		RESET_INPUT,
		ENUMS(TARGET_INPUT, 2),
		ENUMS(TRIG_INPUT, NT),
		INPUTS_LEN
	};
	enum OutputId {
		ENUMS(CTRL_OUTPUT, 2),
		ENUMS(INV_OUTPUT, 2),
		ENUMS(ERR_OUTPUT, 2),
		ENUMS(OVER_OUTPUT, 2),
		ENUMS(UNDER_OUTPUT, 2),
		OUTPUTS_LEN
	};
	enum LightId {
		LIGHTS_LEN
	};

	gov::Engine engine;
	dsp::SchmittTrigger clockTrigger, resetTrigger, inTrigger[NT];
	dsp::SlewLimiter slew[2];   // smooths the control steps between clocks
	dsp::ClockDivider settingsDivider;

	// graph history: measured, target, control for each loop
	float hMeas[2][GHIST] = {}, hTgt[2][GHIST] = {}, hCtrl[2][GHIST] = {};
	int hPos = 0;

	Governor() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const char* names[2] = {"Density", "Variety"};
		const float defTarget[2] = {0.4f, 0.3f};
		for (int k = 0; k < 2; k++) {
			std::string n = names[k];
			configParam(TARGET_PARAM + k, 0.f, 1.f, defTarget[k], n + " target", "%", 0.f, 100.f);
			configSwitch(POL_PARAM + k, 0.f, 1.f, 0.f, n + " polarity",
				{"+ (raising the control raises it)", "- (raising the control lowers it)"});
			configInput(TARGET_INPUT + k, n + " target CV (0 to 10 V adds 0 to 100%)");
			configOutput(CTRL_OUTPUT + k, n + " control (0 to 10 V)");
			configOutput(INV_OUTPUT + k, n + " control, inverted (10 to 0 V)");
			configOutput(ERR_OUTPUT + k, n + " error: target minus measured (+-5 V)");
		}
		configOutput(OVER_OUTPUT + 0, "Too busy (gate)");
		configOutput(UNDER_OUTPUT + 0, "Too sparse (gate)");
		configOutput(OVER_OUTPUT + 1, "Too varied (gate)");
		configOutput(UNDER_OUTPUT + 1, "Too repetitive (gate)");
		configSwitch(WINDOW_PARAM, 0.f, 3.f, 1.f, "Window (clocks measured)", {"8", "16", "32", "64"});
		configParam(RESPONSE_PARAM, 0.f, 1.f, 0.4f, "Response (fast to slow)", " clocks", 64.f, 4.f);
		configParam(WOBBLE_PARAM, 0.f, 1.f, 0.f, "Wobble (smooth to hunting)", "%", 0.f, 100.f);
		configInput(CLOCK_INPUT, "Clock (one pulse per step)");
		configInput(RESET_INPUT, "Reset (clears the measurement history)");
		for (int t = 0; t < NT; t++) configInput(TRIG_INPUT + t, string::f("Track %c trigger", 'A' + t));
		settingsDivider.setDivision(32);
		for (int k = 0; k < 2; k++) slew[k].setRiseFall(20.f, 20.f);  // full range in 50 ms
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		engine.reset();
		engine.dens.reset();
		engine.vari.reset();
	}

	int windowValue() { return WINDOWS[clamp((int) std::round(params[WINDOW_PARAM].getValue()), 0, 3)]; }

	void updateSettings() {
		engine.window = windowValue();
		engine.response = params[RESPONSE_PARAM].getValue();
		engine.wobble = params[WOBBLE_PARAM].getValue();
		gov::Loop* loops[2] = {&engine.dens, &engine.vari};
		for (int k = 0; k < 2; k++) {
			loops[k]->target = clamp(params[TARGET_PARAM + k].getValue() + inputs[TARGET_INPUT + k].getVoltage() / 10.f, 0.f, 1.f);
			loops[k]->invert = params[POL_PARAM + k].getValue() > 0.5f;
		}
		// only connected trigger inputs count (all 6 if none are patched yet)
		uint8_t m = 0;
		for (int t = 0; t < NT; t++) if (inputs[TRIG_INPUT + t].isConnected()) m |= 1 << t;
		engine.trackMask = m ? m : 0x3F;
	}

	void process(const ProcessArgs& args) override {
		if (settingsDivider.process()) updateSettings();

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) engine.reset();
		if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
			engine.clock();
			gov::Loop* loops[2] = {&engine.dens, &engine.vari};
			for (int k = 0; k < 2; k++) {
				hMeas[k][hPos] = loops[k]->measured;
				hTgt[k][hPos] = loops[k]->target;
				hCtrl[k][hPos] = loops[k]->control;
			}
			hPos = (hPos + 1) % GHIST;
		}
		for (int t = 0; t < NT; t++)
			if (inTrigger[t].process(inputs[TRIG_INPUT + t].getVoltage(), 0.1f, 1.f)) engine.trigger(t);

		gov::Loop* loops[2] = {&engine.dens, &engine.vari};
		for (int k = 0; k < 2; k++) {
			float c = slew[k].process(args.sampleTime, loops[k]->control);
			outputs[CTRL_OUTPUT + k].setVoltage(10.f * c);
			outputs[INV_OUTPUT + k].setVoltage(10.f * (1.f - c));
			outputs[ERR_OUTPUT + k].setVoltage(clamp(5.f * loops[k]->error, -5.f, 5.f));
			outputs[OVER_OUTPUT + k].setVoltage(loops[k]->over > 0 ? 10.f : 0.f);
			outputs[UNDER_OUTPUT + k].setVoltage(loops[k]->over < 0 ? 10.f : 0.f);
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "densControl", json_real(engine.dens.integral));
		json_object_set_new(root, "variControl", json_real(engine.vari.integral));
		return root;
	}

	void dataFromJson(json_t* root) override {
		if (json_t* d = json_object_get(root, "densControl"))
			engine.dens.integral = engine.dens.control = clamp((float) json_number_value(d), 0.f, 1.f);
		if (json_t* v = json_object_get(root, "variControl"))
			engine.vari.integral = engine.vari.control = clamp((float) json_number_value(v), 0.f, 1.f);
	}
};

static std::shared_ptr<window::Font> gvFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

// ---- scrolling graph: measured (coloured), target (white dashes), control (yellow) ----
struct LoopGraph : Widget {
	Governor* module = nullptr;
	int which = 0;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(1.2f));
		nvgFillColor(args.vg, FRAME);
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void line(const DrawArgs& args, const float* v, NVGcolor col, float width) {
		const float pad = mm2px(0.8f), w = box.size.x - 2 * pad, h = box.size.y - 2 * pad;
		nvgBeginPath(args.vg);
		for (int i = 0; i < GHIST; i++) {
			float val = clamp(v[(module->hPos + i) % GHIST], 0.f, 1.f);
			float x = pad + w * i / (GHIST - 1), y = pad + h * (1.f - val);
			if (i == 0) nvgMoveTo(args.vg, x, y);
			else nvgLineTo(args.vg, x, y);
		}
		nvgStrokeColor(args.vg, col);
		nvgStrokeWidth(args.vg, width);
		nvgStroke(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			const float pad = mm2px(0.8f), w = box.size.x - 2 * pad, h = box.size.y - 2 * pad;
			// grid lines at 25 / 50 / 75%
			for (int q = 1; q < 4; q++) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, pad, pad + h * q / 4.f, w, mm2px(0.12f));
				nvgFillColor(args.vg, nvgRGB(0x2C, 0x2C, 0x34));
				nvgFill(args.vg);
			}
			float meas = 0.f, tgt = which ? 0.3f : 0.4f;
			if (module) {
				line(args, module->hCtrl[which], nvgTransRGBAf(CTRL_COL, 0.7f), mm2px(0.25f));
				line(args, module->hTgt[which], nvgRGBA(0xF4, 0xF0, 0xE8, 0x90), mm2px(0.2f));
				line(args, module->hMeas[which], LOOP_COL[which], mm2px(0.45f));
				const gov::Loop& l = which ? module->engine.vari : module->engine.dens;
				meas = l.measured;
				tgt = l.target;
			}
			std::shared_ptr<window::Font> font = gvFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(2.3f));
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(args.vg, LOOP_COL[which]);
			nvgText(args.vg, pad + mm2px(0.5f), pad, string::f("%s %d%%", which ? "VARIETY" : "DENSITY", (int) std::round(100.f * meas)).c_str(), NULL);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgFillColor(args.vg, nvgRGB(0xF4, 0xF0, 0xE8));
			nvgText(args.vg, box.size.x - pad - mm2px(0.5f), pad, string::f("target %d%%", (int) std::round(100.f * tgt)).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

struct WindowReadout : Widget {
	Governor* module = nullptr;

	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.8f));
		nvgFillColor(args.vg, FRAME);
		nvgFill(args.vg);
		Widget::draw(args);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			int w = module ? module->windowValue() : 16;
			std::shared_ptr<window::Font> font = gvFont();
			if (font && font->handle >= 0) nvgFontFaceId(args.vg, font->handle);
			nvgFontSize(args.vg, mm2px(2.6f));
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, CTRL_COL);
			nvgText(args.vg, box.size.x / 2, box.size.y / 2 + mm2px(0.1f), string::f("%d steps", w).c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

struct GovernorWidget : ModuleWidget {
	GovernorWidget(Governor* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/Governor.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		using G = Governor;
		for (int k = 0; k < 2; k++) {
			LoopGraph* g = createWidget<LoopGraph>(mm2px(Vec(GRAPH_X, GRAPH_Y[k])));
			g->box.size = mm2px(Vec(GRAPH_W, GRAPH_H));
			g->module = module;
			g->which = k;
			addChild(g);

			float y = ROW_Y[k];
			addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_TGT, y)), module, G::TARGET_PARAM + k));
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_TGT_CV, y)), module, G::TARGET_INPUT + k));
			addParam(createParamCentered<CKSS>(mm2px(Vec(X_POL, y)), module, G::POL_PARAM + k));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[0], y)), module, G::CTRL_OUTPUT + k));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[1], y)), module, G::INV_OUTPUT + k));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[2], y)), module, G::ERR_OUTPUT + k));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[3], y)), module, G::OVER_OUTPUT + k));
			addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(OUT_X[4], y)), module, G::UNDER_OUTPUT + k));
		}

		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_CLOCK, Y_GLOBAL)), module, G::CLOCK_INPUT));
		addInput(createInputCentered<PJ301MPort>(mm2px(Vec(X_RESET, Y_GLOBAL)), module, G::RESET_INPUT));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_WIN, Y_GLOBAL)), module, G::WINDOW_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_RESP, Y_GLOBAL)), module, G::RESPONSE_PARAM));
		addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(X_WOB, Y_GLOBAL)), module, G::WOBBLE_PARAM));
		WindowReadout* wr = createWidget<WindowReadout>(mm2px(Vec(WINTXT_X, Y_GLOBAL - WINTXT_H / 2)));
		wr->box.size = mm2px(Vec(WINTXT_W, WINTXT_H));
		wr->module = module;
		addChild(wr);

		for (int t = 0; t < NT; t++)
			addInput(createInputCentered<PJ301MPort>(mm2px(Vec(IN_X0 + t * IN_DX, Y_IN)), module, G::TRIG_INPUT + t));
	}

	void appendContextMenu(Menu* menu) override {
		Governor* module = getModule<Governor>();
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Centre both controls (5 V)", "", [=]() {
			module->engine.dens.reset();
			module->engine.vari.reset();
		}));
	}
};

Model* modelGovernor = createModel<Governor, GovernorWidget>("Governor");
