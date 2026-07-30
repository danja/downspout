#include "generative_panel_ui.hpp"
#include "mosaic_core.hpp"
START_NAMESPACE_DISTRHO
class MosaicUI:public GenerativePanelUI{public:MosaicUI():GenerativePanelUI("Mosaic","four-slot seeded slices, grains, reverse and layered variations",downspout::mosaic::kParameterSpecs.data(),downspout::mosaic::kParameterCount,220,166,84){}};UI*createUI(){return new MosaicUI();}END_NAMESPACE_DISTRHO
