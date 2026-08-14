#include "DistrhoUI.hpp"

#include "lifeform_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

using downspout::lifeform::kCellCount;
using downspout::lifeform::kClockModeNames;
using downspout::lifeform::kEmitModeNames;
using downspout::lifeform::kGridHeight;
using downspout::lifeform::kGridWidth;
using downspout::lifeform::kOutputModeNames;
using downspout::lifeform::kParamBaseChannel;
using downspout::lifeform::kParamClear;
using downspout::lifeform::kParamClockMode;
using downspout::lifeform::kParamDensity;
using downspout::lifeform::kParamEmitMode;
using downspout::lifeform::kParamGate;
using downspout::lifeform::kParamLedFeedback;
using downspout::lifeform::kParamMutation;
using downspout::lifeform::kParamOutputMode;
using downspout::lifeform::kParamPanic;
using downspout::lifeform::kParamPassInput;
using downspout::lifeform::kParamRandomize;
using downspout::lifeform::kParamRootNote;
using downspout::lifeform::kParamRunning;
using downspout::lifeform::kParamScale;
using downspout::lifeform::kParamSeed;
using downspout::lifeform::kParamStatusActive;
using downspout::lifeform::kParamStatusCellStart;
using downspout::lifeform::kParamStatusGeneration;
using downspout::lifeform::kParamStep;
using downspout::lifeform::kParamVelocity;
using downspout::lifeform::kParameterCount;
using downspout::lifeform::kScaleNames;

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
    {kParamGate, "Gate", 0.0f, 1.0f, false},
    {kParamVelocity, "Vel", 0.0f, 1.0f, false},
    {kParamMutation, "Mut", 0.0f, 1.0f, false},
    {kParamDensity, "Dens", 0.0f, 1.0f, false},
    {kParamBaseChannel, "Ch", 1.0f, 16.0f, true},
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue) noexcept
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue) noexcept
{
    return std::max(minValue, std::min(value, maxValue));
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
    if (index == kParamEmitMode)
        return kEmitModeNames[static_cast<std::size_t>(clampi(selected, 0, static_cast<int>(kEmitModeNames.size()) - 1))];
    return "";
}

} // namespace

