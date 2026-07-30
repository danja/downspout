#include "generative_panel_ui.hpp"
#include "orbit_core.hpp"
START_NAMESPACE_DISTRHO
class OrbitUI:public GenerativePanelUI{public:OrbitUI():GenerativePanelUI("Orbit","seeded orbit, pendulum, random-walk and figure-eight stereo motion",downspout::orbit::kParameterSpecs.data(),downspout::orbit::kParameterCount,102,164,220){}};UI*createUI(){return new OrbitUI();}END_NAMESPACE_DISTRHO
