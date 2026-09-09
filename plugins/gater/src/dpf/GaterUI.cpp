#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

class GaterUI : public UI
{
public:
    GaterUI() : UI(400, 200) {}

private:
    const laf::Theme& t_ { laf::defaultTheme() };
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

protected:
    void onDisplay() override
    {
        // For NanoVG, this will be handled in onNanoDisplay
    }
    
    void onNanoDisplay() override
    {
        // Simple UI drawing
        // This is a placeholder; real implementation would use NanoVG
    }
};

UI* createUI() { return new GaterUI(); }

END_NAMESPACE_DISTRHO
