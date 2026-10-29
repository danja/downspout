#include "downspout/dpf/MagnetoKit.hpp"

#include "spense_core.hpp"
#include "spense_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace core = downspout::spense;

namespace {

constexpr kit::Accent kDisperseAccent {128, 104, 164};  // dusk violet
constexpr kit::Accent kLevelAccent {92, 140, 156};      // cold steel
constexpr kit::Accent kPlotAccent {198, 132, 58};       // amber

constexpr double kPlotMinHz = 20.0, kPlotMaxHz = 20000.0;

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
            ranges[i] = {s.minimum, s.maximum, s.integer, s.logarithmic};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

// Gridline spacing for an axis that must reach `peak`: the smallest round step (1, 2, 2.5, 5 per
// decade) that covers it in five divisions or fewer. The axis then runs to step x divisions.
struct Axis {
    double step;
    int divisions;
};

Axis niceAxis(const double peak)
{
    const double decade = std::pow(10.0, std::floor(std::log10(std::max(peak, 1e-6) / 5.0)));
    for (const double m : {1.0, 2.0, 2.5, 5.0, 10.0}) {
        const double step = m * decade;
        const int divisions = std::max(1, static_cast<int>(std::ceil(peak / step - 1e-9)));
        if (divisions <= 5) return {step, divisions};
    }
    return {10.0 * decade, 5};
}

}  // namespace

class SpenseUI : public MagnetoUI
{
public:
    SpenseUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("SPENSE", "phase disperser",
                   "All-pass stages smear phase, not level: the plot shows how late each frequency arrives. "
                   "Stack adds stages; Resonance narrows them.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawDelayPanel({24.0f, 108.0f, full, 232.0f});
        drawDispersePanel({24.0f, 352.0f, full, 92.0f});
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

    [[nodiscard]] double sampleRate() const
    {
        const double sr = getSampleRate();
        return sr > 1000.0 ? sr : 48000.0;
    }

    void drawDelayPanel(const Rect b)
    {
        const core::Parameters p = currentParameters();
        const double sr = sampleRate();
        const int latency = core::latencySamples(p);
        char caption[96];
        std::snprintf(caption, sizeof(caption), "group delay; latency %d samples (%.2f ms) reported to the host", latency,
                      1000.0 * latency / sr);
        drawPanel(b, "DISPERSION", kPlotAccent, caption);
        const auto& t = theme();
        const Rect plot {b.x + 52.0f, b.y + 44.0f, b.w - 52.0f - 18.0f, b.h - 44.0f - 28.0f};
        drawPlotFrame(plot);

        const int points = static_cast<int>(plot.w);
        std::vector<double> left(static_cast<size_t>(points) + 1), right(left.size());
        double peak = 0.0;
        for (int i = 0; i <= points; ++i) {
            const double hz = kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, static_cast<double>(i) / points);
            left[static_cast<size_t>(i)] = core::groupDelaySamples(p, hz, 0, sr) / sr * 1000.0;
            right[static_cast<size_t>(i)] = core::groupDelaySamples(p, hz, 1, sr) / sr * 1000.0;
            peak = std::max({peak, left[static_cast<size_t>(i)], right[static_cast<size_t>(i)]});
        }
        const Axis axis = niceAxis(peak * 1.03);
        const double top = axis.step * axis.divisions;

        const auto xOf = [&](const double hz) {
            const double n = std::log(hz / kPlotMinHz) / std::log(kPlotMaxHz / kPlotMinHz);
            return plot.x + static_cast<float>(n) * plot.w;
        };
        const auto yOf = [&](const double ms) {
            return plot.y + plot.h - static_cast<float>(std::clamp(ms / top, 0.0, 1.0)) * plot.h;
        };

        // Delay grid on round values of the axis.
        for (int step = 0; step <= axis.divisions; ++step) {
            const double ms = axis.step * step;
            const float y = yOf(ms);
            beginPath();
            sc(t.border);
            strokeWidth(step == 0 ? 1.6f : 0.8f);
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            stroke();
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%g", ms);
            label(plot.x - 8.0f, y, buf, true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        label(b.x + 12.0f, b.y + 36.0f, "ms", true, 10.0f);

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

        // Frequency marker.
        const float cx = xOf(p.frequencyHz);
        beginPath();
        strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 110);
        strokeWidth(1.2f);
        moveTo(cx, plot.y);
        lineTo(cx, plot.y + plot.h);
        stroke();
        // At low Q the delay peaks below this line (0.64 x at Q 0.71), so say what the line is.
        label(cx + (cx < plot.x + plot.w * 0.8f ? 5.0f : -5.0f), plot.y + 6.0f, "Frequency", true, 10.0f,
              (cx < plot.x + plot.w * 0.8f ? ALIGN_LEFT : ALIGN_RIGHT) | ALIGN_TOP);

        // Right channel behind (only distinct once Drift moves its stages), left in front.
        const auto curve = [&](const std::vector<double>& data, const int alpha, const float width) {
            beginPath();
            strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, alpha);
            strokeWidth(width);
            for (int i = 0; i <= points; ++i) {
                const float x = plot.x + static_cast<float>(i) / static_cast<float>(points) * plot.w;
                const float y = yOf(data[static_cast<size_t>(i)]);
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            stroke();
        };
        if (p.drift > 0.001f) curve(right, 120, 1.6f);
        curve(left, 255, 2.4f);

        if (p.drift > 0.001f)
            label(plot.x + plot.w - 8.0f, plot.y + 8.0f, "left bright, right faint: Drift moves each side's stages differently", false, 11.0f,
                  ALIGN_RIGHT | ALIGN_TOP);
    }

    void drawDispersePanel(const Rect b)
    {
        drawPanel(b, "ALL-PASS STACK", kDisperseAccent, "Stack x 8 stages; Drift fans the stages apart around Frequency");
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 3.0f * gap) / 4.0f;
        const float y = b.y + 44.0f;
        float x = b.x + 14.0f;
        drawSlider(core::kStack, "Stack (cascades)", format(core::kStack).c_str(), {x, y, w, 34.0f}, kDisperseAccent);
        x += w + gap;
        drawSlider(core::kFrequency, "Frequency", format(core::kFrequency).c_str(), {x, y, w, 34.0f}, kDisperseAccent);
        x += w + gap;
        drawSlider(core::kResonance, "Resonance (narrow = long)", format(core::kResonance).c_str(), {x, y, w, 34.0f}, kDisperseAccent);
        x += w + gap;
        drawSlider(core::kDrift, "Drift", format(core::kDrift).c_str(), {x, y, w, 34.0f}, kDisperseAccent);
    }

