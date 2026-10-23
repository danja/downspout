#include "downspout/dpf/MagnetoKit.hpp"

#include "ladder_core.hpp"
#include "ladder_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::ladder;

namespace {

constexpr kit::Accent kFilterAccent {198, 120, 64};  // copper
constexpr kit::Accent kLevelAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kPlotAccent {198, 132, 58};    // amber

constexpr double kPlotMinHz = 20.0, kPlotMaxHz = 20000.0;
constexpr float kPlotMinDb = -48.0f, kPlotMaxDb = 36.0f;

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, false, s.logarithmic};
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

class LadderUI : public MagnetoUI
{
public:
    LadderUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("LADDER", "four-pole low-pass",
                   "Resonance feeds the output back to the input and rings on its own near 85 %. "
                   "Bass Comp puts back the low end that the feedback takes away.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawResponsePanel({24.0f, 108.0f, full, 232.0f});
        drawFilterPanel({24.0f, 352.0f, full, 92.0f});
        drawLevelPanel({24.0f, 456.0f, full, 92.0f});
    }

private:
    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            core::field(p, i) = value(i);
        return p;
    }

    void drawResponsePanel(const Rect b)
    {
        drawPanel(b, "RESPONSE", kPlotAccent, "small signal; Drive adds saturation that is not plotted");
        const auto& t = theme();
        const Rect plot {b.x + 52.0f, b.y + 44.0f, b.w - 52.0f - 18.0f, b.h - 44.0f - 28.0f};
        drawPlotFrame(plot);

        const auto xOf = [&](const double hz) {
            const double n = std::log(hz / kPlotMinHz) / std::log(kPlotMaxHz / kPlotMinHz);
            return plot.x + static_cast<float>(n) * plot.w;
        };
        const auto yOf = [&](const double db) {
            const float clamped = std::clamp(static_cast<float>(db), kPlotMinDb, kPlotMaxDb);
            return plot.y + (kPlotMaxDb - clamped) / (kPlotMaxDb - kPlotMinDb) * plot.h;
        };

        // dB grid; 0 dB drawn stronger.
        for (int db = static_cast<int>(kPlotMinDb); db <= static_cast<int>(kPlotMaxDb); db += 12) {
            const float y = yOf(db);
            beginPath();
            sc(t.border);
            strokeWidth(db == 0 ? 1.6f : 0.8f);
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            stroke();
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%+d", db);
            label(plot.x - 8.0f, y, buf, true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        label(b.x + 12.0f, b.y + 36.0f, "dB", true, 10.0f);

        // Frequency grid.
        static constexpr double kMarks[] = {50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0};
        for (const double hz : kMarks) {
            const float x = xOf(hz);
            beginPath();
            sc(t.border);
            strokeWidth(0.8f);
            moveTo(x, plot.y);
            lineTo(x, plot.y + plot.h);
            stroke();
            char buf[16];
            if (hz >= 1000.0)
                std::snprintf(buf, sizeof(buf), "%g k", hz / 1000.0);
            else
                std::snprintf(buf, sizeof(buf), "%g", hz);
            label(x, plot.y + plot.h + 6.0f, buf, true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
        }

        // Cutoff marker.
        const core::Parameters p = currentParameters();
        const float cx = xOf(p.cutoffHz);
        beginPath();
        strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 110);
        strokeWidth(1.2f);
        moveTo(cx, plot.y);
        lineTo(cx, plot.y + plot.h);
        stroke();

        // The curve, from the same model the tests compare with the filter.
        const int points = static_cast<int>(plot.w);
        beginPath();
        strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 255);
        strokeWidth(2.4f);
        // Below the floor the curve is simply not drawn, rather than clamped into a flat line
        // that would read as a real response.
        bool penDown = false;
        for (int i = 0; i <= points; ++i) {
            const double n = static_cast<double>(i) / points;
            const double hz = kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
            const float x = plot.x + static_cast<float>(n) * plot.w;
            const double db = core::responseDb(p, hz);
            if (db < kPlotMinDb) {
                penDown = false;
                continue;
            }
            const float y = yOf(db);
            if (penDown) lineTo(x, y); else moveTo(x, y);
            penDown = true;
        }
        stroke();

        if (p.resonance >= 85.0f)
            label(plot.x + plot.w - 8.0f, plot.y + 8.0f, "self-oscillating: the peak is a ringing tone", false, 11.0f,
                  ALIGN_RIGHT | ALIGN_TOP);
    }

    void drawFilterPanel(const Rect b)
    {
        drawPanel(b, "FILTER", kFilterAccent);
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        const float y = b.y + 44.0f;
        float x = b.x + 14.0f;
        drawSlider(core::kCutoff, "Cutoff", format(core::kCutoff).c_str(), {x, y, w, 34.0f}, kFilterAccent);
        x += w + gap;
        drawSlider(core::kResonance, "Resonance", format(core::kResonance).c_str(), {x, y, w, 34.0f}, kFilterAccent);
        x += w + gap;
        drawSlider(core::kDrive, "Drive (into the saturators)", format(core::kDrive).c_str(), {x, y, w, 34.0f}, kFilterAccent);
    }

    void drawLevelPanel(const Rect b)
    {
        drawPanel(b, "LEVEL", kLevelAccent);
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        const float y = b.y + 44.0f;
        float x = b.x + 14.0f;
        drawSlider(core::kBassComp, "Bass Comp (restores low end)", format(core::kBassComp).c_str(), {x, y, w, 34.0f}, kLevelAccent);
        x += w + gap;
        drawSlider(core::kMix, "Mix", format(core::kMix).c_str(), {x, y, w, 34.0f}, kLevelAccent);
        x += w + gap;
        drawSlider(core::kOutput, "Output", format(core::kOutput).c_str(), {x, y, w, 34.0f}, kLevelAccent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
        switch (parameter) {
        case core::kCutoff:
            if (v >= 1000.0f)
                std::snprintf(buffer, sizeof(buffer), "%.2f kHz", static_cast<double>(v) / 1000.0);
            else
                std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
            break;
        case core::kDrive:
        case core::kOutput:
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
            break;
        }
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LadderUI)
};

UI* createUI()
{
    return new LadderUI();
}

END_NAMESPACE_DISTRHO
