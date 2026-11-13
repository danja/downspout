#include "downspout/dpf/MagnetoKit.hpp"

#include "chorale_core.hpp"
#include "chorale_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::chorale;

namespace {

constexpr kit::Accent kVoiceAccent {96, 150, 170};    // teal
constexpr kit::Accent kModeAccent {170, 118, 82};     // copper
constexpr kit::Accent kCustomAccent {150, 120, 170};  // lilac
constexpr kit::Accent kSoundAccent {120, 132, 96};    // sage

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::specs()[i];
            ranges[i] = {s.minimum, s.maximum, s.integer, false};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class ChoraleUI : public MagnetoUI
{
public:
    ChoraleUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no audio: show the sweeps part-way through their cycle.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(core::kStatusTap1, 0.22f);
            setValue(core::kStatusTap2, 0.71f);
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_ && core::isOutput(index)) return;   // keep the demo readings
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("CHORALE", "stereo chorus",
                   "Short delays that sweep up and down, opposite on left and right, so each side drifts against the other. Pick a Juno voicing or build your own.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawVoices({24.0f, 108.0f, full, 196.0f});
        const float gap = 12.0f, y = 316.0f, h = 224.0f;
        const float wMode = 276.0f, wCustom = 306.0f, wSound = full - wMode - wCustom - 2.0f * gap;
        drawMode({24.0f, y, wMode, h});
        drawCustom({24.0f + wMode + gap, y, wCustom, h});
        drawSound({24.0f + wMode + wCustom + 2.0f * gap, y, wSound, h});
    }

private:
    [[nodiscard]] int mode() { return std::clamp(intValue(core::kMode), 0, core::kModeCount - 1); }

