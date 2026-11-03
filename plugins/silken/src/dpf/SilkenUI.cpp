#include "downspout/dpf/MagnetoKit.hpp"

#include "silken_core.hpp"
#include "silken_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::silken;

namespace {

constexpr kit::Accent kCleanAccent {120, 132, 96};     // sage
constexpr kit::Accent kEssAccent {198, 132, 58};       // amber
constexpr kit::Accent kPolishAccent {150, 120, 170};   // lilac
constexpr kit::Accent kMeterAccent {92, 140, 156};     // steel

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

class SilkenUI : public MagnetoUI
{
public:
    SilkenUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no audio: show a sibilant and a plosive being caught.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(core::kStatusEss, 6.5f);
            setValue(core::kStatusPlosive, 9.0f);
            setValue(core::kStatusDetector, -18.0f);
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
        drawHeader("SILKEN", "vocal clean-up and polish",
                   "Rumble filter, plosive guard and de-esser clean a voice up; warmth, presence, air and sparkle polish it. Signal flows left to right; every stage is neutral when off.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float gap = 12.0f, y = 108.0f, h = 280.0f;
        const float wClean = 270.0f, wPolish = 250.0f, wEss = full - wClean - wPolish - 2.0f * gap;
        drawClean({24.0f, y, wClean, h});
        drawEss({24.0f + wClean + gap, y, wEss, h});
        drawPolish({24.0f + wClean + wEss + 2.0f * gap, y, wPolish, h});
        drawMeters({24.0f, y + h + 12.0f, full, 104.0f});
    }

private:
    void drawBar(const Rect b, const float fraction, const kit::Accent accent, const char* name, const char* shown, const float marker = -1.0f)
    {
        const auto& t = theme();
        label(b.x, b.y + 8.0f, name, false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + b.w, b.y + 8.0f, shown, true, 12.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        const float ty = b.y + 20.0f, th = 12.0f;
        beginPath();
        fc(t.border);
        rect(b.x, ty, b.w, th);
        fill();
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        rect(b.x, ty, b.w * std::clamp(fraction, 0.0f, 1.0f), th);
        fill();
        if (marker >= 0.0f) {
            beginPath();
            fc(t.textPrimary);
            rect(b.x + b.w * std::clamp(marker, 0.0f, 1.0f) - 1.0f, ty - 3.0f, 2.0f, th + 6.0f);
            fill();
        }
    }

    void drawClean(const Rect b)
    {
        drawPanel(b, "CLEAN-UP", kCleanAccent, "rumble and plosives");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        static constexpr const char* kOff[2] = {"Off", "On"};
        label(b.x + pad, b.y + 56.0f, "Rumble filter", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kHpfOn, kOff, 2, {b.x + pad + 110.0f, b.y + 44.0f, w - 110.0f, 24.0f}, kCleanAccent);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(value(core::kHpf)));
        setDim(intValue(core::kHpfOn) == 0);
        drawSlider(core::kHpf, "Frequency (12 dB/oct high-pass)", buf, {b.x + pad, b.y + 80.0f, w, 28.0f}, kCleanAccent);
        setDim(false);
        label(b.x + pad, b.y + 148.0f, "Plosive guard", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kPlosiveOn, kOff, 2, {b.x + pad + 110.0f, b.y + 136.0f, w - 110.0f, 24.0f}, kCleanAccent);
        std::snprintf(buf, sizeof(buf), "%.0f dB", static_cast<double>(value(core::kPlosiveThr)));
        setDim(intValue(core::kPlosiveOn) == 0);
        drawSlider(core::kPlosiveThr, "Threshold (low band, 150 Hz)", buf, {b.x + pad, b.y + 172.0f, w, 28.0f}, kCleanAccent);
        setDim(false);
        label(b.x + pad, b.y + b.h - 38.0f, "Breath bursts on p and b lose their", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + b.h - 22.0f, "thump; ordinary speech is left alone.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawEss(const Rect b)
    {
        const bool listen = intValue(core::kEssListen) != 0;
        drawPanel(b, "DE-ESSER", kEssAccent, listen ? "listening to the detector" : "tames s and t");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        static constexpr const char* kOff[2] = {"Off", "On"};
        static constexpr const char* kMode[2] = {"Band", "Wide"};
        static constexpr const char* kListen[2] = {"Off", "Listen"};
        label(b.x + pad, b.y + 56.0f, "De-esser", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kEssOn, kOff, 2, {b.x + pad + 70.0f, b.y + 44.0f, w - 70.0f, 24.0f}, kEssAccent);
        setDim(intValue(core::kEssOn) == 0);
        label(b.x + pad, b.y + 92.0f, "Cut", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kEssMode, kMode, 2, {b.x + pad + 70.0f, b.y + 80.0f, w - 70.0f, 24.0f}, kEssAccent);
        label(b.x + pad, b.y + 128.0f, "Monitor", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kEssListen, kListen, 2, {b.x + pad + 70.0f, b.y + 116.0f, w - 70.0f, 24.0f}, kEssAccent);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(value(core::kEssFreq)) / 1000.0);
        drawSlider(core::kEssFreq, "Frequency", buf, {b.x + pad, b.y + 152.0f, w, 28.0f}, kEssAccent);
        std::snprintf(buf, sizeof(buf), "%.0f dB", static_cast<double>(value(core::kEssThr)));
        drawSlider(core::kEssThr, "Threshold", buf, {b.x + pad, b.y + 192.0f, w, 28.0f}, kEssAccent);
        std::snprintf(buf, sizeof(buf), "%.0f dB", static_cast<double>(value(core::kEssRange)));
        drawSlider(core::kEssRange, "Range (most it will take off)", buf, {b.x + pad, b.y + 232.0f, w, 28.0f}, kEssAccent);
        setDim(false);
    }

    void drawPolish(const Rect b)
    {
        drawPanel(b, "POLISH", kPolishAccent, "tone and level");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        char buf[32];
        const struct { std::uint32_t id; const char* name; const char* format; bool signedValue; } rows[5] = {
            {core::kWarmth, "Warmth (low shelf, 200 Hz)", "%+.1f dB", true},
            {core::kPresence, "Presence (bell, 3.2 kHz)", "%+.1f dB", true},
            {core::kAir, "Air (high shelf, 10 kHz)", "%+.1f dB", true},
            {core::kSparkle, "Sparkle (new harmonics)", "%.0f %%", false},
            {core::kOutput, "Output", "%+.1f dB", true}};
        float y = b.y + 40.0f;
        for (const auto& r : rows) {
            const double v = static_cast<double>(value(r.id)) * (r.id == core::kSparkle ? 100.0 : 1.0);
            std::snprintf(buf, sizeof(buf), r.format, v);
            drawSlider(r.id, r.name, buf, {b.x + pad, y, w, 28.0f}, kPolishAccent);
            y += 46.0f;
        }
    }

    void drawMeters(const Rect b)
    {
        drawPanel(b, "WHAT IT IS DOING", kMeterAccent, "live");
        const float pad = 14.0f, gap = 20.0f;
        const float colW = (b.w - 2.0f * pad - 2.0f * gap) / 3.0f;
        char buf[40];
        const float det = value(core::kStatusDetector), thr = value(core::kEssThr);
        std::snprintf(buf, sizeof(buf), "%.0f dB (threshold %.0f)", static_cast<double>(det), static_cast<double>(thr));
        drawBar({b.x + pad, b.y + 40.0f, colW, 40.0f}, (det + 60.0f) / 60.0f, kEssAccent, "Sibilance level", buf, (thr + 60.0f) / 60.0f);
        std::snprintf(buf, sizeof(buf), "-%.1f dB", static_cast<double>(value(core::kStatusEss)));
        drawBar({b.x + pad + colW + gap, b.y + 40.0f, colW, 40.0f}, value(core::kStatusEss) / 24.0f, kEssAccent, "De-esser reduction", buf);
        std::snprintf(buf, sizeof(buf), "-%.1f dB", static_cast<double>(value(core::kStatusPlosive)));
        drawBar({b.x + pad + 2.0f * (colW + gap), b.y + 40.0f, colW, 40.0f}, value(core::kStatusPlosive) / 24.0f, kCleanAccent, "Plosive reduction", buf);
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilkenUI)
};

UI* createUI()
{
    return new SilkenUI();
}

END_NAMESPACE_DISTRHO
