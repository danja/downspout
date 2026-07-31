#include "generative_panel_ui.hpp"
#include "orbit_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class OrbitUI : public GenerativePanelUI {
public:
    OrbitUI()
        : GenerativePanelUI(
            "Orbit",
            "Choose a path, set its musical duration, and watch the stereo position move",
            downspout::orbit::kParameterSpecs.data(),
            downspout::orbit::kParameterCount, 102, 164, 220) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::orbit;
        static constexpr const char* trajectories[] {"Orbit", "Pendulum", "Random walk", "Figure eight"};
        beginPanel();

        drawSection(24, 104, 430, 266, "MOTION", "transport-aware trajectory");
        drawChoice(kTrajectory, 38, 137, 402, "Trajectory", trajectories, 4,
                   "Select the geometry used for stereo motion.");
        drawBeatSlider(kRate, 38, 196, 194, "Cycle length", "Musical duration of one complete path.");
        drawPercentSlider(kDepth, 240, 196, 200, "Motion depth", "Amount of left/right movement.");
        drawSlider(kWidth, 38, 252, 194, "Mid / side width", "x", "Stereo width multiplier.", 2);
        drawPercentSlider(kDistance, 240, 252, 200, "Distance", "Distance filtering and attenuation.");
        drawPercentSlider(kDoppler, 38, 308, 194, "Doppler", "Conservative short-delay pitch motion.");
        drawSlider(kSeed, 240, 308, 200, "Path seed", "", "Deterministic random-walk identity.", 0,
                   static_cast<int>(std::lround(value(kTrajectory))) == 2);

        drawSection(24, 382, 430, 249, "OUTPUT", "blend and live position");
        drawPercentSlider(kMix, 38, 415, 402, "Wet / dry", "Blend spatially processed and original audio.");
        drawMeter(kStatusPan, 38, 471, 194, "Pan position");
        drawMeter(kStatusDistance, 240, 471, 200, "Effective distance");
        char position[64] {};
        std::snprintf(position, sizeof(position), "%s %.0f%%",
                      value(kStatusPan) < -0.02f ? "Left" : value(kStatusPan) > 0.02f ? "Right" : "Centre",
                      std::abs(value(kStatusPan)) * 100.0f);
        drawReadout(38, 521, 194, 67, "Current position", position);
        char distance[32] {};
        std::snprintf(distance, sizeof(distance), "%.0f%%", value(kStatusDistance) * 100.0f);
        drawReadout(240, 521, 200, 67, "Current distance", distance);

        drawSection(468, 104, 468, 527, "TRAJECTORY VIEW", "ordinary stereo panning · no HRTF claim");
        drawTrajectory(492, 138, 420, 455);

        endPanel();
    }

    void drawTrajectory(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::orbit;
        const int mode = std::clamp(static_cast<int>(std::lround(value(kTrajectory))), 0, 3);
        beginPath();
        fillColor(17, 22, 27, 255);
        roundedRect(x, y, w, h, 7);
        fill();

        strokeColor(65, 75, 84, 255);
        strokeWidth(1);
        beginPath();
        moveTo(x + w * 0.5f, y + 18);
        lineTo(x + w * 0.5f, y + h - 18);
        moveTo(x + 18, y + h * 0.5f);
        lineTo(x + w - 18, y + h * 0.5f);
        stroke();

        beginPath();
        strokeColor(accentR(), accentG(), accentB(), 205);
        strokeWidth(2.2f);
        for (int i = 0; i <= 120; ++i) {
            const float t = static_cast<float>(i) / 120.0f * 6.2831853f;
            float px = 0.0f;
            float py = 0.0f;
            if (mode == 0) {
                px = std::cos(t);
                py = std::sin(t);
            } else if (mode == 1) {
                px = std::sin(t);
                py = -0.65f + 0.28f * std::cos(2.0f * t);
            } else if (mode == 2) {
                px = 0.62f * std::sin(t) + 0.22f * std::sin(3.7f * t);
                py = 0.55f * std::sin(1.31f * t + 1.2f);
            } else {
                px = std::sin(t);
                py = std::sin(t) * std::cos(t);
            }
            const float sx = x + w * 0.5f + px * w * 0.39f;
            const float sy = y + h * 0.5f - py * h * 0.36f;
            if (i == 0)
                moveTo(sx, sy);
            else
                lineTo(sx, sy);
        }
        stroke();

        const float pan = std::clamp(value(kStatusPan), -1.0f, 1.0f);
        const float distance = std::clamp(value(kStatusDistance), 0.0f, 1.0f);
        const float dotX = x + w * 0.5f + pan * w * 0.39f;
        float pathY = 0.0f;
        if (mode == 0)
            pathY = std::sqrt(std::max(0.0f, 1.0f - pan * pan));
        else if (mode == 1)
            pathY = -0.65f + 0.28f * (1.0f - 2.0f * pan * pan);
        else if (mode == 2)
            pathY = 0.45f * std::sin(pan * 3.1f);
        else
            pathY = pan * std::sqrt(std::max(0.0f, 1.0f - pan * pan));
        const float dotY = y + h * 0.5f - pathY * h * 0.36f;
        beginPath();
        fillColor(239, 242, 238, 255);
        circle(dotX, dotY, 9);
        fill();
        beginPath();
        strokeColor(accentR(), accentG(), accentB(), 255);
        strokeWidth(3);
        circle(dotX, dotY, 12.0f + distance * 5.0f);
        stroke();

        fillColor(130, 143, 151, 255);
        fontSize(10);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(x + 12, y + h - 9, "LEFT", nullptr);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        text(x + w * 0.5f, y + h - 9, "CENTRE", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(x + w - 12, y + h - 9, "RIGHT", nullptr);
    }
};

UI* createUI() { return new OrbitUI(); }

END_NAMESPACE_DISTRHO
