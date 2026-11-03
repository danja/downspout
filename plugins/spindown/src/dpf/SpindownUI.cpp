#include "downspout/dpf/MagnetoKit.hpp"

#include "spindown_core.hpp"
#include "spindown_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::spindown;

namespace {

constexpr kit::Accent kTapeAccent {198, 132, 58};    // amber
constexpr kit::Accent kTriggerAccent {92, 140, 156}; // steel
constexpr kit::Accent kCycleAccent {120, 132, 96};   // sage
constexpr kit::Accent kStopAccent {186, 84, 72};     // red: the tape is stopped

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::specs()[i];
            ranges[i] = {s.minimum, s.maximum, s.integer, i == core::kStopTime || i == core::kStartTime};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

std::string formatMs(const float ms)
{
    char buf[24];
    if (ms >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f s", static_cast<double>(ms) / 1000.0);
    else               std::snprintf(buf, sizeof(buf), "%.0f ms", static_cast<double>(ms));
    return buf;
}

}  // namespace

class SpindownUI : public MagnetoUI
{
public:
    SpindownUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no transport or audio: show a tape part-way through a stop.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(core::kStatusSpeed, 0.42f);
            setValue(core::kStatusDelay, 180.0f);
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
        drawHeader("SPINDOWN", "tape stop",
                   "The tape slows to a standstill, pitch and time falling together, then spins back up to the live signal.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawSpeedPanel({24.0f, 108.0f, full, 214.0f});
        const float gap = 12.0f;
        const float w = (full - gap) * 0.5f;
        drawTapePanel({24.0f, 334.0f, w, 172.0f});
        drawTriggerPanel({24.0f + w + gap, 334.0f, w, 172.0f});
    }

private:
    [[nodiscard]] float stopMs() const { return value(core::kStopTime); }
    [[nodiscard]] float startMs() const { return value(core::kStartTime); }
    [[nodiscard]] float curve() const { return value(core::kCurve); }

    void drawSpeedPanel(const Rect b)
    {
        char right[64];
        const float speed = value(core::kStatusSpeed);
        std::snprintf(right, sizeof(right), "now: %.0f %% speed   lag %s", static_cast<double>(speed * 100.0f),
                      formatMs(value(core::kStatusDelay)).c_str());
        drawPanel(b, "TAPE SPEED", kTapeAccent, right);
        const auto& t = theme();
        const Rect plot {b.x + 64.0f, b.y + 46.0f, b.w - 64.0f - 16.0f, b.h - 46.0f - 28.0f};
        drawPlotFrame(plot);

        label(plot.x - 8.0f, plot.y + 2.0f, "100 %", true, 11.0f, ALIGN_RIGHT | ALIGN_TOP);
        label(plot.x - 8.0f, plot.y + plot.h - 2.0f, "0", true, 11.0f, ALIGN_RIGHT | ALIGN_BOTTOM);

        // One stop followed by one start. The horizontal axis is time: the stop takes Stop Time, a short pause, then Start Time.
        const float pause = std::max(0.12f * (stopMs() + startMs()), 1.0f);
        const float total = stopMs() + pause + startMs();
        const float top = plot.y + 10.0f, bottom = plot.y + plot.h - 10.0f;
        const auto px = [&](const float ms) { return plot.x + plot.w * ms / total; };
        const auto py = [&](const float s) { return bottom - s * (bottom - top); };

        beginPath();
        strokeColor(kTapeAccent.r, kTapeAccent.g, kTapeAccent.b, 255);
        strokeWidth(2.6f);
        constexpr int kSteps = 240;
        moveTo(px(0.0f), py(1.0f));
        for (int i = 1; i <= kSteps; ++i) {
            const float f = static_cast<float>(i) / kSteps;
            lineTo(px(f * stopMs()), py(core::speedFor(f, curve())));
        }
        lineTo(px(stopMs() + pause), py(0.0f));
        for (int i = 1; i <= kSteps; ++i) {
            const float f = static_cast<float>(i) / kSteps;
            lineTo(px(stopMs() + pause + f * startMs()), py(core::speedFor(1.0 - f, curve())));
        }
        stroke();

        // The current speed as a line across the plot.
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(plot.x, py(speed));
        lineTo(plot.x + plot.w, py(speed));
        stroke();

        const float ly = plot.y + plot.h + 6.0f;
        label(plot.x, ly, ("stop " + formatMs(stopMs())).c_str(), false, 11.0f);
        label(plot.x + plot.w, ly, ("start " + formatMs(startMs())).c_str(), false, 11.0f, ALIGN_RIGHT | ALIGN_TOP);
        label(plot.x + plot.w * 0.5f, ly, "pitch follows the speed; the line marks the speed right now", true, 11.0f,
              ALIGN_CENTER | ALIGN_TOP);
    }

