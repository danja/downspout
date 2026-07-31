#include "generative_panel_ui.hpp"
#include "polymeter_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class PolymeterUI : public GenerativePanelUI {
public:
    PolymeterUI()
        : GenerativePanelUI(
            "Polymeter",
            "See each Euclidean lane as a complete rhythm: pattern, playhead, note, and variation",
            downspout::polymeter::kParameterSpecs.data(),
            downspout::polymeter::kParameterCount, 226, 118, 92) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::polymeter;
        beginPanel();

        drawSection(24, 98, 1032, 46, "CLOCK");
        drawBeatSlider(kGrid, 602, 101, 210, "Step grid", "Quarter-note duration of one step.");
        drawSlider(kSeed, 820, 101, 222, "Seed", "", "Deterministic probability and drift identity.", 0);
        char status[80] {};
        std::snprintf(status, sizeof(status), "Step %d · %d events this block",
                      static_cast<int>(std::lround(value(kStatusStep))),
                      static_cast<int>(std::lround(value(kStatusEvents))));
        fillColor(176, 185, 180, 255);
        fontSize(11);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(96, 116, status, nullptr);

        for (int lane = 0; lane < kLaneCount; ++lane) {
            const float x = lane % 2 == 0 ? 24.0f : 547.0f;
            const float y = lane < 2 ? 155.0f : 445.0f;
            drawLane(lane, x, y, 509.0f, 276.0f);
        }
        endPanel();
    }

    void drawLane(const int lane, const float x, const float y, const float w, const float h)
    {
        using namespace downspout::polymeter;
        char title[24] {};
        std::snprintf(title, sizeof(title), "LANE %d", lane + 1);
        drawSection(x, y, w, h, title);
        drawPattern(lane, x + 113, y + 9, w - 127, 23);

        drawSlider(laneParam(lane, kLength), x + 14, y + 39, 152, "Length", " steps",
                   "Number of steps before this lane repeats.", 0);
        drawSlider(laneParam(lane, kPulses), x + 174, y + 39, 152, "Pulses", "",
                   "Evenly distributed active steps.", 0);
        drawSlider(laneParam(lane, kRotation), x + 334, y + 39, 161, "Rotation", " steps",
                   "Rotate the pattern around its cycle.", 0);
        drawPercentSlider(laneParam(lane, kProbability), x + 14, y + 95, 231, "Probability",
                          "Chance that an active step produces a note.");
        drawSlider(laneParam(lane, kRatchets), x + 253, y + 95, 242, "Ratchets", "",
                   "Repeated notes inside an active step.", 0);
        drawPercentSlider(laneParam(lane, kAccent), x + 14, y + 151, 231, "Accent",
                          "Velocity contrast between strong and regular hits.");
        drawPercentSlider(laneParam(lane, kPhaseDrift), x + 253, y + 151, 242, "Phase drift",
                          "Controlled deterministic timing drift.");
        drawNoteSlider(laneParam(lane, kNote), x + 14, y + 207, 231, "Note",
                       "MIDI note emitted by this lane.");
        drawChannelSlider(laneParam(lane, kChannel), x + 253, y + 207, 242, "Channel",
                          "MIDI output channel; channel 10 is conventional drums.");
    }

    void drawPattern(const int lane, const float x, const float y, const float w, const float h)
    {
        using namespace downspout::polymeter;
        const int length = std::clamp(static_cast<int>(std::lround(value(laneParam(lane, kLength)))), 1, 32);
        const int pulses = std::clamp(static_cast<int>(std::lround(value(laneParam(lane, kPulses)))), 0, length);
        const int rotation = static_cast<int>(std::lround(value(laneParam(lane, kRotation)))) % length;
        const int current = static_cast<int>(std::lround(value(kStatusStep))) % length;
        const float cell = w / static_cast<float>(length);
        for (int i = 0; i < length; ++i) {
            const int rotated = (i - rotation + length) % length;
            const bool hit = pulses > 0 && (rotated * pulses) % length < pulses;
            beginPath();
            if (i == current)
                fillColor(238, 238, 232, 255);
            else if (hit)
                fillColor(accentR(), accentG(), accentB(), 230);
            else
                fillColor(49, 57, 64, 255);
            roundedRect(x + i * cell + 1, y, std::max(2.0f, cell - 2), h, 2);
            fill();
        }
    }
};

UI* createUI() { return new PolymeterUI(); }

END_NAMESPACE_DISTRHO
