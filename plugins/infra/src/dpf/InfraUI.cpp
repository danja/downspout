#include "downspout/dpf/MagnetoKit.hpp"

#include "infra_core.hpp"
#include "infra_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::infra;

namespace {

constexpr kit::Accent kLfoAccent {198, 132, 58};    // amber
constexpr kit::Accent kRateAccent {92, 140, 156};   // steel
constexpr kit::Accent kVcaAccent {120, 132, 96};    // sage
constexpr kit::Accent kRightAccent {92, 140, 156};  // the right channel's curve

constexpr std::array<const char*, core::kWaveformCount> kWaveNames = {
    "Sine", "Triangle", "Ramp up", "Ramp down", "Square", "Soft square", "Sample & hold", "Smooth random"};

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

class InfraUI : public MagnetoUI
{
public:
    InfraUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no transport; let it show a plausible picture (120 BPM, Auto).
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(core::kStatusRate, 10.0f);
            setValue(core::kStatusMultiple, 5.0f);
            setValue(core::kStatusLfoL, 0.93f);
            setValue(core::kStatusLfoR, 0.93f);
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
        drawHeader("INFRA", "beat-locked tremolo",
                   "A low-frequency oscillator, locked to the host's beat, drives the gain of both channels.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawScopePanel({24.0f, 108.0f, full, 228.0f});
        const float gap = 12.0f;
        const float w = (full - gap) * 0.5f;
        drawRatePanel({24.0f, 348.0f, w, 158.0f});
        drawVcaPanel({24.0f + w + gap, 348.0f, w, 158.0f});
    }

private:
    [[nodiscard]] float rangeDb() const { return value(core::kRange); }

    void drawScopePanel(const Rect b)
    {
        char right[48];
        std::snprintf(right, sizeof(right), "now: L %+.1f dB   R %+.1f dB",
                      static_cast<double>(core::gainDbFor(value(core::kStatusLfoL), rangeDb())),
                      static_cast<double>(core::gainDbFor(value(core::kStatusLfoR), rangeDb())));
        drawPanel(b, "LFO", kLfoAccent, right);
        const auto& t = theme();
        const Rect plot {b.x + 64.0f, b.y + 46.0f, b.w - 64.0f - 16.0f, b.h - 46.0f - 28.0f};
        drawPlotFrame(plot);

        // Gain scale: the LFO peak is 0 dB, the trough is -Range.
        char buf[24];
        label(plot.x - 8.0f, plot.y + 2.0f, "0 dB", true, 11.0f, ALIGN_RIGHT | ALIGN_TOP);
        std::snprintf(buf, sizeof(buf), "-%.1f dB", static_cast<double>(rangeDb()));
        label(plot.x - 8.0f, plot.y + plot.h - 2.0f, buf, true, 11.0f, ALIGN_RIGHT | ALIGN_BOTTOM);

        const auto wave = static_cast<core::Waveform>(intValue(core::kWaveform));
        constexpr int kSteps = 360;
        constexpr double kCycles = 2.0;   // the plot shows two cycles
        const int phases[2] = {intValue(core::kPhaseL), intValue(core::kPhaseR)};
        const kit::Accent colours[2] = {kLfoAccent, kRightAccent};
        for (int pass = 0; pass < 2; ++pass) {
            // Draw the left channel last so it stays visible when the two coincide (the default).
            const int c = 1 - pass;
            beginPath();
            strokeColor(colours[c].r, colours[c].g, colours[c].b, 255);
            strokeWidth(c == 0 ? 2.6f : 2.0f);
            for (int i = 0; i <= kSteps; ++i) {
                const double x = kCycles * i / kSteps;
                const float v = core::lfoValue(wave, x + 0.25 * phases[c]);
                const float px = plot.x + plot.w * static_cast<float>(i) / kSteps;
                const float py = plot.y + 10.0f + (1.0f - v) * (plot.h - 20.0f);
                if (i == 0) moveTo(px, py); else lineTo(px, py);
            }
            stroke();
        }
        // One cycle divider.
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(plot.x + plot.w * 0.5f, plot.y);
        lineTo(plot.x + plot.w * 0.5f, plot.y + plot.h);
        stroke();

        // Legend and the beat the cycles are locked to.
        const float ly = plot.y + plot.h + 6.0f;
        beginPath();
        fillColor(kLfoAccent.r, kLfoAccent.g, kLfoAccent.b, 255);
        rect(plot.x, ly + 4.0f, 14.0f, 4.0f);
        fill();
        label(plot.x + 20.0f, ly, "Left", false, 11.0f);
        beginPath();
        fillColor(kRightAccent.r, kRightAccent.g, kRightAccent.b, 255);
        rect(plot.x + 68.0f, ly + 4.0f, 14.0f, 4.0f);
        fill();
        label(plot.x + 88.0f, ly, "Right", false, 11.0f);
        label(plot.x + plot.w, ly, "two cycles; each channel is the same shape shifted by its phase", true, 11.0f,
              ALIGN_RIGHT | ALIGN_TOP);
    }