    void drawVoices(const Rect b)
    {
        const int m = mode();
        const int voices = std::clamp(intValue(core::kVoices), 1, core::kMaxVoices);
        char caption[64];
        if (m < 3) std::snprintf(caption, sizeof(caption), "sweeps at 0.513 Hz and 0.863 Hz, 1.5 to 5.4 ms");
        else       std::snprintf(caption, sizeof(caption), "%.2f Hz, %.1f ms around %.1f ms", static_cast<double>(value(core::kStatusRate)),
                                 static_cast<double>(value(core::kStatusDepth)), static_cast<double>(value(core::kDelay)));
        drawPanel(b, "DELAY SWEEPS", kVoiceAccent, caption);

        const auto& t = theme();
        const float trackX = b.x + 124.0f, trackW = b.w - 124.0f - 28.0f;
        const char* juno[2] = {"Voice I", "Voice II"};
        const char* own[core::kMaxVoices] = {"Voice 1", "Voice 2", "Voice 3", "Voice 4"};
        const int lanes = m < 3 ? 2 : voices;
        const float spacing = std::min(44.0f, (b.h - 76.0f) / static_cast<float>(lanes));
        for (int v = 0; v < lanes; ++v) {
            const float cy = b.y + 56.0f + spacing * (static_cast<float>(v) + 0.5f);
            // In the Juno modes a lane is lit only when its voice is part of the mode.
            const bool active = m == 3 || m == 1 || (m == 0 && v == 0) || (m == 2 && v == 1);
            label(b.x + 16.0f, cy, m < 3 ? juno[v] : own[v], !active, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            beginPath();
            sc(t.border);
            strokeWidth(2.0f);
            moveTo(trackX, cy);
            lineTo(trackX + trackW, cy);
            stroke();
            const float pos = std::clamp(value(core::kStatusTap1 + static_cast<std::uint32_t>(v)), 0.0f, 1.0f);
            beginPath();
            fillColor(kVoiceAccent.r, kVoiceAccent.g, kVoiceAccent.b, active ? 255 : 60);
            circle(trackX + trackW * pos, cy, active ? 8.0f : 5.0f);
            fill();
        }
        label(trackX, b.y + b.h - 18.0f, "shorter delay", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(trackX + trackW, b.y + b.h - 18.0f, "longer delay", true, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
    }

    void drawMode(const Rect b)
    {
        const int m = mode();
        drawPanel(b, "VOICING", kModeAccent, m < 3 ? "fixed Juno sweeps" : "your own");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        static constexpr const char* kModes[4] = {"I", "I+II", "II", "Custom"};
        drawSegments(core::kMode, kModes, 4, {b.x + pad, b.y + 44.0f, w, 28.0f}, kModeAccent);
        static constexpr const char* kLines[4][3] = {
            {"I: the slow sweep alone.", "A gentle, single-voice shimmer.", "Delay 1.5 - 5.4 ms."},
            {"I+II: both sweeps together.", "The wide, lush ensemble sound,", "two voices drifting against each other."},
            {"II: the faster sweep alone.", "A quicker, more watery wobble.", "Delay 1.5 - 5.4 ms."},
            {"Custom: 1 to 4 voices at the", "Rate, Depth and Delay you set,", "spread round the cycle."}};
        for (int i = 0; i < 3; ++i) label(b.x + pad, b.y + 96.0f + static_cast<float>(i) * 18.0f, kLines[m][i], i > 0, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        setDim(m != 3);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%d voice%s", std::clamp(intValue(core::kVoices), 1, core::kMaxVoices), intValue(core::kVoices) == 1 ? "" : "s");
        drawStepper(core::kVoices, "Voices (Custom)", buf, {b.x + pad, b.y + 168.0f, w, 22.0f});
        setDim(false);
    }

    // The value in force when a controller has taken a control over, otherwise the parameter.
    [[nodiscard]] std::string shownValue(const std::uint32_t param, const std::uint32_t status, const char* format, const float scale = 1.0f)
    {
        const float now = value(status), set = value(param);
        char buf[48];
        std::snprintf(buf, sizeof(buf), format, static_cast<double>(now * scale));
        std::string s = buf;
        if (std::abs(now - set) > 1e-3f * std::max(1.0f, std::abs(set))) s += " (CC)";
        return s;
    }

    void drawCustom(const Rect b)
    {
        const bool on = mode() == 3;
        drawPanel(b, "CUSTOM", kCustomAccent, on ? "Rate, Depth and Delay" : "only in Custom mode");
        setDim(!on);
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        std::string s = shownValue(core::kRate, core::kStatusRate, "%.2f Hz");
        drawSlider(core::kRate, "Rate", s.c_str(), {b.x + pad, b.y + 44.0f, w, 28.0f}, kCustomAccent);
        s = shownValue(core::kDepth, core::kStatusDepth, "%.1f ms");
        drawSlider(core::kDepth, "Depth (sweep either side)", s.c_str(), {b.x + pad, b.y + 92.0f, w, 28.0f}, kCustomAccent);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(value(core::kDelay)));
        drawSlider(core::kDelay, "Delay (centre)", buf, {b.x + pad, b.y + 140.0f, w, 28.0f}, kCustomAccent);
        setDim(false);
        label(b.x + pad, b.y + b.h - 22.0f, "Depth is held below Delay.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawSound(const Rect b)
    {
        drawPanel(b, "SOUND", kSoundAccent, "all voicings");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f %%", static_cast<double>(value(core::kTone) * 100.0f));
        drawSlider(core::kTone, "Tone (filter opening)", buf, {b.x + pad, b.y + 38.0f, w, 28.0f}, kSoundAccent);
        std::string s = shownValue(core::kWidth, core::kStatusWidth, "%.0f %%", 100.0f);
        drawSlider(core::kWidth, "Width (0 = mono wet)", s.c_str(), {b.x + pad, b.y + 76.0f, w, 28.0f}, kSoundAccent);
        s = shownValue(core::kMix, core::kStatusMix, "%.0f %%", 100.0f);
        drawSlider(core::kMix, "Mix (dry to wet)", s.c_str(), {b.x + pad, b.y + 114.0f, w, 28.0f}, kSoundAccent);
        static constexpr const char* kInputs[2] = {"Mono sum", "Stereo"};
        label(b.x + pad, b.y + 162.0f, "Input", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kInput, kInputs, 2, {b.x + pad + 48.0f, b.y + 150.0f, w - 48.0f, 24.0f}, kSoundAccent);
        static constexpr const char* kSync[2] = {"Free", "BBT"};
        label(b.x + pad, b.y + 196.0f, "Sync", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kSync, kSync, 2, {b.x + pad + 48.0f, b.y + 184.0f, w - 48.0f, 24.0f}, kSoundAccent);
        const int ch = intValue(core::kCcChannel);
        if (ch == 0) std::snprintf(buf, sizeof(buf), "CC off");
        else         std::snprintf(buf, sizeof(buf), "ch %d: CC1-4", ch);
        drawStepper(core::kCcChannel, "CC channel (Rate Depth Width Mix)", buf, {b.x + pad, b.y + 220.0f, w, 22.0f});
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChoraleUI)
};

UI* createUI()
{
    return new ChoraleUI();
}

END_NAMESPACE_DISTRHO
