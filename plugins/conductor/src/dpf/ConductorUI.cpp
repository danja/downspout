#include "generative_panel_ui.hpp"
#include "conductor_core.hpp"
START_NAMESPACE_DISTRHO
class ConductorUI:public GenerativePanelUI{public:ConductorUI():GenerativePanelUI("Conductor",
"bar-aligned intro, development, break, reprise and coda scene commands",
downspout::conductor::kParameterSpecs.data(),downspout::conductor::kParameterCount,220,154,78){}};
UI* createUI(){return new ConductorUI();}
END_NAMESPACE_DISTRHO
