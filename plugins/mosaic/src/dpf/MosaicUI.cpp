#include "generative_panel_ui.hpp"
#include "mosaic_core.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

class MosaicUI : public GenerativePanelUI {
public:
    MosaicUI()
        : GenerativePanelUI(
            "Mosaic",
            "Load up to four WAV files, then shape deterministic slices, grains, and layers",
            downspout::mosaic::kParameterSpecs.data(),
            downspout::mosaic::kParameterCount, 220, 166, 84) {}

private:
    static constexpr std::array<const char*, 4> kStateKeys {
        "sample_1", "sample_2", "sample_3", "sample_4"
    };

    void stateChanged(const char* key, const char* stateValue) override
    {
        for (std::size_t i = 0; i < kStateKeys.size(); ++i) {
            if (std::strcmp(key, kStateKeys[i]) == 0) {
                paths_[i] = stateValue != nullptr ? stateValue : "";
                repaint();
                return;
            }
        }
    }

    void onNanoDisplay() override
    {
        using namespace downspout::mosaic;
        static constexpr const char* modes[] {"MIDI", "Autonomous", "Both"};
        beginPanel();

        drawSection(24, 104, 912, 154, "SAMPLE POOL", "WAV PCM16 / float32 · loaded outside audio processing");
        for (int i = 0; i < 4; ++i)
            drawSampleSlot(i, 38.0f + i * 221.0f, 137.0f, 207.0f, 101.0f);

        drawSection(24, 270, 430, 361, "PLAYBACK", "triggering and variation");
        drawChoice(kMode, 38, 303, 402, "Trigger source", modes, 3,
                   "Trigger from MIDI, transport-synchronised autonomous events, or both.");
        const bool autonomous = static_cast<int>(std::lround(value(kMode))) != 0;
        drawPercentSlider(kDensity, 38, 362, 194, "Autonomous density",
                          "Density of transport-synchronised autonomous triggers.", autonomous);
        drawSlider(kSeed, 240, 362, 200, "Variation seed", "", "Deterministic sample and slice choices.", 0);
        drawPercentSlider(kReverseChance, 38, 418, 194, "Reverse chance",
                          "Chance that a triggered region plays backward.");
        drawSlider(kPitchRange, 240, 418, 200, "Pitch range", " semitones",
                   "Maximum deterministic pitch variation.", 0);
        drawPercentSlider(kStereoSpread, 38, 474, 402, "Stereo spread",
                          "Spread simultaneous voices across the stereo field.");
        drawSlider(kAttack, 38, 530, 194, "Attack", " sec", "Transient-safe fade-in.", 3);
        drawSlider(kRelease, 240, 530, 200, "Release", " sec", "Transient-safe fade-out.", 3);

        drawSection(468, 270, 468, 361, "REGION SHAPE", "slice and grain relationship");
        drawRegionPreview(486, 307, 432, 165);
        drawPercentSlider(kSliceSize, 486, 488, 207, "Slice size",
                          "Fraction of a sample available to the triggered voice.");
        drawSlider(kGrainSize, 701, 488, 217, "Grain size", " sec",
                   "Duration of an individual grain.", 3);
        drawMeter(kStatusLoaded, 486, 544, 207, "Loaded samples", "", 0);
        drawMeter(kStatusVoices, 701, 544, 217, "Active voices", "", 0);
        drawLamp(486, 594, 432,
                 value(kStatusMissing) >= 0.5f ? "One or more sample slots are empty" : "Sample pool ready",
                 true, value(kStatusMissing) >= 0.5f);

        endPanel();
    }

    void drawSampleSlot(const int slot, const float x, const float y, const float w, const float h)
    {
        beginPath();
        fillColor(paths_[slot].empty() ? 34 : accentR(),
                  paths_[slot].empty() ? 41 : accentG(),
                  paths_[slot].empty() ? 48 : accentB(),
                  paths_[slot].empty() ? 255 : 85);
        roundedRect(x, y, w, h, 6);
        fill();
        char heading[20] {};
        std::snprintf(heading, sizeof(heading), "SLOT %d", slot + 1);
        fillColor(220, 226, 222, 255);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10, y + 8, heading, nullptr);
        const std::string display = paths_[slot].empty() ? "Empty - choose a WAV file" : basename(paths_[slot]);
        char shortened[32] {};
        std::snprintf(shortened, sizeof(shortened), "%.28s", display.c_str());
        fillColor(paths_[slot].empty() ? 130 : 205, paths_[slot].empty() ? 142 : 214,
                  paths_[slot].empty() ? 150 : 210, 255);
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10, y + 29, shortened, nullptr);
        drawAction(slot, x + 10, y + 61, paths_[slot].empty() ? w - 20 : (w - 28) * 0.62f,
                   28, paths_[slot].empty() ? "Load WAV" : "Replace",
                   "Choose a WAV file for this slot.");
        if (!paths_[slot].empty())
            drawAction(100 + slot, x + 18 + (w - 28) * 0.62f, y + 61,
                       (w - 28) * 0.38f, 28, "Clear", "Remove this sample from the pool.", true);
    }

    void drawRegionPreview(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::mosaic;
        beginPath();
        fillColor(18, 23, 28, 255);
        roundedRect(x, y, w, h, 6);
        fill();
        beginPath();
        strokeColor(100, 112, 121, 255);
        strokeWidth(1.2f);
        for (int i = 0; i <= 120; ++i) {
            const float t = static_cast<float>(i) / 120.0f;
            const float wave = std::sin(t * 38.0f) * std::sin(t * 4.7f)
                + 0.35f * std::sin(t * 83.0f);
            const float px = x + 10 + t * (w - 20);
            const float py = y + h * 0.5f - wave * h * 0.25f;
            if (i == 0)
                moveTo(px, py);
            else
                lineTo(px, py);
        }
        stroke();
        const float sliceW = std::clamp(value(kSliceSize), 0.01f, 1.0f) * (w - 20);
        beginPath();
        fillColor(accentR(), accentG(), accentB(), 42);
        roundedRect(x + 10, y + 12, sliceW, h - 24, 4);
        fill();
        beginPath();
        strokeColor(accentR(), accentG(), accentB(), 220);
        strokeWidth(2);
        roundedRect(x + 10, y + 12, sliceW, h - 24, 4);
        stroke();
        const float grain = std::clamp(value(kGrainSize) / 0.25f, 0.01f, 1.0f) * sliceW;
        beginPath();
        fillColor(238, 240, 234, 155);
        roundedRect(x + 10, y + h - 28, std::max(3.0f, grain), 7, 3);
        fill();
        fillColor(132, 144, 152, 255);
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 12, y + 10, "SELECTED SLICE", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(x + w - 12, y + h - 9, "grain", nullptr);
    }

    void actionTriggered(const int action) override
    {
        if (action >= 0 && action < 4) {
            requestStateFile(kStateKeys[static_cast<std::size_t>(action)]);
            return;
        }
        if (action >= 100 && action < 104) {
            const std::size_t slot = static_cast<std::size_t>(action - 100);
            paths_[slot].clear();
            setState(kStateKeys[slot], "");
            repaint();
        }
    }

    static std::string basename(const std::string& path)
    {
        const auto pos = path.find_last_of("/\\");
        return pos == std::string::npos ? path : path.substr(pos + 1);
    }

    std::array<std::string, 4> paths_ {};
};

constexpr std::array<const char*, 4> MosaicUI::kStateKeys;

UI* createUI() { return new MosaicUI(); }

END_NAMESPACE_DISTRHO
