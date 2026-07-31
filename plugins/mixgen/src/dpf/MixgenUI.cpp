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
            "Automatic producer for T-Mix · routes eight musical gain lanes to CC 20–27",
            downspout::mixgen::kParameterSpecs.data(),
            downspout::mixgen::kParameterCount, 105, 196, 216) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::mixgen;
        beginPanel();

        drawSection(24, 102, 500, 192, "PATTERN", "when should channels lead or recede?");
        static constexpr const char* modes[] {"Random", "Quasi", "Euclidean"};
        drawChoice(kMode, 38, 137, 472, "Producer logic", modes, 3,
                   "Random repeats a seeded pattern; Quasi reduces clumps; Euclidean distributes activity evenly.");
        drawBeatSlider(kRate, 38, 197, 220, "Step rate",
                       "Time between producer decisions, in quarter-note beats.");
        drawSlider(kSteps, 270, 197, 112, "Length", " steps",
                   "Number of decisions before the pattern repeats.", 0);
        drawSlider(kSeed, 394, 197, 116, "Seed", "",
                   "Change this to generate a different repeatable arrangement.", 0);

        drawSection(540, 102, 516, 192, "MIX SHAPE", "how strongly should it produce?");
        drawPercentSlider(kDensity, 554, 137, 238, "Active density",
                          "How often channels occupy the foreground.");
        drawPercentSlider(kDepth, 804, 137, 238, "Depth",
                          "How far background channels are turned down.");
        drawPercentSlider(kVariation, 554, 197, 238, "Accent variation",
                          "Humanize the level of active channels.");
        drawPercentSlider(kSpread, 804, 197, 238, "Lane spread",
                          "Offset decisions across channels instead of moving them together.");

        drawSection(24, 310, 1032, 84, "CONNECTION", "send Mixgen MIDI to the T-Mix track");
        drawToggle(kEnabled, 38, 342, 206, "Producer enabled",
                   "Turning this off sends unity to all eight channels.");
        drawChannelSlider(kMidiChannel, 258, 339, 188, "MIDI output",
                          "T-Mix accepts the contract on any MIDI channel.");
        drawReadout(460, 337, 268, 44, "FIXED DESTINATION", "T-Mix channels 1–8");
        drawReadout(742, 337, 300, 44, "MIDI CONTRACT", "CC 20 21 22 23 24 25 26 27");

        drawSection(24, 410, 1032, 270, "PRODUCER BOARD",
                    "pattern preview · blue bar is the live gain sent to T-Mix");
        drawBoard(38, 447, 1004, 216);
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
                roundedRect(cx, rowY + 4.0f, cellWidth, rowHeight - 8.0f, 3.0f);
                fill();
                beginPath();
                fillColor(105, 196, 216, 225);
                rect(cx + 2.0f, rowY + rowHeight - 6.0f - gain * (rowHeight - 12.0f),
                     std::max(1.0f, cellWidth - 4.0f), gain * (rowHeight - 12.0f));
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
