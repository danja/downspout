#include "DistrhoUI.hpp"

#include "paunchlad_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

using downspout::paunchlad::kCellCount;
using downspout::paunchlad::kControlParamSpecs;
using downspout::paunchlad::kGridHeight;
using downspout::paunchlad::kGridWidth;
using downspout::paunchlad::gridToNote;
using downspout::paunchlad::unapplyPadMap;
using downspout::paunchlad::kParamDry;
using downspout::paunchlad::kParamLedFeedback;
using downspout::paunchlad::kParamPadMap;
using downspout::paunchlad::kParamPanic;
using downspout::paunchlad::kParamStatusActivity;
using downspout::paunchlad::kParamStatusCellStart;
using downspout::paunchlad::kParamStatusMode;
using downspout::paunchlad::kParameterCount;

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
};

constexpr std::array<SliderDef, 7> kSliders = {{
    {64, "Dry"},
    {65, "Echo"},
    {66, "Fbk"},
    {67, "Tone"},
    {68, "Siren"},
    {69, "Spring"},
    {70, "Out"},
}};

constexpr std::array<const char*, 8> kRowNames = {{
    "Echo",
    "Chop",
    "Drop",
    "Spring",
    "Snare",
    "Siren",
    "Alarm",
    "Panic",
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue) noexcept
{
    return std::max(minValue, std::min(value, maxValue));
}

} // namespace

