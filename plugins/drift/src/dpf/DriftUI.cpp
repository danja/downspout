#include "generative_panel_ui.hpp"
#include "drift_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class DriftUI : public GenerativePanelUI {
public:
    DriftUI()
        : GenerativePanelUI(
            "Drift",
            "Four independent MIDI CC lanes with visible shape, range, timing, and destination",
            downspout::drift::kParameterSpecs.data(),
            downspout::drift::kParameterCount, 117, 190, 134) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::drift;
        beginPanel();

        drawSection(24, 98, 1032, 46, "GLOBAL");
        drawSlider(kSeed, 602, 101, 210, "Seed", "", "Deterministic modulation identity.", 0);
        drawSlider(kRateLimit, 820, 101, 222, "Event budget", " / sec",
                   "Maximum MIDI events generated per second.", 0);
        char status[48] {};
        std::snprintf(status, sizeof(status), "%d events this block",
                      static_cast<int>(std::lround(value(kStatusEvents))));
        fillColor(176, 185, 180, 255);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(102, 116, status, nullptr);

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float x = lane % 2 == 0 ? 24.0f : 547.0f;
            const float y = lane < 2 ? 155.0f : 445.0f;
            drawLane(lane, x, y, 509.0f, 276.0f);
        }
        endPanel();
    }

    void drawLane(const int lane, const float x, const float y, const float w, const float h)
    {
        using namespace downspout::drift;
        static constexpr const char* modes[] {"LFO", "S&H", "Walk", "Chaos", "Follower"};
        char title[24] {};
        std::snprintf(title, sizeof(title), "LANE %d", lane + 1);
        drawSection(x, y, w, h, title);
        drawPreview(lane, x + 267, y + 10, w - 280, 22);

        const auto mode = laneParam(lane, kMode);
        const bool follower = static_cast<int>(std::lround(value(mode))) == 4;
        drawChoice(mode, x + 14, y + 35, w - 28, "Source", modes, 5,
                   "Choose the lane's modulation source.");
        drawBeatSlider(laneParam(lane, kRate), x + 14, y + 94, 231, "Cycle / step",
                       "Transport duration of one cycle or generated step.", !follower);
        drawPercentSlider(laneParam(lane, kSmoothing), x + 253, y + 94, 242, "Smoothing",
                          "Smooth abrupt CC changes.");
        drawSlider(laneParam(lane, kMinimum), x + 14, y + 150, 231, "Range minimum", "",
                   "Lowest CC value emitted.", 0);
        drawSlider(laneParam(lane, kMaximum), x + 253, y + 150, 242, "Range maximum", "",
                   "Highest CC value emitted.", 0);
        drawPercentSlider(laneParam(lane, kPhase), x + 14, y + 206, 152, "Phase",
                          "Offset this lane within its cycle.", !follower);
        drawCcSlider(laneParam(lane, kController), x + 174, y + 206, 160, "Destination",
                     "MIDI controller destination.");
        drawChannelSlider(laneParam(lane, kChannel), x + 342, y + 206, 153, "Channel",
                          "MIDI output channel.");
    }

    void drawPreview(const int lane, const float x, const float y, const float w, const float h)
    {
        using namespace downspout::drift;
        const int mode = static_cast<int>(std::lround(value(laneParam(lane, kMode))));
        const float phase = value(laneParam(lane, kPhase));
        beginPath();
        strokeColor(accentR(), accentG(), accentB(), 210);
        strokeWidth(1.8f);
        for (int i = 0; i <= 40; ++i) {
            const float t = static_cast<float>(i) / 40.0f;
            float v = 0.5f;
            if (mode == 0)
                v = 0.5f + 0.42f * std::sin(6.2831853f * (t + phase));
            else if (mode == 1)
                v = 0.15f + 0.7f * static_cast<float>(((i / 8) * 37 + lane * 11) % 17) / 16.0f;
            else if (mode == 2)
                v = std::clamp(0.5f + 0.08f * static_cast<float>((i / 5) % 5 - 2), 0.08f, 0.92f);
            else if (mode == 3)
                v = 0.5f + 0.38f * std::sin(12.0f * t + 2.1f * std::sin(19.0f * t));
            else
                v = 0.12f + 0.76f * std::abs(std::sin(8.0f * t) * std::sin(3.1f * t));
            const float px = x + t * w;
            const float py = y + (1.0f - v) * h;
            if (i == 0)
                moveTo(px, py);
            else
                lineTo(px, py);
        }
        stroke();
    }
};

UI* createUI() { return new DriftUI(); }

END_NAMESPACE_DISTRHO
