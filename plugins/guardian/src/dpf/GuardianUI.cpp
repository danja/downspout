#include "generative_panel_ui.hpp"
#include "guardian_core.hpp"
START_NAMESPACE_DISTRHO
class GuardianUI:public GenerativePanelUI{public:GuardianUI():GenerativePanelUI("Guardian","DC removal, look-ahead limiting, true-peak guard and latched faults",downspout::guardian::kParameterSpecs.data(),downspout::guardian::kParameterCount,230,112,94){}};UI*createUI(){return new GuardianUI();}END_NAMESPACE_DISTRHO