class PaunchLadUI : public UI
{
public:
    PaunchLadUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        for (std::size_t i = 0; i < kControlParamSpecs.size(); ++i)
            values_[kParamDry + i] = kControlParamSpecs[i].defaultValue;

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
            repaint();
        }
    }

    void uiIdle() override
    {
        bool changed = false;
        for (std::size_t i = kParamStatusCellStart; i < kParamStatusCellStart + kCellCount; ++i)
        {
            if (values_[i] > 0.0f)
            {
                values_[i] = std::max(0.0f, values_[i] - 0.08f);
                changed = true;
            }
        }
        if (values_[kParamStatusActivity] > 0.0f)
        {
            values_[kParamStatusActivity] = std::max(0.0f, values_[kParamStatusActivity] - 0.04f);
            changed = true;
        }
        if (changed)
            repaint();
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
                triggerParameter(static_cast<std::uint32_t>(i));
                const std::size_t statusIndex = kParamStatusCellStart + i;
                values_[statusIndex] = 1.0f;
                repaint();
                return true;
            }
        }

        if (panicRect_.contains(x, y))
        {
            triggerParameter(kParamPanic);
            return true;
        }

        if (ledRect_.contains(x, y))
        {
            setParameter(kParamLedFeedback, values_[kParamLedFeedback] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }

        if (mapRect_.contains(x, y))
        {
            setParameter(kParamPadMap, static_cast<float>((static_cast<int>(std::lround(values_[kParamPadMap])) + 1) % 4));
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
    std::array<Rect, kCellCount> padRects_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    Rect panicRect_ {};
    Rect ledRect_ {};
    Rect mapRect_ {};
    int activeSlider_ = -1;

    [[nodiscard]] float padValue(const std::size_t index) const noexcept
    {
        const std::size_t statusIndex = kParamStatusCellStart + index;
        if (statusIndex < values_.size() && values_[statusIndex] > 0.02f)
            return values_[statusIndex];
        return values_[index];
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        rect(0.0f, 0.0f, width, height);
        fillColor(12, 10, 9, 255);
        fill();
        closePath();

        beginPath();
        rect(0.0f, 0.0f, width, 100.0f);
        fillColor(32, 24, 18, 255);
        fill();
        closePath();
    }

    void drawHeader(const float width)
    {
        fontSize(34.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(249, 239, 218, 255);
        text(28.0f, 22.0f, "PaunchLad", nullptr);

        fontSize(13.0f);
        fillColor(206, 184, 140, 255);
        text(202.0f, 38.0f, "dub sirens, snare throws, spring splashes, dropouts, and tape chops", nullptr);

        char buffer[96];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "activity %d%% | row %d",
                      static_cast<int>(std::lround(values_[kParamStatusActivity] * 100.0f)),
                      static_cast<int>(std::lround(values_[kParamStatusMode])) + 1);
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(236, 225, 205, 255);
        text(width - 28.0f, 40.0f, buffer, nullptr);
    }

    void drawGrid()
    {
        const float x0 = 34.0f;
        const float y0 = 128.0f;
        const float cell = 58.0f;
        const float gap = 8.0f;

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

            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(177, 157, 127, 255);
            text(x0 + 8.0f * (cell + gap) + 10.0f,
                 y0 + static_cast<float>(visualRow) * (cell + gap) + cell * 0.5f,
                 kRowNames[row],
                 nullptr);
        }
    }

    void drawPad(const Rect& rect, const std::size_t row, const bool active)
    {
        int r = 31;
        int g = 28;
        int b = 24;
        if (active)
        {
            if (row <= 1)
            {
                r = 68; g = 137; b = 214;
            }
            else if (row <= 3)
            {
                r = 174; g = 75; b = 211;
            }
            else if (row == 4)
            {
                r = 71; g = 175; b = 103;
            }
            else if (row == 5)
            {
                r = 216; g = 132; b = 49;
            }
            else if (row == 6)
            {
                r = 218; g = 195; b = 65;
            }
            else
            {
                r = 206; g = 70; b = 64;
            }
        }

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(r, g, b, active ? 250 : 240);
        fill();
        strokeColor(active ? 248 : 65, active ? 236 : 55, active ? 198 : 45, active ? 210 : 255);
        strokeWidth(active ? 1.5f : 1.0f);
        stroke();
        closePath();
    }

    void drawPanel()
    {
        const float x = 645.0f;
        const float y = 128.0f;
        const float w = 320.0f;
        const float h = 512.0f;

        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(23, 20, 17, 245);
        fill();
        strokeColor(75, 58, 40, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(241, 221, 178, 255);
        text(x + 20.0f, y + 20.0f, "Mixer", nullptr);

        drawSliders(x + 20.0f, y + 58.0f, w - 40.0f);
        drawButtons(x + 20.0f, y + h - 78.0f, w - 40.0f);
    }

    void drawSliders(const float x, const float y, const float w)
    {
        const float sliderW = (w - 48.0f) / static_cast<float>(kSliders.size());
        for (std::size_t i = 0; i < kSliders.size(); ++i)
        {
            const Rect rect {x + static_cast<float>(i) * (sliderW + 8.0f), y, sliderW, 320.0f};
            sliderRects_[i] = rect;
            drawSlider(kSliders[i], rect);
        }
    }

    void drawSlider(const SliderDef& slider, const Rect& rect)
    {
        const float norm = clampf(values_[slider.index], 0.0f, 1.0f);
        const float trackX = rect.x + rect.w * 0.5f - 5.0f;
        const float trackY = rect.y + 34.0f;
        const float trackH = 216.0f;

        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(221, 205, 176, 255);
        text(rect.x + rect.w * 0.5f, rect.y, slider.label, nullptr);

        beginPath();
        roundedRect(trackX, trackY, 10.0f, trackH, 5.0f);
        fillColor(44, 37, 31, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(trackX, trackY + trackH * (1.0f - norm), 10.0f, trackH * norm, 5.0f);
        fillColor(221, 151, 66, 245);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 4.0f, trackY + trackH * (1.0f - norm) - 5.0f, rect.w - 8.0f, 10.0f, 5.0f);
        fillColor(249, 240, 217, 255);
        fill();
        closePath();

        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(norm * 100.0f)));
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(245, 184, 93, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 266.0f, buffer, nullptr);
    }

    void drawButtons(const float x, const float y, const float w)
    {
        const float buttonW = (w - 20.0f) / 3.0f;
        panicRect_ = {x, y, buttonW, 44.0f};
        mapRect_ = {x + buttonW + 10.0f, y, buttonW, 44.0f};
        ledRect_ = {x + (buttonW + 10.0f) * 2.0f, y, buttonW, 44.0f};
        char mapLabel[16];
        std::snprintf(mapLabel, sizeof(mapLabel), "Map %d", static_cast<int>(std::lround(values_[kParamPadMap])) + 1);
        drawButton(panicRect_, "Panic", 206, 70, 64, false);
        drawButton(mapRect_, mapLabel, 216, 132, 49, values_[kParamPadMap] > 0.5f);
        drawButton(ledRect_, values_[kParamLedFeedback] >= 0.5f ? "LED On" : "LED Off", 74, 137, 214, values_[kParamLedFeedback] >= 0.5f);
    }

    void drawButton(const Rect& rect, const char* label, const int r, const int g, const int b, const bool active)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(active ? r : 38, active ? g : 31, active ? b : 25, 255);
        fill();
        strokeColor(r, g, b, active ? 245 : 190);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(249, 238, 218, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void updateSlider(const std::size_t sliderIndex, const float y)
    {
        const Rect& rect = sliderRects_[sliderIndex];
        const float top = rect.y + 34.0f;
        const float height = 216.0f;
        const float norm = 1.0f - clampf((y - top) / height, 0.0f, 1.0f);
        setParameter(kSliders[sliderIndex].index, norm);
    }

    void triggerParameter(const std::uint32_t parameter)
    {
        if (parameter < kCellCount)
        {
            const std::size_t row = parameter / kGridWidth;
            const std::size_t col = parameter % kGridWidth;
            std::size_t hardwareRow = row;
            std::size_t hardwareCol = col;
            unapplyPadMap(static_cast<std::uint32_t>(std::lround(values_[kParamPadMap])), hardwareRow, hardwareCol);
            sendNote(0, gridToNote(hardwareRow, hardwareCol), 127);
            return;
        }

        editParameter(parameter, true);
        setParameterValue(parameter, 1.0f);
        editParameter(parameter, false);
        setParameterValue(parameter, 0.0f);
    }

    void setParameter(const std::uint32_t parameter, const float value)
    {
        editParameter(parameter, true);
        setParameterValue(parameter, value);
        editParameter(parameter, false);
        values_[parameter] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PaunchLadUI)
};

UI* createUI()
{
    return new PaunchLadUI();
}

END_NAMESPACE_DISTRHO
