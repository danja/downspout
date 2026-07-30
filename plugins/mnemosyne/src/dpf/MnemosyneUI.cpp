#include "generative_panel_ui.hpp"
#include "mnemosyne_core.hpp"
START_NAMESPACE_DISTRHO
class MnemosyneUI:public GenerativePanelUI{public:MnemosyneUI():GenerativePanelUI("Mnemosyne","bounded motif memory: listen, accompany, transform and autonomous recall",downspout::mnemosyne::kParameterSpecs.data(),downspout::mnemosyne::kParameterCount,176,132,202){}};UI*createUI(){return new MnemosyneUI();}END_NAMESPACE_DISTRHO
