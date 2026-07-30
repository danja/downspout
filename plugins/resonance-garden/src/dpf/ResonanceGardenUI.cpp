#include "generative_panel_ui.hpp"
#include "resonance_garden_core.hpp"
START_NAMESPACE_DISTRHO
class ResonanceGardenUI:public GenerativePanelUI{public:ResonanceGardenUI():GenerativePanelUI("Resonance Garden","MIDI-tuned damped resonators with internal-scale fallback and freeze",downspout::resonance_garden::kParameterSpecs.data(),downspout::resonance_garden::kParameterCount,102,190,126){}};UI*createUI(){return new ResonanceGardenUI();}END_NAMESPACE_DISTRHO
