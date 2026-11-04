#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelFrancoisMarkovian);
	p->addModel(modelInterlock);
	p->addModel(modelGovernor);
	p->addModel(modelRewrite);
}
