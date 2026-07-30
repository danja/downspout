#include "generative_panel_ui.hpp"
#include "oracle_core.hpp"
START_NAMESPACE_DISTRHO
class OracleUI:public GenerativePanelUI{public:OracleUI():GenerativePanelUI("Oracle","preallocated audio/MIDI observation with guarded CC and note responses",downspout::oracle::kParameterSpecs.data(),downspout::oracle::kParameterCount,90,182,214){}};UI*createUI(){return new OracleUI();}END_NAMESPACE_DISTRHO