    void drawLevelPanel(const Rect b)
    {
        drawPanel(b, "LEVEL", kLevelAccent, "the all-pass stack itself does not change loudness");
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        const float y = b.y + 44.0f;
        float x = b.x + 14.0f;
        drawSlider(core::kPreGain, "Pre Gain (into the stack)", format(core::kPreGain).c_str(), {x, y, w, 34.0f}, kLevelAccent);
        x += w + gap;
        drawSlider(core::kPostGain, "Post Gain", format(core::kPostGain).c_str(), {x, y, w, 34.0f}, kLevelAccent);
        x += w + gap;
        static const char* const kClipNames[] = {"Off", "Clip at 0 dB"};
        label(x, y, "Output clipper");
        drawSegments(core::kClip, kClipNames, 2, {x, y + 16.0f, w, 24.0f}, kLevelAccent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[40];
        const float v = value(parameter);
        switch (parameter) {
        case core::kStack:
            std::snprintf(buffer, sizeof(buffer), "%d (%d stages)", intValue(parameter), intValue(parameter) * core::kStagesPerCascade);
            break;
        case core::kFrequency:
            if (v >= 1000.0f)
                std::snprintf(buffer, sizeof(buffer), "%.2f kHz", static_cast<double>(v) / 1000.0);
            else
                std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
            break;
        case core::kResonance:
            std::snprintf(buffer, sizeof(buffer), "Q %.2f", static_cast<double>(v));
            break;
        case core::kDrift:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v) * 100.0);
            break;
        default:  // the two gains
            if (v <= core::kMinGainDb + 0.05f) return "-inf dB";
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        }
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpenseUI)
};

UI* createUI()
{
    return new SpenseUI();
}

END_NAMESPACE_DISTRHO
