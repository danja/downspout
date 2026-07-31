#include "generative_panel_ui.hpp"
#include "oracle_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class OracleUI : public GenerativePanelUI {
public:
    OracleUI()
        : GenerativePanelUI(
            "Oracle",
            "Listen first, then decide how measured features become safe MIDI responses",
            downspout::oracle::kParameterSpecs.data(),
            downspout::oracle::kParameterCount, 90, 182, 214) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::oracle;
        beginPanel();

        drawSection(24, 104, 426, 225, "LISTEN", "audio and MIDI observation");
        drawPercentSlider(kSmoothing, 38, 137, 194, "Feature smoothing",
                          "Smooth level, brightness, density, pitch, and onset measurements.");
        drawSlider(kOnsetThreshold, 240, 137, 196, "Onset threshold", "",
                   "Minimum transient strength treated as an onset.", 3);
        drawToggle(kPassInput, 38, 193, 398, "Pass incoming MIDI through",
                   "Forward observed MIDI alongside generated responses.");
        drawLamp(38, 243, 194, "Onset detected", value(kStatusOnset) >= 0.5f);
        drawLamp(240, 243, 196, "Input finite", value(kStatusFaults) < 0.5f,
                 value(kStatusFaults) >= 0.5f);

        drawSection(24, 341, 426, 290, "RESPOND", "constrained note generation");
        drawPercentSlider(kResponseChance, 38, 374, 194, "Response chance",
                          "Chance of producing a response after a qualified onset.");
        drawBeatSlider(kFeedbackGuard, 240, 374, 196, "Response guard",
                       "Minimum musical time between generated responses.");
        drawNoteSlider(kMinNote, 38, 430, 194, "Lowest note", "Lower response-note boundary.");
        drawNoteSlider(kMaxNote, 240, 430, 196, "Highest note", "Upper response-note boundary.");
        drawChannelSlider(kChannel, 38, 486, 194, "Response output", "Generated response channel.");
        drawSlider(kSeed, 240, 486, 196, "Seed", "", "Deterministic response identity.", 0);
        drawCcSlider(kLevelCc, 38, 542, 194, "Level destination", "CC receiving measured level.");
        drawCcSlider(kBrightnessCc, 240, 542, 196, "Brightness destination", "CC receiving measured brightness.");
        drawCcSlider(kDensityCc, 38, 598, 194, "Density destination", "CC receiving measured density.");
        drawCcSlider(kPitchCc, 240, 598, 196, "Pitch destination", "CC receiving measured pitch class.");

        drawSection(464, 104, 472, 527, "ANALYSIS", "live processor measurements");
        drawAnalysis(482, 139, 436, 192);
        drawMeter(kStatusLevel, 482, 350, 207, "Level");
        drawMeter(kStatusBrightness, 697, 350, 221, "Brightness");
        drawMeter(kStatusDensity, 482, 400, 207, "Density");
        drawMeter(kStatusOnset, 697, 400, 221, "Onset strength");
        const int pitch = static_cast<int>(std::lround(value(kStatusPitchClass)));
        drawReadout(482, 450, 207, 61, "Dominant pitch class", pitchClassName(pitch));
        char faults[32] {};
        std::snprintf(faults, sizeof(faults), "%d", static_cast<int>(std::lround(value(kStatusFaults))));
        drawReadout(697, 450, 221, 61, "Recovered non-finite samples", faults);
        drawPitchWheel(482, 529, 436, 78, pitch);

        endPanel();
    }

    void drawAnalysis(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::oracle;
        beginPath();
        fillColor(18, 23, 28, 255);
        roundedRect(x, y, w, h, 6);
        fill();
        const float level = std::clamp(value(kStatusLevel), 0.0f, 1.0f);
        const float brightness = std::clamp(value(kStatusBrightness), 0.0f, 1.0f);
        const float density = std::clamp(value(kStatusDensity), 0.0f, 1.0f);
        for (int i = 0; i < 24; ++i) {
            const float t = static_cast<float>(i) / 23.0f;
            const float shaped = level * (0.25f + 0.75f * std::exp(-std::abs(t - brightness) * (4.0f + 8.0f * density)));
            const float bh = 8.0f + shaped * (h - 26.0f);
            beginPath();
            fillColor(accentR(), accentG(), accentB(), 100 + static_cast<int>(120 * shaped));
            roundedRect(x + 9 + i * (w - 18) / 24.0f, y + h - 9 - bh,
                        (w - 26) / 24.0f, bh, 2);
            fill();
        }
        fillColor(130, 144, 153, 255);
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10, y + 8, "LOW", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(x + w - 10, y + 8, "BRIGHT", nullptr);
    }

    void drawPitchWheel(const float x, const float y, const float w, const float h, const int pitch)
    {
        const float cell = w / 12.0f;
        for (int i = 0; i < 12; ++i) {
            beginPath();
            fillColor(i == pitch ? accentR() : 37, i == pitch ? accentG() : 44,
                      i == pitch ? accentB() : 51, 255);
            roundedRect(x + i * cell + 1, y, cell - 2, h, 3);
            fill();
            fillColor(i == pitch ? 16 : 155, i == pitch ? 20 : 165,
                      i == pitch ? 23 : 170, 255);
            fontSize(9);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(x + (i + 0.5f) * cell, y + h * 0.5f, pitchClassName(i), nullptr);
        }
    }
};

UI* createUI() { return new OracleUI(); }

END_NAMESPACE_DISTRHO
