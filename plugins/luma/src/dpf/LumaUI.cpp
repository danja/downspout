#include "DistrhoUI.hpp"

#include "luma_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

using downspout::luma::kCellCount;
using downspout::luma::kClockModeNames;
using downspout::luma::kGridHeight;
using downspout::luma::kGridWidth;
using downspout::luma::kOutputModeNames;
using downspout::luma::kParamBaseChannel;
using downspout::luma::kParamClear;
using downspout::luma::kParamClockMode;
using downspout::luma::kParamDensity;
using downspout::luma::kParamEnergy;
using downspout::luma::kParamGate;
using downspout::luma::kParamLedFeedback;
using downspout::luma::kParamOutputMode;
using downspout::luma::kParamPassInput;
using downspout::luma::kParamRandomize;
using downspout::luma::kParamRootNote;
using downspout::luma::kParamScale;
using downspout::luma::kParamStatusActive;
using downspout::luma::kParamStatusCellStart;
using downspout::luma::kParamStatusStep;
using downspout::luma::kParamSwing;
using downspout::luma::kParameterCount;
using downspout::luma::kScaleNames;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct SliderDef {
    std::uint32_t index;
    const char* label;
    float min;
    float max;
    bool integer;
};

constexpr std::array<SliderDef, 6> kSliders = {{
    {kParamRootNote, "Root", 0.0f, 127.0f, true},
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamEnergy, "Energy", 0.0f, 1.0f, false},
    {kParamGate, "Gate", 0.0f, 1.0f, false},
    {kParamSwing, "Swing", 0.0f, 1.0f, false},
    {kParamBaseChannel, "Channel", 1.0f, 16.0f, true},
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue) noexcept
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue) noexcept
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const char* noteName(const int midi) noexcept
{
    static constexpr const char* kNames[] = {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return kNames[(midi % 12 + 12) % 12];
}

[[nodiscard]] const char* selectorName(const std::uint32_t index, const float value)
{
    const int selected = static_cast<int>(std::lround(value));
    if (index == kParamScale)
        return kScaleNames[static_cast<std::size_t>(clampi(selected, 0, static_cast<int>(kScaleNames.size()) - 1))];
    if (index == kParamClockMode)
        return kClockModeNames[static_cast<std::size_t>(clampi(selected, 0, static_cast<int>(kClockModeNames.size()) - 1))];
    if (index == kParamOutputMode)
        return kOutputModeNames[static_cast<std::size_t>(clampi(selected, 0, static_cast<int>(kOutputModeNames.size()) - 1))];
    return "";
}

} // namespace

