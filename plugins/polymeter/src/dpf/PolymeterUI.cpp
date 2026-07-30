#include "generative_panel_ui.hpp"
#include "polymeter_core.hpp"
START_NAMESPACE_DISTRHO
class PolymeterUI:public GenerativePanelUI{public:PolymeterUI():GenerativePanelUI("Polymeter","four Euclidean lanes with coprime lengths, ratchets, accents and drift",downspout::polymeter::kParameterSpecs.data(),downspout::polymeter::kParameterCount,226,118,92){}};UI*createUI(){return new PolymeterUI();}END_NAMESPACE_DISTRHO
