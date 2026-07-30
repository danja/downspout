#include "generative_panel_ui.hpp"
#include "harmonic_atlas_core.hpp"
START_NAMESPACE_DISTRHO
class HarmonicAtlasUI : public GenerativePanelUI {
public:
    HarmonicAtlasUI() : GenerativePanelUI(
        "Harmonic Atlas", "autonomous tonal, modal, chromatic-mediant and neo-Riemannian movement",
        downspout::harmonic_atlas::kParameterSpecs.data(),
        downspout::harmonic_atlas::kParameterCount, 104, 188, 206) {}
};
UI* createUI() { return new HarmonicAtlasUI(); }
END_NAMESPACE_DISTRHO