class LumaUI : public UI
{
public:
    LumaUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamRootNote] = 48.0f;
        values_[kParamScale] = 1.0f;
        values_[kParamDensity] = 0.42f;
        values_[kParamEnergy] = 0.48f;
        values_[kParamGate] = 0.45f;
        values_[kParamClockMode] = 0.0f;
        values_[kParamOutputMode] = 0.0f;
        values_[kParamBaseChannel] = 1.0f;
        values_[kParamLedFeedback] = 1.0f;
        values_[kParamPassInput] = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < values_.size())
        {
            values_[index] = value;
            touched_[index] = true;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        drawBackground(width, height);
        drawHeader(width);
        drawGrid();
        drawPanel();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press)
        {
            activeSlider_ = -1;
            return false;
        }

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        for (std::size_t i = 0; i < padRects_.size(); ++i)
        {
            if (padRects_[i].contains(x, y))
            {
                commitParameter(static_cast<std::uint32_t>(i), values_[i] >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i)
        {
            if (selectorRects_[i].contains(x, y))
            {
                cycleSelector(i);
                return true;
            }
        }

        if (scatterRect_.contains(x, y))
        {
            triggerParameter(kParamRandomize);
            return true;
        }
        if (clearRect_.contains(x, y))
        {
            triggerParameter(kParamClear);
            return true;
        }
        if (ledRect_.contains(x, y))
        {
            commitParameter(kParamLedFeedback, values_[kParamLedFeedback] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (passRect_.contains(x, y))
        {
            commitParameter(kParamPassInput, values_[kParamPassInput] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i)
        {
            if (sliderRects_[i].contains(x, y))
            {
                activeSlider_ = static_cast<int>(i);
                updateSlider(i, y);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0)
            return false;

        updateSlider(static_cast<std::size_t>(activeSlider_), static_cast<float>(ev.pos.getY()));
        return true;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<bool, kParameterCount> touched_ {};
    std::array<Rect, kCellCount> padRects_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    std::array<Rect, 3> selectorRects_ {};
    Rect scatterRect_ {};
    Rect clearRect_ {};
    Rect ledRect_ {};
    Rect passRect_ {};
    int activeSlider_ = -1;

    [[nodiscard]] float padValue(const std::size_t index) const noexcept
    {
        const std::size_t statusIndex = kParamStatusCellStart + index;
        if (statusIndex < touched_.size() && touched_[statusIndex])
            return values_[statusIndex];
        return values_[index];
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        rect(0.0f, 0.0f, width, height);
        fillColor(11, 13, 15, 255);
        fill();
        closePath();

        beginPath();
        rect(0.0f, 0.0f, width, 96.0f);
        fillColor(20, 25, 29, 255);
        fill();
        closePath();
    }

    void drawHeader(const float width)
    {
        fontSize(34.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(240, 245, 240, 255);
        text(28.0f, 22.0f, "Luma", nullptr);

        fontSize(13.0f);
        fillColor(163, 174, 176, 255);
        text(136.0f, 37.0f, "Launchpad pad agents for bass, chords, melody, and drum sparks", nullptr);

        char buffer[96];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "active %d | step %d",
                      static_cast<int>(std::lround(values_[kParamStatusActive])),
                      static_cast<int>(std::lround(values_[kParamStatusStep])));
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(216, 225, 218, 255);
        text(width - 28.0f, 38.0f, buffer, nullptr);
    }

    void drawGrid()
    {
        const float x0 = 34.0f;
        const float y0 = 128.0f;
        const float cell = 57.0f;
        const float gap = 9.0f;

        for (std::size_t visualRow = 0; visualRow < kGridHeight; ++visualRow)
        {
            const std::size_t row = kGridHeight - 1u - visualRow;
            for (std::size_t col = 0; col < kGridWidth; ++col)
            {
                const std::size_t index = row * kGridWidth + col;
                const Rect rect {x0 + static_cast<float>(col) * (cell + gap),
                                 y0 + static_cast<float>(visualRow) * (cell + gap),
                                 cell,
                                 cell};
                padRects_[index] = rect;
                drawPad(rect, row, padValue(index) >= 0.5f);
            }
        }
    }

    void drawPad(const Rect& rect, const std::size_t row, const bool active)
    {
        int r = 25;
        int g = 31;
        int b = 35;
        if (active)
        {
            if (row < 2)
            {
                r = 67; g = 126; b = 210;
            }
            else if (row < 4)
            {
                r = 210; g = 150; b = 61;
            }
            else if (row < 6)
            {
                r = 75; g = 174; b = 115;
            }
            else
            {
                r = 205; g = 72; b = 64;
            }
        }

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(r, g, b, active ? 245 : 235);
        fill();
        strokeColor(active ? 236 : 58, active ? 241 : 67, active ? 230 : 72, active ? 190 : 255);
        strokeWidth(active ? 1.4f : 1.0f);
        stroke();
        closePath();
    }

    void drawPanel()
    {
        const float x = 610.0f;
        const float y = 128.0f;
        const float w = 336.0f;
        const float h = 498.0f;

        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(18, 22, 25, 245);
        fill();
        strokeColor(50, 61, 66, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        drawSelectors(x + 22.0f, y + 22.0f, w - 44.0f);
        drawSliders(x + 22.0f, y + 136.0f, w - 44.0f);
        drawButtons(x + 22.0f, y + h - 76.0f, w - 44.0f);
    }

    void drawSelectors(const float x, const float y, const float w)
    {
        constexpr std::array<std::uint32_t, 3> kSelectorParams = {{
            kParamScale, kParamClockMode, kParamOutputMode,
        }};
        constexpr std::array<const char*, 3> kSelectorLabels = {{
            "Scale", "Clock", "Output",
        }};

        const float boxW = (w - 16.0f) / 3.0f;
        for (std::size_t i = 0; i < kSelectorParams.size(); ++i)
        {
            const Rect rect {x + static_cast<float>(i) * (boxW + 8.0f), y, boxW, 74.0f};
            selectorRects_[i] = rect;

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(167, 178, 179, 255);
            text(rect.x, rect.y, kSelectorLabels[i], nullptr);

            beginPath();
            roundedRect(rect.x, rect.y + 21.0f, rect.w, 35.0f, 6.0f);
            fillColor(31, 38, 42, 255);
            fill();
            strokeColor(77, 99, 105, 255);
            strokeWidth(1.0f);
            stroke();
            closePath();

            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(238, 243, 238, 255);
            text(rect.x + rect.w * 0.5f,
                 rect.y + 39.0f,
                 selectorName(kSelectorParams[i], values_[kSelectorParams[i]]),
                 nullptr);
        }
    }

    void drawSliders(const float x, const float y, const float w)
    {
        const float sliderW = (w - 48.0f) / static_cast<float>(kSliders.size());
        for (std::size_t i = 0; i < kSliders.size(); ++i)
        {
            const SliderDef& slider = kSliders[i];
            const Rect rect {x + static_cast<float>(i) * (sliderW + 12.0f), y, sliderW, 214.0f};
            sliderRects_[i] = rect;
            drawSlider(slider, rect);
        }
    }

    void drawSlider(const SliderDef& slider, const Rect& rect)
    {
        const float norm = clampf((values_[slider.index] - slider.min) / (slider.max - slider.min), 0.0f, 1.0f);
        const float trackX = rect.x + rect.w * 0.5f - 5.0f;
        const float trackY = rect.y + 28.0f;
        const float trackH = 138.0f;

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(201, 211, 209, 255);
        text(rect.x + rect.w * 0.5f, rect.y, slider.label, nullptr);

        beginPath();
        roundedRect(trackX, trackY, 10.0f, trackH, 5.0f);
        fillColor(36, 43, 47, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(trackX, trackY + trackH * (1.0f - norm), 10.0f, trackH * norm, 5.0f);
        fillColor(76, 160, 136, 245);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 5.0f, trackY + trackH * (1.0f - norm) - 5.0f, rect.w - 10.0f, 10.0f, 5.0f);
        fillColor(238, 244, 238, 255);
        fill();
        closePath();

        char buffer[32];
        if (slider.index == kParamRootNote)
        {
            const int midi = static_cast<int>(std::lround(values_[slider.index]));
            std::snprintf(buffer, sizeof(buffer), "%s%d", noteName(midi), midi / 12 - 1);
        }
        else if (slider.integer)
        {
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(values_[slider.index])));
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(values_[slider.index] * 100.0f)));
        }

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(131, 222, 188, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 178.0f, buffer, nullptr);
    }

    void drawButtons(const float x, const float y, const float w)
    {
        const float buttonW = (w - 24.0f) / 4.0f;
        scatterRect_ = {x, y, buttonW, 42.0f};
        clearRect_ = {x + buttonW + 8.0f, y, buttonW, 42.0f};
        ledRect_ = {x + (buttonW + 8.0f) * 2.0f, y, buttonW, 42.0f};
        passRect_ = {x + (buttonW + 8.0f) * 3.0f, y, buttonW, 42.0f};
        drawButton(scatterRect_, "Scatter", 211, 151, 66, false);
        drawButton(clearRect_, "Clear", 206, 80, 74, false);
        drawButton(ledRect_, values_[kParamLedFeedback] >= 0.5f ? "LED On" : "LED Off", 78, 147, 210, values_[kParamLedFeedback] >= 0.5f);
        drawButton(passRect_, values_[kParamPassInput] >= 0.5f ? "Pass" : "Block", 83, 166, 113, values_[kParamPassInput] >= 0.5f);
    }

    void drawButton(const Rect& rect, const char* label, const int r, const int g, const int b, const bool active)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(active ? r : 31, active ? g : 38, active ? b : 42, 255);
        fill();
        strokeColor(r, g, b, active ? 245 : 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(239, 244, 238, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void cycleSelector(const std::size_t index)
    {
        const std::uint32_t parameter = index == 0 ? kParamScale : (index == 1 ? kParamClockMode : kParamOutputMode);
        const int count = index == 0 ? static_cast<int>(kScaleNames.size()) :
                          index == 1 ? static_cast<int>(kClockModeNames.size()) :
                                       static_cast<int>(kOutputModeNames.size());
        const int current = static_cast<int>(std::lround(values_[parameter]));
        commitParameter(parameter, static_cast<float>((current + 1) % count));
    }

    void updateSlider(const std::size_t sliderIndex, const float y)
    {
        if (sliderIndex >= kSliders.size())
            return;

        const SliderDef& slider = kSliders[sliderIndex];
        const Rect& rect = sliderRects_[sliderIndex];
        const float top = rect.y + 28.0f;
        const float height = 138.0f;
        const float norm = 1.0f - clampf((y - top) / height, 0.0f, 1.0f);
        float value = slider.min + norm * (slider.max - slider.min);
        if (slider.integer)
            value = std::round(value);
        commitParameter(slider.index, value);
    }

    void triggerParameter(const std::uint32_t parameter)
    {
        editParameter(parameter, true);
        setParameterValue(parameter, 1.0f);
        editParameter(parameter, false);
        setParameterValue(parameter, 0.0f);
        repaint();
    }

    void commitParameter(const std::uint32_t parameter, const float value)
    {
        editParameter(parameter, true);
        setParameterValue(parameter, value);
        editParameter(parameter, false);
        values_[parameter] = value;
        if (parameter < kCellCount)
        {
            const std::size_t statusIndex = kParamStatusCellStart + parameter;
            values_[statusIndex] = value;
            touched_[statusIndex] = true;
        }
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LumaUI)
};

UI* createUI()
{
    return new LumaUI();
}

END_NAMESPACE_DISTRHO
