#include "generative_panel_ui.hpp"
#include "drift_core.hpp"
START_NAMESPACE_DISTRHO
class DriftUI:public GenerativePanelUI{public:DriftUI():GenerativePanelUI("Drift","four bounded CC lanes: LFO, sample-and-hold, walk, chaos and follower",downspout::drift::kParameterSpecs.data(),downspout::drift::kParameterCount,117,190,134){}};UI*createUI(){return new DriftUI();}END_NAMESPACE_DISTRHO