class LifeformUI : public UI
{
public:
    LifeformUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamRootNote] = 48.0f;
        values_[kParamGate] = 0.52f;
        values_[kParamVelocity] = 0.72f;
        values_[kParamDensity] = 0.34f;
        values_[kParamBaseChannel] = 4.0f;
        values_[kParamLedFeedback] = 1.0f;
        values_[kParamRunning] = 1.0f;
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

        if (runRect_.contains(x, y))
        {
            commitParameter(kParamRunning, values_[kParamRunning] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (stepRect_.contains(x, y))
        {
            triggerParameter(kParamStep);
            return true;
        }
        if (randomRect_.contains(x, y))
        {
            triggerParameter(kParamRandomize);
            return true;
        }
        if (clearRect_.contains(x, y))
        {
            triggerParameter(kParamClear);
            return true;
        }
        if (panicRect_.contains(x, y))
        {
            triggerParameter(kParamPanic);
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
    std::array<Rect, 4> selectorRects_ {};
    Rect runRect_ {};
    Rect stepRect_ {};
    Rect randomRect_ {};
    Rect clearRect_ {};
    Rect ledRect_ {};
    Rect panicRect_ {};
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
        fillColor(8, 14, 11, 255);
        fill();
        closePath();

        beginPath();
        rect(0.0f, 0.0f, width, 96.0f);
        fillColor(16, 31, 24, 255);
        fill();
        closePath();
    }

    void drawHeader(const float width)
    {
        fontSize(34.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(235, 250, 230, 255);
        text(28.0f, 22.0f, "Lifeform", nullptr);

        fontSize(13.0f);
        fillColor(158, 194, 170, 255);
        text(190.0f, 37.0f, "Conway cells breathing MIDI notes one beat at a time", nullptr);

        char buffer[96];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "cells %d | gen %d",
                      static_cast<int>(std::lround(values_[kParamStatusActive])),
                      static_cast<int>(std::lround(values_[kParamStatusGeneration])));
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(214, 232, 214, 255);
        text(width - 28.0f, 38.0f, buffer, nullptr);
    }

    void drawGrid()
    {
        const float x0 = 36.0f;
        const float y0 = 124.0f;
        const float cell = 58.0f;
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
                drawPad(rect, row, col, padValue(index) >= 0.5f);
            }
        }
    }

    void drawPad(const Rect& rect, const std::size_t row, const std::size_t col, const bool active)
    {
        int r = 18;
        int g = 27;
        int b = 22;
        if (active)
        {
            if (row < 2)
            {
                r = 71; g = 143; b = 221;
            }
            else if (row < 5)
            {
                r = 72; g = 188; b = 112;
            }
            else if (row < 7)
            {
                r = 222; g = 157; b = 65;
            }
            else
            {
                r = 173; g = 92; b = 215;
            }
        }

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(r, g, b, active ? 248 : 238);
        fill();
        strokeColor(active ? 236 : 46, active ? 247 : 62, active ? 214 : 50, active ? 205 : 255);
        strokeWidth(active ? 1.6f : 1.0f);
        stroke();
        closePath();

        if (active && ((static_cast<int>(row + col) % 3) == 0))
        {
            beginPath();
            circle(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, 5.0f);
            fillColor(245, 252, 230, 220);
            fill();
            closePath();
        }
    }

    void drawPanel()
    {
        const float x = 612.0f;
        const float y = 124.0f;
        const float w = 336.0f;
        const float h = 505.0f;

        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(14, 24, 19, 245);
        fill();
        strokeColor(51, 83, 62, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        drawSelectors(x + 22.0f, y + 22.0f, w - 44.0f);
        drawSliders(x + 22.0f, y + 134.0f, w - 44.0f);
        drawButtons(x + 22.0f, y + h - 92.0f, w - 44.0f);
    }

    void drawSelectors(const float x, const float y, const float w)
    {
        constexpr std::array<std::uint32_t, 4> kSelectorParams = {{
            kParamScale, kParamClockMode, kParamOutputMode, kParamEmitMode,
        }};
        constexpr std::array<const char*, 4> kSelectorLabels = {{
            "Scale", "Clock", "Output", "Emit",
        }};

        const float boxW = (w - 24.0f) / 4.0f;
        for (std::size_t i = 0; i < kSelectorParams.size(); ++i)
        {
            const Rect rect {x + static_cast<float>(i) * (boxW + 8.0f), y, boxW, 74.0f};
            selectorRects_[i] = rect;
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(157, 188, 166, 255);
            text(rect.x, rect.y, kSelectorLabels[i], nullptr);

            beginPath();
            roundedRect(rect.x, rect.y + 21.0f, rect.w, 35.0f, 6.0f);
            fillColor(28, 44, 35, 255);
            fill();
            strokeColor(77, 118, 88, 255);
            strokeWidth(1.0f);
            stroke();
            closePath();

            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(232, 246, 226, 255);
            text(rect.x + rect.w * 0.5f, rect.y + 39.0f, selectorName(kSelectorParams[i], values_[kSelectorParams[i]]), nullptr);
        }
    }

    void drawSliders(const float x, const float y, const float w)
    {
        const float sliderW = (w - 50.0f) / static_cast<float>(kSliders.size());
        for (std::size_t i = 0; i < kSliders.size(); ++i)
        {
            const Rect rect {x + static_cast<float>(i) * (sliderW + 10.0f), y, sliderW, 216.0f};
            sliderRects_[i] = rect;
            drawSlider(kSliders[i], rect);
        }
    }

    void drawSlider(const SliderDef& slider, const Rect& rect)
    {
        const float norm = clampf((values_[slider.index] - slider.min) / (slider.max - slider.min), 0.0f, 1.0f);
        const float trackX = rect.x + rect.w * 0.5f - 5.0f;
        const float trackY = rect.y + 28.0f;
        const float trackH = 140.0f;

        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(204, 224, 202, 255);
        text(rect.x + rect.w * 0.5f, rect.y, slider.label, nullptr);

        beginPath();
        roundedRect(trackX, trackY, 10.0f, trackH, 5.0f);
        fillColor(34, 48, 39, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(trackX, trackY + trackH * (1.0f - norm), 10.0f, trackH * norm, 5.0f);
        fillColor(89, 192, 122, 245);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 4.0f, trackY + trackH * (1.0f - norm) - 5.0f, rect.w - 8.0f, 10.0f, 5.0f);
        fillColor(237, 249, 227, 255);
        fill();
        closePath();

        char buffer[24];
        if (slider.integer)
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(values_[slider.index])));
        else
            std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(values_[slider.index] * 100.0f)));
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(139, 230, 166, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 180.0f, buffer, nullptr);
    }

    void drawButtons(const float x, const float y, const float w)
    {
        const float buttonW = (w - 24.0f) / 4.0f;
        const float rowGap = 8.0f;
        runRect_ = {x, y, buttonW, 38.0f};
        stepRect_ = {x + buttonW + 8.0f, y, buttonW, 38.0f};
        randomRect_ = {x + (buttonW + 8.0f) * 2.0f, y, buttonW, 38.0f};
        passRect_ = {x + (buttonW + 8.0f) * 3.0f, y, buttonW, 38.0f};
        clearRect_ = {x, y + 38.0f + rowGap, buttonW, 38.0f};
        panicRect_ = {x + buttonW + 8.0f, y + 38.0f + rowGap, buttonW, 38.0f};
        ledRect_ = {x + (buttonW + 8.0f) * 2.0f, y + 38.0f + rowGap, buttonW, 38.0f};
        drawButton(runRect_, values_[kParamRunning] >= 0.5f ? "Run" : "Stop", 72, 188, 112, values_[kParamRunning] >= 0.5f);
        drawButton(stepRect_, "Step", 82, 143, 221, false);
        drawButton(randomRect_, "Random", 173, 92, 215, false);
        drawButton(passRect_, values_[kParamPassInput] >= 0.5f ? "Pass" : "Block", 88, 176, 112, values_[kParamPassInput] >= 0.5f);
        drawButton(clearRect_, "Clear", 204, 76, 70, false);
        drawButton(panicRect_, "Panic", 232, 70, 58, false);
        drawButton(ledRect_, values_[kParamLedFeedback] >= 0.5f ? "LED" : "No LED", 75, 146, 214, values_[kParamLedFeedback] >= 0.5f);
    }

    void drawButton(const Rect& rect, const char* label, const int r, const int g, const int b, const bool active)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(active ? r : 27, active ? g : 42, active ? b : 33, 255);
        fill();
        strokeColor(r, g, b, active ? 245 : 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(236, 248, 228, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void cycleSelector(const std::size_t index)
    {
        const std::uint32_t parameter = index == 0 ? kParamScale :
                                        index == 1 ? kParamClockMode :
                                        index == 2 ? kParamOutputMode :
                                                     kParamEmitMode;
        const int count = index == 0 ? static_cast<int>(kScaleNames.size()) :
                          index == 1 ? static_cast<int>(kClockModeNames.size()) :
                          index == 2 ? static_cast<int>(kOutputModeNames.size()) :
                                       static_cast<int>(kEmitModeNames.size());
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
        const float height = 140.0f;
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
        setParameterValue(parameter, 0.0f);
        editParameter(parameter, false);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LifeformUI)
};

UI* createUI()
{
    return new LifeformUI();
}

END_NAMESPACE_DISTRHO
