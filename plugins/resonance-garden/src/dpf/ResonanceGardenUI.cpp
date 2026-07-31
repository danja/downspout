#include "generative_panel_ui.hpp"
#include "resonance_garden_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class ResonanceGardenUI : public GenerativePanelUI {
public:
    ResonanceGardenUI()
        : GenerativePanelUI(
            "Resonance Garden",
            "Tune an eight-voice resonator bank from MIDI or an internal musical fallback",
            downspout::resonance_garden::kParameterSpecs.data(),
            downspout::resonance_garden::kParameterCount, 102, 190, 126) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::resonance_garden;
        static constexpr const char* scales[] {"Major", "Minor", "Dorian", "Penta"};
        beginPanel();

        drawSection(24, 104, 430, 226, "RESONANCE", "decay, colour, and stability");
        drawSlider(kDecay, 38, 137, 194, "Decay", " sec", "Resonator decay time.", 2);
        drawPercentSlider(kDamping, 240, 137, 200, "Damping", "Reduce high-frequency resonance.");
        drawPercentSlider(kInharmonicity, 38, 193, 194, "Inharmonicity", "Detune resonators away from exact harmonics.");
        drawPercentSlider(kFeedback, 240, 193, 200, "Resonance feedback", "Bounded feedback amount.");
        drawToggle(kFreeze, 38, 249, 402, "Freeze resonator energy", "Hold the current resonator energy.");

        drawSection(24, 342, 430, 289, "TUNING & OUTPUT", "MIDI notes override the fallback scale");
        drawPitchClassSlider(kRoot, 38, 375, 194, "Internal root", "Fallback scale root.");
        drawChoice(kScale, 240, 375, 200, "Internal scale", scales, 4,
                   "Fallback scale used when no MIDI notes are held.");
        drawSlider(kVoiceLimit, 38, 434, 194, "Voice limit", "", "Maximum simultaneous resonators.", 0);
        drawPercentSlider(kExcitation, 240, 434, 200, "Excitation", "How strongly input excites the resonators.");
        drawPercentSlider(kMix, 38, 490, 402, "Wet / dry", "Blend resonated and original audio.");
        drawMeter(kStatusPeak, 38, 546, 194, "Output peak");
        drawMeter(kStatusVoices, 240, 546, 200, "Active voices", "", 0);

        drawSection(468, 104, 468, 527, "RESONATOR BANK", "live voice energy and pitch map");
        drawGarden(487, 139, 430, 455);

        endPanel();
    }

    void drawGarden(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::resonance_garden;
        const int active = std::clamp(static_cast<int>(std::lround(value(kStatusVoices))), 0, 8);
        const float peak = std::clamp(value(kStatusPeak) / 1.5f, 0.0f, 1.0f);
        const int root = static_cast<int>(std::lround(value(kRoot)));
        for (int i = 0; i < 8; ++i) {
            const float angle = -1.35f + static_cast<float>(i) * 2.7f / 7.0f;
            const float cx = x + w * 0.5f + std::sin(angle) * w * 0.39f;
            const float cy = y + h * 0.78f - std::cos(angle) * h * 0.58f;
            const bool on = i < active;
            const float radius = 22.0f + (on ? 17.0f * peak : 0.0f);
            beginPath();
            fillColor(on ? accentR() : 43, on ? accentG() : 50,
                      on ? accentB() : 57, on ? 190 : 255);
            circle(cx, cy, radius);
            fill();
            beginPath();
            strokeColor(on ? 218 : 79, on ? 229 : 88, on ? 220 : 95, 190);
            strokeWidth(1.5f);
            circle(cx, cy, radius + 6);
            stroke();
            fillColor(on ? 18 : 130, on ? 22 : 140, on ? 24 : 147, 255);
            fontSize(11);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(cx, cy, pitchClassName(root + i), nullptr);
        }
        fillColor(130, 143, 151, 255);
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        text(x + w * 0.5f, y + h - 8, active > 0 ? "MIDI / fallback voices active" : "Waiting for audio excitation", nullptr);
    }
};

UI* createUI() { return new ResonanceGardenUI(); }

END_NAMESPACE_DISTRHO
