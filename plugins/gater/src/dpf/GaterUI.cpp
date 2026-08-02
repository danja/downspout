#include "DistrhoUI.hpp"

START_NAMESPACE_DISTRHO

class GaterUI : public UI
{
public:
    GaterUI() : UI(400, 200) {}

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
