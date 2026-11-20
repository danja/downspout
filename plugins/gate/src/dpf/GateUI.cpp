#include "downspout/dpf/MagnetoKit.hpp"

#include "gate_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::gate;

constexpr kit::Accent kDetectorAccent {198, 132, 58};  // amber: sidechain and threshold
constexpr kit::Accent kShapeAccent {148, 112, 168};    // violet: hysteresis, hold and envelope
constexpr kit::Accent kOutputAccent {120, 140, 92};    // olive: output and bypass

constexpr std::uint32_t pid(const core::ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};

    Tables()
    {
        for (std::size_t i = 0; i < core::kParameterCount; ++i) {
            const core::ParamSpec& spec = core::specs()[i];
            ranges[i] = {spec.minimum, spec.maximum, spec.integer, spec.logarithmic};
            defaults[i] = spec.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

constexpr int kActionReset = 0;

}  // namespace

class GateUI : public MagnetoUI {
public:
    GateUI()
        : MagnetoUI(tables().ranges.data(), static_cast<std::uint32_t>(core::kParameterCount),
                    tables().defaults.data())
    {
    }

protected:
    void onAction(const int id) override
    {
        if (id != kActionReset)
            return;
        for (std::uint32_t i = 0; i < core::kInputParameterCount; ++i)
            commit(i, core::specs()[i].defaultValue);
        history_.clear();
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("GATE", "Linked stereo noise gate",
                   "Main L/R in-out; sidechain L/R on inputs 3-4. Detector drives both channels.");

        drawDetectorPanel({24.0f, 108.0f, 872.0f, 190.0f});
        drawShapePanel({24.0f, 310.0f, 872.0f, 172.0f});
        drawPlotPanel({24.0f, 494.0f, 872.0f, 136.0f});
        drawOutputPanel({24.0f, 642.0f, 872.0f, 40.0f});
    }

private:
    struct Sample {
        float level;
        float reduction;
        bool open;
    };

    static constexpr std::size_t kHistorySize = 180;

    void drawDetectorPanel(const Rect b)
    {
        drawPanel(b, "DETECTOR", kDetectorAccent, "sidechain source + level");
        label(b.x + 16.0f, b.y + 40.0f, "Source", false, 12.0f);
        drawSegments(pid(core::ParamId::scSource), core::kSourceNames.data(), 2,
                     {b.x + 16.0f, b.y + 58.0f, 208.0f, 28.0f}, kDetectorAccent);
        drawSlider(pid(core::ParamId::threshold), "Threshold",
                   format(core::ParamId::threshold).c_str(), {b.x + 248.0f, b.y + 40.0f, 190.0f, 34.0f},
                   kDetectorAccent);
        drawSlider(pid(core::ParamId::hysteresis), "Hysteresis",
                   format(core::ParamId::hysteresis).c_str(), {b.x + 458.0f, b.y + 40.0f, 190.0f, 34.0f},
                   kDetectorAccent);
        drawSlider(pid(core::ParamId::scHpf), "SC HPF",
                   format(core::ParamId::scHpf).c_str(), {b.x + 668.0f, b.y + 40.0f, 188.0f, 34.0f},
                   kDetectorAccent);

        label(b.x + 248.0f, b.y + 92.0f, "Closes at", false, 12.0f);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f dB", closeThreshold());
        label(b.x + 248.0f, b.y + 110.0f, buf, true, 13.0f);
        label(b.x + 458.0f, b.y + 92.0f, "Detector", false, 12.0f);
        std::snprintf(buf, sizeof(buf), "%.1f dB", value(pid(core::ParamId::outLevel)));
        label(b.x + 458.0f, b.y + 110.0f, buf, true, 13.0f);
        label(b.x + 668.0f, b.y + 92.0f, "Gate", false, 12.0f);
        label(b.x + 668.0f, b.y + 110.0f, value(pid(core::ParamId::outOpen)) >= 0.5f ? "OPEN" : "CLOSED",
              true, 13.0f);
        label(b.x + 16.0f, b.y + 104.0f, "External stays shut when unconnected.", true, 11.0f);
        label(b.x + 16.0f, b.y + 124.0f, "The HPF affects only the detector.", true, 11.0f);
        drawButton(kActionReset, "Reset", {b.x + 16.0f, b.y + 144.0f, 208.0f, 28.0f}, kDetectorAccent, false);
    }

    void drawShapePanel(const Rect b)
    {
        drawPanel(b, "SHAPING", kShapeAccent, "range + ballistics");
        drawSlider(pid(core::ParamId::range), "Range",
                   format(core::ParamId::range).c_str(), {b.x + 16.0f, b.y + 40.0f, 190.0f, 34.0f},
                   kShapeAccent);
        drawSlider(pid(core::ParamId::attack), "Attack",
                   format(core::ParamId::attack).c_str(), {b.x + 226.0f, b.y + 40.0f, 190.0f, 34.0f},
                   kShapeAccent);
        drawSlider(pid(core::ParamId::hold), "Hold",
                   format(core::ParamId::hold).c_str(), {b.x + 436.0f, b.y + 40.0f, 190.0f, 34.0f},
                   kShapeAccent);
        drawSlider(pid(core::ParamId::release), "Release",
                   format(core::ParamId::release).c_str(), {b.x + 646.0f, b.y + 40.0f, 210.0f, 34.0f},
                   kShapeAccent);

        label(b.x + 16.0f, b.y + 96.0f, "Range is maximum attenuation when closed; 0 dB is transparent.",
              true, 11.0f);
        label(b.x + 16.0f, b.y + 116.0f, "Hold counts only below the close point, then Release fades the gate.",
              true, 11.0f);
        label(b.x + 16.0f, b.y + 136.0f, "Shared envelope keeps the stereo image fixed.", true, 11.0f);
    }

    void drawPlotPanel(const Rect b)
    {
        drawPanel(b, "LIVE HISTORY", kDetectorAccent, "detector / threshold / gate");
        const Rect plot {b.x + 16.0f, b.y + 40.0f, b.w - 32.0f, b.h - 54.0f};
        drawPlotFrame(plot);
        recordSample();

        const auto& t = theme();
        const float openY = plot.y + 9.0f;
        const float openH = 12.0f;
        sc(t.border);
        strokeWidth(1.0f);
        beginPath();
        moveTo(plot.x, plot.y + 26.0f);
        lineTo(plot.x + plot.w, plot.y + 26.0f);
        stroke();

        // Gate state strip, newest at the right.
        for (std::size_t i = 0; i < history_.size(); ++i) {
            const Sample& s = history_[i];
            if (!s.open)
                continue;
            const float x = plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kHistorySize - 1);
            beginPath();
            fillColor(kShapeAccent.r, kShapeAccent.g, kShapeAccent.b, 150);
            rect(x, openY, std::max(2.0f, plot.w / static_cast<float>(kHistorySize)), openH);
            fill();
        }

        const float topDb = 6.0f;
        const float bottomDb = -80.0f;
        const auto yOf = [&](const float db) {
            const float n = std::clamp((db - bottomDb) / (topDb - bottomDb), 0.0f, 1.0f);
            return plot.y + plot.h - 4.0f - n * (plot.h - 38.0f);
        };

        // Threshold and close point.
        const float openLine = yOf(value(pid(core::ParamId::threshold)));
        const float closeLine = yOf(closeThreshold());
        for (const float y : {openLine, closeLine}) {
            beginPath();
            sc(t.textDim);
            strokeWidth(1.0f);
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            stroke();
        }
        label(plot.x + 4.0f, openLine - 12.0f, "OPEN", true, 9.0f);
        label(plot.x + 4.0f, closeLine + 2.0f, "CLOSE", true, 9.0f);

        // Detector trace.
        beginPath();
        strokeColor(kDetectorAccent.r, kDetectorAccent.g, kDetectorAccent.b, 255);
        strokeWidth(2.0f);
        bool first = true;
        for (std::size_t i = 0; i < history_.size(); ++i) {
            const float x = plot.x + plot.w * static_cast<float>(i) / static_cast<float>(kHistorySize - 1);
            const float y = yOf(history_[i].level);
            if (first) {
                moveTo(x, y);
                first = false;
            } else {
                lineTo(x, y);
            }
        }
        stroke();

        label(plot.x + 4.0f, plot.y + 4.0f, "+6 dB", true, 9.0f);
        label(plot.x + 4.0f, plot.y + plot.h - 13.0f, "-80 dB", true, 9.0f);
    }

    void drawOutputPanel(const Rect b)
    {
        drawSlider(pid(core::ParamId::outputDb), "Output",
                   format(core::ParamId::outputDb).c_str(), {b.x + 16.0f, b.y + 0.0f, 260.0f, 34.0f},
                   kOutputAccent);
        label(b.x + 300.0f, b.y + 3.0f, "Reduction", false, 12.0f);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.1f dB", value(pid(core::ParamId::outReduction)));
        label(b.x + 380.0f, b.y + 3.0f, buf, true, 12.0f);
        label(b.x + 500.0f, b.y + 3.0f, "Bypass", false, 12.0f);
        drawSegments(pid(core::ParamId::bypass), core::kOffOnNames.data(), 2,
                     {b.x + 560.0f, b.y - 2.0f, 140.0f, 26.0f}, kOutputAccent);
        label(b.x + 720.0f, b.y + 3.0f, "0 latency", true, 11.0f);
    }

    void recordSample()
    {
        Sample s {value(pid(core::ParamId::outLevel)), value(pid(core::ParamId::outReduction)),
                  value(pid(core::ParamId::outOpen)) >= 0.5f};
        if (!history_.empty() && history_.back().level == s.level && history_.back().open == s.open &&
            history_.back().reduction == s.reduction)
            return;
        history_.push_back(s);
        while (history_.size() > kHistorySize)
            history_.pop_front();
    }

    [[nodiscard]] float closeThreshold() const noexcept
    {
        return std::clamp(value(pid(core::ParamId::threshold)) - value(pid(core::ParamId::hysteresis)),
                          core::kMinThresholdDb - core::kMaxHysteresisDb, 0.0f);
    }

    [[nodiscard]] std::string format(const core::ParamId id) const
    {
        const std::uint32_t index = pid(id);
        const float v = value(index);
        const core::ParamSpec& spec = core::specs()[index];
        char buf[64];
        switch (id) {
        case core::ParamId::scSource:
            return core::kSourceNames[std::clamp(static_cast<int>(v), 0, 1)];
        case core::ParamId::bypass:
            return core::kOffOnNames[std::clamp(static_cast<int>(v), 0, 1)];
        case core::ParamId::attack:
        case core::ParamId::hold:
        case core::ParamId::release:
            std::snprintf(buf, sizeof(buf), "%.0f %s", v, spec.unit);
            break;
        case core::ParamId::scHpf:
            std::snprintf(buf, sizeof(buf), "%.0f %s", v, spec.unit);
            break;
        default:
            std::snprintf(buf, sizeof(buf), "%+.1f %s", v, spec.unit);
            break;
        }
        return buf;
    }

    std::deque<Sample> history_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GateUI)
};

UI* createUI()
{
    return new GateUI();
}

END_NAMESPACE_DISTRHO
