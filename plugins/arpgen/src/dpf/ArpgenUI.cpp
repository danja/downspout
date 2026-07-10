#include "DistrhoUI.hpp"

#include "arpgen_core.hpp"
#include "arpgen_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::arpgen;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct Choice {
    std::uint32_t parameter;
    const char* label;
    const char* const* values;
    int count;
    float minimum;
};

struct Slider {
    std::uint32_t parameter;
    const char* label;
    float minimum;
    float maximum;
};

constexpr const char* kModes[] = {"CHORD", "SCALE"};
constexpr const char* kOrders[] = {"Up", "Down", "Up / Down", "Down / Up"};
constexpr const char* kRates[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
constexpr const char* kSlices[] = {"Quarter bar", "Half bar", "Whole bar"};
constexpr const char* kKeys[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr const char* kScales[] = {
    "Major", "Natural minor", "Harmonic minor", "Dorian", "Mixolydian",
    "Pentatonic major", "Pentatonic minor", "Blues", "Lydian", "Phrygian dominant"
};
constexpr const char* kShapes[] = {"Scale run", "Triad stack", "Seventh stack"};
constexpr const char* kOctaves[] = {"1", "2", "3", "4"};
constexpr const char* kPass[] = {"Off", "On"};
constexpr const char* kChannels[] = {
    "Follow input", "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr std::array<Choice, 7> kChoices {{
    {kParamOrder, "ORDER", kOrders, 4, 0.0f},
    {kParamRate, "RATE", kRates, 6, 0.0f},
    {kParamCaptureSlice, "CAPTURE", kSlices, 3, 0.0f},
    {kParamKey, "KEY", kKeys, 12, 0.0f},
    {kParamScale, "SCALE", kScales, SCALE_COUNT, 0.0f},
    {kParamScaleShape, "MATERIAL", kShapes, 3, 0.0f},
    {kParamOctaves, "OCTAVES", kOctaves, 4, 1.0f},
}};

constexpr std::array<Slider, 2> kSliders {{
    {kParamGate, "GATE", 0.05f, 1.0f},
    {kParamVelocityFollow, "VELOCITY FOLLOW", 0.0f, 1.0f},
}};

float clampf(const float value, const float low, const float high)
{
    return std::max(low, std::min(value, high));
}

int clampi(const int value, const int low, const int high)
{
    return std::max(low, std::min(value, high));
}

}  // namespace

class ArpgenUI : public UI
{
public:
    ArpgenUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamOrder] = ORDER_UP_DOWN;
        values_[kParamRate] = RATE_SIXTEENTH;
        values_[kParamOctaves] = 2.0f;
        values_[kParamGate] = 0.72f;
        values_[kParamVelocityFollow] = 0.8f;
        values_[kParamStatusNote] = -1.0f;
#ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
#else
        loadSharedResources();
#endif
    }