    void drawRatePanel(const Rect b)
    {
        drawPanel(b, "RATE & SHAPE", kRateAccent, "locked to the host beat");
        const float pad = 14.0f;
        const float w = b.w - 2.0f * pad;
        drawStepper(core::kWaveform, "Waveform", kWaveNames[static_cast<size_t>(std::clamp(intValue(core::kWaveform), 0, core::kWaveformCount - 1))],
                    {b.x + pad, b.y + 42.0f, w, 22.0f});
        char buf[48];
        const int m = intValue(core::kMultiple);
        const float used = value(core::kStatusMultiple);
        if (m == 0) std::snprintf(buf, sizeof(buf), "Auto: %.0f x beat", static_cast<double>(used));
        else        std::snprintf(buf, sizeof(buf), "%d x beat", m);
        drawSlider(core::kMultiple, "Cycles per beat (0 = Auto, nearest 10 Hz)", buf, {b.x + pad, b.y + 86.0f, w, 28.0f}, kRateAccent);
        if (value(core::kStatusRate) > 0.0f) {
            std::snprintf(buf, sizeof(buf), "%.1f Hz now (%.0f x beat)", static_cast<double>(value(core::kStatusRate)), static_cast<double>(used));
            label(b.x + pad, b.y + 126.0f, buf, false, 12.0f);
        } else {
            label(b.x + pad, b.y + 126.0f, "no audio yet; rate is held to 1-20 Hz", true, 11.0f);
        }
    }

    void drawVcaPanel(const Rect b)
    {
        drawPanel(b, "VCA", kVcaAccent, "gain falls from 0 dB to -Range");
        const float pad = 14.0f;
        const float w = b.w - 2.0f * pad;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "0 to -%.1f dB", static_cast<double>(rangeDb()));
        drawSlider(core::kRange, "Range", buf, {b.x + pad, b.y + 42.0f, w, 28.0f}, kVcaAccent);
        static constexpr const char* kPhases[4] = {"0\xC2\xB0", "90\xC2\xB0", "180\xC2\xB0", "270\xC2\xB0"};
        label(b.x + pad, b.y + 82.0f, "Phase  Left");
        drawSegments(core::kPhaseL, kPhases, 4, {b.x + pad + 92.0f, b.y + 78.0f, w - 92.0f, 24.0f}, kLfoAccent);
        label(b.x + pad, b.y + 116.0f, "Phase  Right");
        drawSegments(core::kPhaseR, kPhases, 4, {b.x + pad + 92.0f, b.y + 112.0f, w - 92.0f, 24.0f}, kRightAccent);
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InfraUI)
};

UI* createUI()
{
    return new InfraUI();
}

END_NAMESPACE_DISTRHO