    void drawTapePanel(const Rect b)
    {
        drawPanel(b, "TAPE", kTapeAccent, "how it stops and starts");
        const float pad = 14.0f;
        const float w = b.w - 2.0f * pad;
        drawSlider(core::kStopTime, "Stop Time", formatMs(stopMs()).c_str(), {b.x + pad, b.y + 40.0f, w, 28.0f}, kTapeAccent);
        drawSlider(core::kStartTime, "Start Time", formatMs(startMs()).c_str(), {b.x + pad, b.y + 80.0f, w, 28.0f}, kTapeAccent);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.2f %s", static_cast<double>(curve()), curve() < 1.0f ? "(late drop)" : (curve() > 1.0f ? "(early drop)" : "(straight)"));
        drawSlider(core::kCurve, "Curve", buf, {b.x + pad, b.y + 120.0f, w, 28.0f}, kTapeAccent);
    }

    void drawTriggerPanel(const Rect b)
    {
        drawPanel(b, "TRIGGER", kTriggerAccent, "what stops the tape");
        const float pad = 14.0f;
        const float w = b.w - 2.0f * pad;
        static constexpr const char* kTriggers[3] = {"Manual", "MIDI", "Cycle"};
        drawSegments(core::kTrigger, kTriggers, 3, {b.x + pad, b.y + 38.0f, w, 24.0f}, kTriggerAccent);
        const int mode = intValue(core::kTrigger);
        if (mode == 0) {
            static constexpr const char* kStop[2] = {"Playing", "STOPPED"};
            drawSegments(core::kStop, kStop, 2, {b.x + pad, b.y + 76.0f, w, 24.0f}, intValue(core::kStop) != 0 ? kStopAccent : kCycleAccent);
            label(b.x + pad, b.y + 114.0f, "Automate Stop, or send CC 4 (64+) on the CC channel.", true, 11.0f);
        } else if (mode == 1) {
            label(b.x + pad, b.y + 80.0f, "Hold any note to stop the tape;", false, 12.0f);
            label(b.x + pad, b.y + 98.0f, "release every note to start it.", false, 12.0f);
        } else {
            static constexpr const char* kCycles[4] = {"1 bar", "2 bars", "4 bars", "8 bars"};
            drawSegments(core::kCycle, kCycles, 4, {b.x + pad, b.y + 76.0f, w, 24.0f}, kCycleAccent);
            char buf[40];
            std::snprintf(buf, sizeof(buf), "last %.2f beats", static_cast<double>(value(core::kLength)));
            drawSlider(core::kLength, "Stopped for", buf, {b.x + pad, b.y + 108.0f, w, 28.0f}, kCycleAccent);
        }
        char cc[48];
        const int ch = intValue(core::kCcChannel);
        if (ch == 0) std::snprintf(cc, sizeof(cc), "CC control off");
        else         std::snprintf(cc, sizeof(cc), "CC 1-4 on channel %d: times, curve, stop", ch);
        drawStepper(core::kCcChannel, "CC Channel", cc, {b.x + pad, b.y + b.h - 30.0f, w, 22.0f});
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpindownUI)
};

UI* createUI()
{
    return new SpindownUI();
}

END_NAMESPACE_DISTRHO