protected:
    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index < values_.size()) {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        drawBackground(width, height);
        drawHeader(width);
        drawModeSwitch(28.0f, 106.0f, width - 56.0f, 46.0f);
        drawTimingBand(28.0f, 178.0f, width - 56.0f, 100.0f);
        drawSourceBand(28.0f, 304.0f, width - 56.0f, 116.0f);
        drawPerformanceBand(28.0f, 446.0f, width - 56.0f, height - 470.0f);
    }

    bool onMouse(const MouseEvent& event) override
    {
        if (event.button != 1)
            return false;
        if (!event.press) {
            dragging_ = -1;
            return true;
        }
        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        for (int i = 0; i < 2; ++i) {
            if (modeRects_[static_cast<std::size_t>(i)].contains(x, y)) {
                setValue(kParamMode, static_cast<float>(i));
                return true;
            }
        }
        for (std::size_t i = 0; i < choiceRects_.size(); ++i) {
            if (choiceRects_[i].contains(x, y)) {
                cycleChoice(static_cast<int>(i), 1);
                return true;
            }
        }
        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                dragging_ = static_cast<int>(i);
                updateSlider(dragging_, x);
                return true;
            }
        }
        if (passRect_.contains(x, y)) {
            setValue(kParamPassInput, values_[kParamPassInput] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (channelRect_.contains(x, y)) {
            const int next = (static_cast<int>(std::lround(values_[kParamOutputChannel])) + 1) % 17;
            setValue(kParamOutputChannel, static_cast<float>(next));
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& event) override
    {
        if (dragging_ < 0)
            return false;
        updateSlider(dragging_, static_cast<float>(event.pos.getX()));
        return true;
    }

    bool onScroll(const ScrollEvent& event) override
    {
        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        const int direction = event.delta.getY() > 0.0f ? -1 : 1;
        for (std::size_t i = 0; i < choiceRects_.size(); ++i) {
            if (choiceRects_[i].contains(x, y)) {
                cycleChoice(static_cast<int>(i), direction);
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, 2> modeRects_ {};
    std::array<Rect, kChoices.size()> choiceRects_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    Rect passRect_ {};
    Rect channelRect_ {};
    int dragging_ = -1;

    void setValue(const std::uint32_t parameter, const float value)
    {
        values_[parameter] = value;
        setParameterValue(parameter, value);
        repaint();
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        rect(0.0f, 0.0f, width, height);
        fillColor(16, 19, 22, 255);
        fill();
        closePath();
        beginPath();
        rect(0.0f, 0.0f, width, 82.0f);
        fillColor(27, 32, 36, 255);
        fill();
        closePath();
    }

    void drawHeader(const float width)
    {
        fontSize(27.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(242, 244, 239, 255);
        text(28.0f, 36.0f, "ARPGEN", nullptr);
        fontSize(12.0f);
        fillColor(147, 158, 162, 255);
        text(28.0f, 61.0f, "TRANSPORT ARPEGGIATOR", nullptr);

        const float activity = std::max(values_[kParamStatusInput], values_[kParamStatusOutput]);
        beginPath();
        circle(width - 38.0f, 40.0f, 6.0f);
        fillColor(activity > 0.02f ? 96 : 55, activity > 0.02f ? 211 : 67,
                  activity > 0.02f ? 154 : 70, 255);
        fill();
        closePath();
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(161, 171, 174, 255);
        char status[64];
        std::snprintf(status, sizeof(status), "%d NOTES", static_cast<int>(std::lround(values_[kParamStatusMaterial])));
        text(width - 58.0f, 40.0f, status, nullptr);
    }

    void label(const float x, const float y, const char* value)
    {
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(128, 142, 147, 255);
        text(x, y, value, nullptr);
    }

    void drawModeSwitch(const float x, const float y, const float w, const float h)
    {
        const int selected = clampi(static_cast<int>(std::lround(values_[kParamMode])), 0, 1);
        const float segmentW = w * 0.5f;
        for (int i = 0; i < 2; ++i) {
            const Rect rectValue {x + segmentW * i, y, segmentW, h};
            modeRects_[static_cast<std::size_t>(i)] = rectValue;
            beginPath();
            rect(rectValue.x, rectValue.y, rectValue.w, rectValue.h);
            fillColor(i == selected ? 205 : 35, i == selected ? 222 : 42, i == selected ? 105 : 46, 255);
            fill();
            strokeColor(63, 72, 75, 255);
            strokeWidth(1.0f);
            stroke();
            closePath();
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(i == selected ? 17 : 180, i == selected ? 21 : 188, i == selected ? 20 : 190, 255);
            text(rectValue.x + rectValue.w * 0.5f, rectValue.y + rectValue.h * 0.5f, kModes[i], nullptr);
        }
    }

    void drawBand(const float x, const float y, const float w, const float h, const char* title)
    {
        beginPath();
        roundedRect(x, y, w, h, 6.0f);
        fillColor(24, 28, 31, 255);
        fill();
        strokeColor(45, 52, 55, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();
        label(x + 16.0f, y + 12.0f, title);
    }

    void drawChoice(const int choiceIndex, const Rect& rectValue)
    {
        const auto& choice = kChoices[static_cast<std::size_t>(choiceIndex)];
        choiceRects_[static_cast<std::size_t>(choiceIndex)] = rectValue;
        label(rectValue.x, rectValue.y, choice.label);
        beginPath();
        roundedRect(rectValue.x, rectValue.y + 18.0f, rectValue.w, rectValue.h - 18.0f, 5.0f);
        fillColor(35, 41, 44, 255);
        fill();
        closePath();
        const int index = clampi(static_cast<int>(std::lround(values_[choice.parameter] - choice.minimum)), 0, choice.count - 1);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(228, 231, 224, 255);
        text(rectValue.x + 12.0f, rectValue.y + 18.0f + (rectValue.h - 18.0f) * 0.5f,
             choice.values[index], nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(126, 219, 179, 255);
        text(rectValue.x + rectValue.w - 11.0f, rectValue.y + 18.0f + (rectValue.h - 18.0f) * 0.5f, ">", nullptr);
    }

    void drawTimingBand(const float x, const float y, const float w, const float h)
    {
        drawBand(x, y, w, h, "PATTERN");
        const float gap = 14.0f;
        const float cellW = (w - 32.0f - gap) * 0.5f;
        drawChoice(0, {x + 16.0f, y + 35.0f, cellW, 52.0f});
        drawChoice(1, {x + 16.0f + cellW + gap, y + 35.0f, cellW, 52.0f});
    }

    void drawSourceBand(const float x, const float y, const float w, const float h)
    {
        const bool chord = values_[kParamMode] < 0.5f;
        drawBand(x, y, w, h, chord ? "CHORD SOURCE" : "SCALE SOURCE");
        const float gap = 12.0f;
        if (chord) {
            const float cellW = (w - 32.0f - gap) * 0.5f;
            drawChoice(2, {x + 16.0f, y + 38.0f, cellW, 58.0f});
            drawChoice(6, {x + 16.0f + cellW + gap, y + 38.0f, cellW, 58.0f});
            choiceRects_[3] = {}; choiceRects_[4] = {}; choiceRects_[5] = {};
        } else {
            const float cellW = (w - 32.0f - gap * 3.0f) * 0.25f;
            drawChoice(3, {x + 16.0f, y + 38.0f, cellW, 58.0f});
            drawChoice(4, {x + 16.0f + (cellW + gap), y + 38.0f, cellW, 58.0f});
            drawChoice(5, {x + 16.0f + (cellW + gap) * 2.0f, y + 38.0f, cellW, 58.0f});
            drawChoice(6, {x + 16.0f + (cellW + gap) * 3.0f, y + 38.0f, cellW, 58.0f});
            choiceRects_[2] = {};
        }
    }

    void drawSlider(const int index, const Rect& rectValue)
    {
        const auto& slider = kSliders[static_cast<std::size_t>(index)];
        sliderRects_[static_cast<std::size_t>(index)] = rectValue;
        label(rectValue.x, rectValue.y, slider.label);
        const float amount = clampf((values_[slider.parameter] - slider.minimum) /
                                    (slider.maximum - slider.minimum), 0.0f, 1.0f);
        beginPath();
        roundedRect(rectValue.x, rectValue.y + 24.0f, rectValue.w, 12.0f, 5.0f);
        fillColor(43, 49, 51, 255);
        fill();
        closePath();
        beginPath();
        roundedRect(rectValue.x, rectValue.y + 24.0f, rectValue.w * amount, 12.0f, 5.0f);
        fillColor(126, 219, 179, 255);
        fill();
        closePath();
        char value[16];
        std::snprintf(value, sizeof(value), "%d%%", static_cast<int>(std::lround(values_[slider.parameter] * 100.0f)));
        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(206, 211, 205, 255);
        text(rectValue.x + rectValue.w, rectValue.y, value, nullptr);
    }

    void drawSmallChoice(const Rect& rectValue, const char* title, const char* value, const bool active)
    {
        label(rectValue.x, rectValue.y, title);
        beginPath();
        roundedRect(rectValue.x, rectValue.y + 18.0f, rectValue.w, rectValue.h - 18.0f, 5.0f);
        fillColor(active ? 43 : 35, active ? 68 : 41, active ? 57 : 44, 255);
        fill();
        closePath();
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? 126 : 211, active ? 219 : 216, active ? 179 : 211, 255);
        text(rectValue.x + rectValue.w * 0.5f, rectValue.y + 18.0f + (rectValue.h - 18.0f) * 0.5f, value, nullptr);
    }

    void drawPerformanceBand(const float x, const float y, const float w, const float h)
    {
        drawBand(x, y, w, h, "PERFORMANCE");
        const float contentY = y + 42.0f;
        const float sliderW = w * 0.25f;
        drawSlider(0, {x + 16.0f, contentY, sliderW, 42.0f});
        drawSlider(1, {x + 32.0f + sliderW, contentY, sliderW, 42.0f});
        passRect_ = {x + w - 326.0f, contentY, 132.0f, 50.0f};
        channelRect_ = {x + w - 180.0f, contentY, 164.0f, 50.0f};
        drawSmallChoice(passRect_, "PASS INPUT", kPass[values_[kParamPassInput] >= 0.5f ? 1 : 0], values_[kParamPassInput] >= 0.5f);
        const int channel = clampi(static_cast<int>(std::lround(values_[kParamOutputChannel])), 0, 16);
        drawSmallChoice(channelRect_, "OUTPUT", kChannels[channel], false);

        const float keyboardY = y + h - 32.0f;
        const float keyW = (w - 32.0f) / 12.0f;
        const int activePitch = static_cast<int>(std::lround(values_[kParamStatusNote]));
        for (int pitch = 0; pitch < 12; ++pitch) {
            beginPath();
            rect(x + 16.0f + keyW * pitch, keyboardY, keyW - 2.0f, 5.0f);
            const bool active = activePitch >= 0 && activePitch % 12 == pitch;
            fillColor(active ? 205 : 62, active ? 222 : 70, active ? 105 : 72, 255);
            fill();
            closePath();
        }
    }

    void cycleChoice(const int choiceIndex, const int direction)
    {
        const auto& choice = kChoices[static_cast<std::size_t>(choiceIndex)];
        int current = static_cast<int>(std::lround(values_[choice.parameter] - choice.minimum));
        current = (current + direction + choice.count) % choice.count;
        setValue(choice.parameter, static_cast<float>(current) + choice.minimum);
    }

    void updateSlider(const int sliderIndex, const float x)
    {
        const auto& rectValue = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const auto& slider = kSliders[static_cast<std::size_t>(sliderIndex)];
        const float amount = clampf((x - rectValue.x) / std::max(1.0f, rectValue.w), 0.0f, 1.0f);
        setValue(slider.parameter, slider.minimum + amount * (slider.maximum - slider.minimum));
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArpgenUI)
};

UI* createUI() { return new ArpgenUI(); }

END_NAMESPACE_DISTRHO
