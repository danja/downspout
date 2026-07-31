#include "generative_panel_ui.hpp"
#include "mixgen_core.hpp"

#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

class MixgenUI : public GenerativePanelUI {
public:
    MixgenUI()
        : GenerativePanelUI(
            "Mixgen",
            "Producer Control Bus · T-Mix CC 20–27 · Loopdelay 30–31 · Lightverb 32–33",
            downspout::mixgen::kParameterSpecs.data(),
            downspout::mixgen::kParameterCount, 105, 196, 216) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::mixgen;
        beginPanel();

        drawSection(24, 102, 500, 160, "PATTERN", "when should channels lead or recede?");
        static constexpr const char* modes[] {"Random", "Quasi", "Euclidean"};
        drawChoice(kMode, 38, 137, 472, "Producer logic", modes, 3,
                   "Random repeats a seeded pattern; Quasi reduces clumps; Euclidean distributes activity evenly.");
        drawBeatSlider(kRate, 38, 197, 220, "Step rate",
                       "Time between producer decisions, in quarter-note beats.");
        drawSlider(kSteps, 270, 197, 112, "Length", " steps",
                   "Number of decisions before the pattern repeats.", 0);
        drawSlider(kSeed, 394, 197, 116, "Seed", "",
                   "Change this to generate a different repeatable arrangement.", 0);

        drawSection(540, 102, 516, 160, "MIX SHAPE", "how strongly should it produce?");
        drawPercentSlider(kDensity, 554, 137, 238, "Active density",
                          "How often channels occupy the foreground.");
        drawPercentSlider(kDepth, 804, 137, 238, "Depth",
                          "How far background channels are turned down.");
        drawPercentSlider(kVariation, 554, 197, 238, "Accent variation",
                          "Humanize the level of active channels.");
        drawPercentSlider(kSpread, 804, 197, 238, "Lane spread",
                          "Offset decisions across channels instead of moving them together.");

        drawSection(24, 278, 1032, 84, "BUS ROUTING", "one MIDI send can drive the complete serial chain");
        drawToggle(kEnabled, 38, 310, 176, "Producer enabled",
                   "Off sends CC 19 release and restores T-Mix unity overlays.");
        drawChannelSlider(kMidiChannel, 226, 307, 142, "Bus channel",
                          "Match the Control channel in each receiving plugin.");
        static constexpr const char* profiles[] {"T-Mix", "FX only", "Full bus"};
        drawChoice(kRoutingProfile, 380, 304, 382, "Factory routing", profiles, 3,
                   "T-Mix sends CC 20–27; FX sends four configurable macro lanes; Full sends both.");
        drawReadout(774, 307, 268, 44, "LIFECYCLE", "CC 19 · acquire / release");

        drawSection(24, 378, 1032, 152, "PRODUCER BOARD",
                    "pattern preview · blue bar is the live gain sent to T-Mix");
        drawBoard(38, 411, 1004, 104);

        drawSection(24, 546, 1032, 134, "FX MACRO ROUTER",
                    "select a lane, then choose its pattern source, CC destination, range, and polarity");
        static constexpr const char* fxLanes[] {"Delay time", "Delay FB", "Verb mix", "Verb space"};
        drawChoice(kFxEditLane, 38, 577, 300, "Edit macro", fxLanes, 4,
                   "Select which of the four effect-control lanes is being edited.");
        const int fx = std::clamp(static_cast<int>(std::lround(value(kFxEditLane))), 0, kFxLaneCount - 1);
        drawChannelSlider(kFxSourceBase + fx, 350, 577, 136, "Pattern source",
                          "Mix lane whose pattern drives this macro.");
        drawCcSlider(kFxCcBase + fx, 498, 577, 138, "Destination CC",
                     "Change this to address a third-party MIDI-controlled effect.");
        drawPercentSlider(kFxMinimumBase + fx, 648, 577, 124, "Minimum",
                          "Lowest normalized value emitted by this macro.");
        drawPercentSlider(kFxMaximumBase + fx, 784, 577, 124, "Maximum",
                          "Highest normalized value emitted by this macro.");
        drawToggle(kFxInvertBase + fx, 920, 580, 122, "Invert",
                   "Make effects rise when the selected mix lane recedes.");
        drawLamp(38, 637, 176, "BUS ACTIVE", value(kStatusBusActive) >= 0.5f);
        char live[32] {};
        std::snprintf(live, sizeof(live), "%s · %d%%", kFxLaneNames[fx],
                      static_cast<int>(std::lround(value(kStatusFxBase + fx) * 100.0f)));
        drawReadout(226, 634, 410, 38, "LIVE MACRO", live);
        drawReadout(648, 634, 394, 38, "DEFAULT FULL BUS", "CC 30 delay · 31 feedback · 32 mix · 33 space");
        endPanel();
    }

    void drawBoard(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::mixgen;
        std::array<float, kParameterCount> parameters {};
        for (std::uint32_t index = 0; index < kParameterCount; ++index)
            parameters[index] = value(index);
        const int length = static_cast<int>(std::lround(value(kSteps)));
        const int visible = std::min(length, 16);
        const int current = static_cast<int>(std::lround(value(kStatusStep))) % std::max(1, length);
        const float labelWidth = 56.0f;
        const float liveWidth = 104.0f;
        const float gridWidth = w - labelWidth - liveWidth - 20.0f;
        const float cellGap = 3.0f;
        const float cellWidth = (gridWidth - cellGap * (visible - 1)) / visible;
        const float rowHeight = h / kLaneCount;

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float rowY = y + lane * rowHeight;
            char label[16] {};
            std::snprintf(label, sizeof(label), "CH %d", lane + 1);
            fillColor(192, 204, 207, 255);
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(x, rowY + rowHeight * 0.5f, label, nullptr);
            for (int step = 0; step < visible; ++step) {
                const float gain = gainForStep(parameters, lane, step);
                const float cx = x + labelWidth + step * (cellWidth + cellGap);
                beginPath();
                fillColor(step == current ? 72 : 34, step == current ? 99 : 42,
                          step == current ? 108 : 49, 255);
                roundedRect(cx, rowY + 2.0f, cellWidth, rowHeight - 4.0f, 3.0f);
                fill();
                beginPath();
                fillColor(105, 196, 216, 225);
                rect(cx + 2.0f, rowY + rowHeight - 3.0f - gain * (rowHeight - 6.0f),
                     std::max(1.0f, cellWidth - 4.0f), gain * (rowHeight - 6.0f));
                fill();
            }
            const float liveX = x + labelWidth + gridWidth + 14.0f;
            const float liveGain = value(kStatusGainBase + lane);
            drawBar(liveX, rowY + rowHeight * 0.5f - 3.0f, liveWidth, liveGain);
        }
    }
};

UI* createUI() { return new MixgenUI(); }

END_NAMESPACE_DISTRHO
