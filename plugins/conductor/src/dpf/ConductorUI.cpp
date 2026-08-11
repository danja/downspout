#include "generative_panel_ui.hpp"
#include "conductor_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class ConductorUI : public GenerativePanelUI {
public:
    ConductorUI()
        : GenerativePanelUI(
            "Conductor",
            "Build a repeatable long-form arc; scene commands leave graph routing to the host",
            downspout::conductor::kParameterSpecs.data(),
            downspout::conductor::kParameterCount, 220, 154, 78) {}

private:
    int pendingSection_ = -1;

    void parameterChanged(uint32_t index, float v) override
    {
        GenerativePanelUI::parameterChanged(index, v);
        using namespace downspout::conductor;
        if (index == kStatusSection && pendingSection_ >= 0 &&
            static_cast<int>(std::lround(v)) == pendingSection_)
            pendingSection_ = -1;
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press) {
            const float x = static_cast<float>(ev.pos.getX());
            const float y = static_cast<float>(ev.pos.getY());
            using namespace downspout::conductor;
            const int current = std::clamp(static_cast<int>(std::lround(value(kStatusSection))), 0, 4);
            for (int i = 0; i < 5; ++i) {
                const float sx = 38.0f + i * 174.0f;
                if (x >= sx && x <= sx + 162.0f && y >= 140.0f && y <= 182.0f) {
                    if (i != current) {
                        pendingSection_ = i;
                        commitParameter(kPendingSection, static_cast<float>(i + 1));
                        repaint();
                    }
                    return true;
                }
            }
        }
        return handleMouse(ev);
    }

    void onNanoDisplay() override
    {
        using namespace downspout::conductor;
        static constexpr const char* modes[] {"Fixed form", "Weighted form"};
        static constexpr const char* sections[] {"Intro", "Development", "Break", "Reprise", "Coda"};

        beginPanel();
        drawSection(24, 104, 912, 112, "FORM POSITION", "click a section to jump at the next bar");
        const int current = std::clamp(static_cast<int>(std::lround(value(kStatusSection))), 0, 4);
        const int barsLeft = std::max(0, static_cast<int>(std::lround(value(kStatusBarsLeft))));
        for (int i = 0; i < 5; ++i) {
            const float sx = 38.0f + i * 174.0f;
            const bool isCurrent = (i == current);
            const bool isPending = (i == pendingSection_);
            beginPath();
            fillColor(isCurrent ? accentR() : isPending ? 200 : 42,
                      isCurrent ? accentG() : isPending ? 148 : 49,
                      isCurrent ? accentB() : isPending ? 36  : 56, 255);
            roundedRect(sx, 140, 162, 42, 5);
            fill();
            fillColor(isCurrent ? 20 : isPending ? 28 : 170,
                      isCurrent ? 23 : isPending ? 21 : 180,
                      isCurrent ? 25 : isPending ? 12 : 184, 255);
            fontSize(11);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(sx + 81, 161, sections[i], nullptr);
        }
        char remaining[64] {};
        if (pendingSection_ >= 0)
            std::snprintf(remaining, sizeof(remaining), "→ jumping to %s at next bar", sections[pendingSection_]);
        else
            std::snprintf(remaining, sizeof(remaining), "%s · %d bars remaining", sections[current], barsLeft);
        fillColor(205, 212, 208, 255);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(38, 190, remaining, nullptr);

        drawSection(24, 228, 574, 329, "STRUCTURE", "weighted form uses the section emphasis controls");
        drawChoice(kMode, 38, 260, 546, "Form generation", modes, 2,
                   "Fixed follows the five-section order; Weighted uses seeded section choices.");
        drawSlider(kMinBars, 38, 319, 264, "Minimum section", " bars", "Shortest section duration.", 0);
        drawSlider(kMaxBars, 310, 319, 274, "Maximum section", " bars", "Longest section duration.", 0);
        drawWeight(kIntroWeight, 38, 378, 264, "Intro");
        drawWeight(kDevelopWeight, 310, 378, 274, "Development");
        drawWeight(kBreakWeight, 38, 434, 264, "Break");
        drawWeight(kRepriseWeight, 310, 434, 274, "Reprise");
        drawWeight(kCodaWeight, 38, 490, 546, "Coda");

        drawSection(612, 228, 324, 329, "MIDI COMMANDS", "advanced routing");
        drawNoteSlider(kBaseNote, 626, 260, 143, "Section note", "Base note used for section commands.");
        drawChannelSlider(kChannel, 777, 260, 145, "Output", "MIDI command channel.");
        drawCcSlider(kSceneCc, 626, 316, 143, "Scene", "CC carrying the current scene number.");
        drawCcSlider(kDensityCc, 777, 316, 145, "Density", "CC carrying the section density.");
        drawCcSlider(kEnergyCc, 626, 372, 143, "Energy", "CC carrying the section energy.");
        drawCcSlider(kMutationCc, 777, 372, 145, "Mutation", "CC requesting generative mutation.");
        drawCcSlider(kResetCc, 626, 428, 296, "Reset", "CC requesting downstream reset.");
        drawSlider(kSeed, 626, 484, 296, "Form seed", "", "Deterministic form identity.", 0);

        endPanel();
    }

    void drawWeight(const std::uint32_t parameter,
                    const float x,
                    const float y,
                    const float w,
                    const char* label)
    {
        using namespace downspout::conductor;
        drawPercentSlider(parameter, x, y, w, label, "Relative chance of selecting this section.",
                          value(kMode) >= 0.5f);
    }
};

UI* createUI() { return new ConductorUI(); }

END_NAMESPACE_DISTRHO
