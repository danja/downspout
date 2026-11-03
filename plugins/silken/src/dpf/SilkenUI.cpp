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
constexpr kit::Accent kDynAccent {170, 118, 82};       // copper
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
            setValue(core::kStatusGate, 0.0f);
            setValue(core::kStatusLevel, 5.5f);
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
                   "A vocal strip in signal order: clean up (rumble, plosives, harshness), de-ess, even out (gate, leveller), then polish (EQ, drive, sparkle). Every stage is neutral when off.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float gap = 12.0f, y = 108.0f, h = 330.0f;
        const float w = (full - 3.0f * gap) / 4.0f;
        drawClean({24.0f, y, w, h});
        drawEss({24.0f + (w + gap), y, w, h});
        drawDynamics({24.0f + 2.0f * (w + gap), y, w, h});
        drawPolish({24.0f + 3.0f * (w + gap), y, w, h});
        drawMeters({24.0f, y + h + 12.0f, full, 90.0f});
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

    static void reductionText(char* buf, const std::size_t size, const float db)
    {
        if (db < 0.05f) std::snprintf(buf, size, "0.0 dB");
        else std::snprintf(buf, size, "-%.1f dB", static_cast<double>(db));
    }

    void slider(const std::uint32_t id, const char* name, const char* format, const float scale, const Rect r, const kit::Accent accent)
    {
        char buf[40];
        std::snprintf(buf, sizeof(buf), format, static_cast<double>(value(id)) * static_cast<double>(scale));
        drawSlider(id, name, buf, r, accent);
    }

    void drawClean(const Rect b)
    {
        drawPanel(b, "CLEAN-UP", kCleanAccent, "rumble, plosives, harshness");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        static constexpr const char* kOff[2] = {"Off", "On"};
        label(b.x + pad, b.y + 56.0f, "Rumble filter", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kHpfOn, kOff, 2, {b.x + pad + 110.0f, b.y + 44.0f, w - 110.0f, 24.0f}, kCleanAccent);
        setDim(intValue(core::kHpfOn) == 0);
        slider(core::kHpf, "Frequency (12 dB/oct high-pass)", "%.0f Hz", 1.0f, {b.x + pad, b.y + 76.0f, w, 28.0f}, kCleanAccent);
        setDim(false);
        label(b.x + pad, b.y + 128.0f, "Plosive guard", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kPlosiveOn, kOff, 2, {b.x + pad + 110.0f, b.y + 116.0f, w - 110.0f, 24.0f}, kCleanAccent);
        setDim(intValue(core::kPlosiveOn) == 0);
        slider(core::kPlosiveThr, "Threshold (low band, 150 Hz)", "%.0f dB", 1.0f, {b.x + pad, b.y + 148.0f, w, 28.0f}, kCleanAccent);
        setDim(false);
        label(b.x + pad, b.y + 200.0f, "Harshness tamer (0 dB = off)", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        slider(core::kTameAmount, "Amount (most it takes off)", "%.1f dB", 1.0f, {b.x + pad, b.y + 212.0f, w, 28.0f}, kCleanAccent);
        setDim(value(core::kTameAmount) <= 0.0f);
        slider(core::kTameFreq, "Frequency", "%.2f kHz", 0.001f, {b.x + pad, b.y + 250.0f, w, 28.0f}, kCleanAccent);
        slider(core::kTameThr, "Threshold", "%.0f dB", 1.0f, {b.x + pad, b.y + 288.0f, w, 28.0f}, kCleanAccent);
        setDim(false);
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
        drawSegments(core::kEssOn, kOff, 2, {b.x + pad + 80.0f, b.y + 44.0f, w - 80.0f, 24.0f}, kEssAccent);
        setDim(intValue(core::kEssOn) == 0);
        label(b.x + pad, b.y + 92.0f, "Cut", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kEssMode, kMode, 2, {b.x + pad + 80.0f, b.y + 80.0f, w - 80.0f, 24.0f}, kEssAccent);
        label(b.x + pad, b.y + 128.0f, "Monitor", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kEssListen, kListen, 2, {b.x + pad + 80.0f, b.y + 116.0f, w - 80.0f, 24.0f}, kEssAccent);
        slider(core::kEssFreq, "Frequency", "%.2f kHz", 0.001f, {b.x + pad, b.y + 152.0f, w, 28.0f}, kEssAccent);
        slider(core::kEssThr, "Threshold", "%.0f dB", 1.0f, {b.x + pad, b.y + 192.0f, w, 28.0f}, kEssAccent);
        slider(core::kEssRange, "Range (most it will take off)", "%.0f dB", 1.0f, {b.x + pad, b.y + 232.0f, w, 28.0f}, kEssAccent);
        setDim(false);
        label(b.x + pad, b.y + b.h - 34.0f, "Press Listen and sweep Frequency until you", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        label(b.x + pad, b.y + b.h - 18.0f, "hear the s sounds alone, then set Threshold.", true, 11.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }

    void drawDynamics(const Rect b)
    {
        drawPanel(b, "EVEN OUT", kDynAccent, "gate and leveller");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        static constexpr const char* kOff[2] = {"Off", "On"};
        label(b.x + pad, b.y + 56.0f, "Gate", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kGateOn, kOff, 2, {b.x + pad + 110.0f, b.y + 44.0f, w - 110.0f, 24.0f}, kDynAccent);
        setDim(intValue(core::kGateOn) == 0);
        slider(core::kGateThr, "Threshold", "%.0f dB", 1.0f, {b.x + pad, b.y + 76.0f, w, 28.0f}, kDynAccent);
        slider(core::kGateRange, "Range (how far it closes)", "%.0f dB", 1.0f, {b.x + pad, b.y + 114.0f, w, 28.0f}, kDynAccent);
        setDim(false);
        label(b.x + pad, b.y + 172.0f, "Leveller", false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        drawSegments(core::kLevelOn, kOff, 2, {b.x + pad + 110.0f, b.y + 160.0f, w - 110.0f, 24.0f}, kDynAccent);
        setDim(intValue(core::kLevelOn) == 0);
        slider(core::kLevelThr, "Threshold", "%.0f dB", 1.0f, {b.x + pad, b.y + 192.0f, w, 28.0f}, kDynAccent);
        slider(core::kLevelRatio, "Ratio", "%.1f : 1", 1.0f, {b.x + pad, b.y + 230.0f, w, 28.0f}, kDynAccent);
        slider(core::kLevelMix, "Mix (parallel)", "%.0f %%", 100.0f, {b.x + pad, b.y + 268.0f, w, 28.0f}, kDynAccent);
        setDim(false);
    }

    void drawPolish(const Rect b)
    {
        drawPanel(b, "POLISH", kPolishAccent, "tone and level");
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        const struct { std::uint32_t id; const char* name; const char* format; float scale; } rows[7] = {
            {core::kMud, "Mud (bell, 300 Hz)", "%+.1f dB", 1.0f},
            {core::kWarmth, "Warmth (low shelf, 200 Hz)", "%+.1f dB", 1.0f},
            {core::kPresence, "Presence (bell, 3.2 kHz)", "%+.1f dB", 1.0f},
            {core::kAir, "Air (high shelf, 10 kHz)", "%+.1f dB", 1.0f},
            {core::kDrive, "Drive (soft saturation)", "%.0f %%", 100.0f},
            {core::kSparkle, "Sparkle (new harmonics)", "%.0f %%", 100.0f},
            {core::kOutput, "Output", "%+.1f dB", 1.0f}};
        float y = b.y + 40.0f;
        for (const auto& r : rows) {
            slider(r.id, r.name, r.format, r.scale, {b.x + pad, y, w, 28.0f}, kPolishAccent);
            y += 40.0f;
        }
    }

    void drawMeters(const Rect b)
    {
        drawPanel(b, "WHAT IT IS DOING", kMeterAccent, "live");
        const float pad = 14.0f, gap = 16.0f;
        const float colW = (b.w - 2.0f * pad - 4.0f * gap) / 5.0f;
        char buf[48];
        const float det = value(core::kStatusDetector), thr = value(core::kEssThr);
        std::snprintf(buf, sizeof(buf), "%.0f dB (thr %.0f)", static_cast<double>(det), static_cast<double>(thr));
        float x = b.x + pad;
        drawBar({x, b.y + 40.0f, colW, 40.0f}, (det + 60.0f) / 60.0f, kEssAccent, "Sibilance level", buf, (thr + 60.0f) / 60.0f);
        x += colW + gap;
        reductionText(buf, sizeof(buf), value(core::kStatusEss));
        drawBar({x, b.y + 40.0f, colW, 40.0f}, value(core::kStatusEss) / 24.0f, kEssAccent, "De-esser", buf);
        x += colW + gap;
        reductionText(buf, sizeof(buf), value(core::kStatusPlosive));
        drawBar({x, b.y + 40.0f, colW, 40.0f}, value(core::kStatusPlosive) / 24.0f, kCleanAccent, "Plosive guard", buf);
        x += colW + gap;
        reductionText(buf, sizeof(buf), value(core::kStatusGate));
        drawBar({x, b.y + 40.0f, colW, 40.0f}, value(core::kStatusGate) / 40.0f, kDynAccent, "Gate", buf);
        x += colW + gap;
        reductionText(buf, sizeof(buf), value(core::kStatusLevel));
        drawBar({x, b.y + 40.0f, colW, 40.0f}, value(core::kStatusLevel) / 30.0f, kDynAccent, "Leveller", buf);
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SilkenUI)
};

UI* createUI()
{
    return new SilkenUI();
}

END_NAMESPACE_DISTRHO
