#include "generative_panel_ui.hpp"
#include "mnemosyne_core.hpp"

#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

class MnemosyneUI : public GenerativePanelUI {
public:
    MnemosyneUI()
        : GenerativePanelUI(
            "Mnemosyne",
            "Listen for a phrase, preserve it in the reservoir, then accompany or recall it",
            downspout::mnemosyne::kParameterSpecs.data(),
            downspout::mnemosyne::kParameterCount, 176, 132, 202) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::mnemosyne;
        static constexpr const char* modes[] {"Listen", "Accompany", "Autonomous"};
        static constexpr const char* transforms[] {"Original", "Transp.", "Invert", "Rotate", "Stretch", "Remix"};

        beginPanel();

        drawSection(24, 104, 912, 147, "MEMORY RESERVOIR", "eight phrases · sixty-four note records each");
        const int phraseCount = std::clamp(static_cast<int>(std::lround(value(kStatusPhrases))), 0, 8);
        const int captured = std::max(0, static_cast<int>(std::lround(value(kStatusEvents))));
        for (int i = 0; i < 8; ++i) {
            const float x = 38.0f + i * 108.0f;
            beginPath();
            fillColor(i < phraseCount ? accentR() : 41, i < phraseCount ? accentG() : 48,
                      i < phraseCount ? accentB() : 55, 255);
            roundedRect(x, 142, 96, 48, 6);
            fill();
            fillColor(i < phraseCount ? 18 : 145, i < phraseCount ? 21 : 154,
                      i < phraseCount ? 24 : 160, 255);
            fontSize(11);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            char slot[20] {};
            std::snprintf(slot, sizeof(slot), "Phrase %d", i + 1);
            text(x + 48, 166, slot, nullptr);
        }
        char status[96] {};
        if (phraseCount == 0)
            std::snprintf(status, sizeof(status), "Play MIDI while transport runs to capture the first phrase.");
        else
            std::snprintf(status, sizeof(status), "%d phrases stored · %d events in the phrase being captured", phraseCount, captured);
        fillColor(153, 164, 172, 255);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(38, 204, status, nullptr);
        drawAction(1, 814, 199, 108, 28, "Clear memory", "Clear all captured phrases.", true);

        drawSection(24, 263, 576, 294, "CAPTURE & RECALL", "primary musical behavior");
        drawChoice(kMode, 38, 296, 548, "Operating mode", modes, 3,
                   "Listen captures only; Accompany passes input and adds recall; Autonomous recalls without input.");
        drawSlider(kPhraseBars, 38, 355, 264, "Phrase length", " bars", "Capture and recall window.", 0);
        drawToggle(kPassInput, 310, 355, 276, "Pass incoming MIDI", "Forward the original performance.");
        drawPercentSlider(kNovelty, 38, 411, 264, "Novelty", "Probability of introducing transformed material.");
        drawPercentSlider(kContinuity, 310, 411, 276, "Continuity", "Preference for preserving motif identity.");
        drawPercentSlider(kRhythmFidelity, 38, 467, 548, "Rhythmic fidelity", "How strictly recalled events keep their original timing.");

        drawSection(614, 263, 322, 294, "TRANSFORMATION", "shape recalled material");
        drawChoice(kTransform, 628, 296, 294, "Transform", transforms, 6,
                   "Primary transformation used during recall.");
        drawNoteSlider(kRegister, 628, 355, 294, "Target register", "Centre note for transformed material.");
        drawChannelSlider(kChannel, 628, 411, 143, "Output", "Generated MIDI channel.");
        drawSlider(kSeed, 779, 411, 143, "Seed", "", "Deterministic recall identity.", 0);
        drawReadout(628, 467, 294, 58, "Capture activity",
                    captured > 0 ? "Listening · notes received" : "Waiting for MIDI");

        endPanel();
    }

    void actionTriggered(const int action) override
    {
        if (action == 1)
            setState("reservoir", "version=1\ncount=0\n");
    }
};

UI* createUI() { return new MnemosyneUI(); }

END_NAMESPACE_DISTRHO
