#include "DistrhoUI.hpp"

#include "gremlin_driver_dpf_shared.hpp"
#include "gremlin_driver_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::gremlin_driver::kActionNames;
using downspout::gremlin_driver::kClockModeNames;
using downspout::gremlin_driver::kGremlinDriverParameterCount;
using downspout::gremlin_driver::kLaneCount;
using downspout::gremlin_driver::kLaneParamSpecs;
using downspout::gremlin_driver::kParamBpm;
using downspout::gremlin_driver::kParamClockMode;
using downspout::gremlin_driver::kParamLaneStart;
using downspout::gremlin_driver::kParamRandomize;
using downspout::gremlin_driver::kParamStatusBpm;
using downspout::gremlin_driver::kParamStatusLaneStart;
using downspout::gremlin_driver::kParamStatusTransport;
using downspout::gremlin_driver::kParamStatusTriggerStart;
using downspout::gremlin_driver::kParamTriggerStart;
using downspout::gremlin_driver::kShapeNames;
using downspout::gremlin_driver::kTargetNames;
using downspout::gremlin_driver::kTriggerCount;
using downspout::gremlin_driver::kTriggerParamSpecs;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

struct SliderBinding {
    uint32_t index;
    const char* label;
    float minimum;
    float maximum;
    bool integer;
};

struct SelectorBinding {
    uint32_t index;
    const char* const* labels;
    int count;
};

constexpr std::array<SliderBinding, 12> kLaneSliders = {{
    {kParamLaneStart + 2, "Rate", 0.0f, 1.0f, false},
    {kParamLaneStart + 3, "Depth", 0.0f, 1.0f, false},
    {kParamLaneStart + 4, "Center", 0.0f, 1.0f, false},
    {kParamLaneStart + 7, "Rate", 0.0f, 1.0f, false},
    {kParamLaneStart + 8, "Depth", 0.0f, 1.0f, false},
    {kParamLaneStart + 9, "Center", 0.0f, 1.0f, false},
    {kParamLaneStart + 12, "Rate", 0.0f, 1.0f, false},
    {kParamLaneStart + 13, "Depth", 0.0f, 1.0f, false},
    {kParamLaneStart + 14, "Center", 0.0f, 1.0f, false},
    {kParamLaneStart + 17, "Rate", 0.0f, 1.0f, false},
    {kParamLaneStart + 18, "Depth", 0.0f, 1.0f, false},
    {kParamLaneStart + 19, "Center", 0.0f, 1.0f, false},
}};

constexpr std::array<SliderBinding, 4> kTriggerSliders = {{
    {kParamTriggerStart + 1, "Rate", 0.0f, 1.0f, false},
    {kParamTriggerStart + 2, "Chance", 0.0f, 1.0f, false},
    {kParamTriggerStart + 4, "Rate", 0.0f, 1.0f, false},
    {kParamTriggerStart + 5, "Chance", 0.0f, 1.0f, false},
}};

constexpr std::array<SelectorBinding, 4> kLaneTargetSelectors = {{
    {kParamLaneStart + 0, kTargetNames.data(), static_cast<int>(kTargetNames.size())},
    {kParamLaneStart + 5, kTargetNames.data(), static_cast<int>(kTargetNames.size())},
    {kParamLaneStart + 10, kTargetNames.data(), static_cast<int>(kTargetNames.size())},
    {kParamLaneStart + 15, kTargetNames.data(), static_cast<int>(kTargetNames.size())},
}};

constexpr std::array<SelectorBinding, 4> kLaneShapeSelectors = {{
    {kParamLaneStart + 1, kShapeNames.data(), static_cast<int>(kShapeNames.size())},
    {kParamLaneStart + 6, kShapeNames.data(), static_cast<int>(kShapeNames.size())},
    {kParamLaneStart + 11, kShapeNames.data(), static_cast<int>(kShapeNames.size())},
    {kParamLaneStart + 16, kShapeNames.data(), static_cast<int>(kShapeNames.size())},
}};

constexpr std::array<SelectorBinding, 2> kTriggerActionSelectors = {{
    {kParamTriggerStart + 0, kActionNames.data(), static_cast<int>(kActionNames.size())},
    {kParamTriggerStart + 3, kActionNames.data(), static_cast<int>(kActionNames.size())},
}};

float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

int clampi(const int value, const int minValue, const int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

std::string formatPercent(const float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    return buffer;
}

std::string formatBpm(const float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f BPM", value);
    return buffer;
}

std::string selectorLabel(const SelectorBinding& binding, const float value)
{
    const int index = clampi(static_cast<int>(std::lround(value)), 0, binding.count - 1);
    return binding.labels[index];
}

}  // namespace

class GremlinDriverUI : public UI
{
public:
    GremlinDriverUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamClockMode] = 1.0f;
        values_[kParamBpm] = 120.0f;
        for (std::size_t i = 0; i < kLaneCount * 5; ++i)
            values_[kParamLaneStart + i] = kLaneParamSpecs[i].defaultValue;
        for (std::size_t i = 0; i < kTriggerCount * 3; ++i)
            values_[kParamTriggerStart + i] = kTriggerParamSpecs[i].defaultValue;
        values_[kParamStatusBpm] = 120.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values_.size())
        {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 94.0f);
        drawGlobalBar(pad, 132.0f, width - pad * 2.0f, 92.0f);
        drawLanes(pad, 244.0f, width - pad * 2.0f);
        drawTriggers(pad, height - 242.0f, width - pad * 2.0f);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press)
        {
            activeSlider_ = -1;
            return false;
        }

        if (clockRects_[0].contains(x, y))
        {
            setValueParameter(kParamClockMode, 0.0f);
            return true;
        }
        if (clockRects_[1].contains(x, y))
        {
            setValueParameter(kParamClockMode, 1.0f);
            return true;
        }
        if (randomizeRect_.contains(x, y))
        {
            triggerParameter(kParamRandomize);
            return true;
        }
        if (bpmRect_.contains(x, y))
        {
            activeSlider_ = kBpmSliderIndex;
            updateSliderFromPosition(activeSlider_, x, y);
            return true;
        }

        for (std::size_t i = 0; i < laneTargetRects_.size(); ++i)
        {
            if (laneTargetRects_[i].contains(x, y))
            {
                cycleSelector(kLaneTargetSelectors[i], 1);
                return true;
            }
            if (laneShapeRects_[i].contains(x, y))
            {
                cycleSelector(kLaneShapeSelectors[i], 1);
                return true;
            }
        }

        for (std::size_t i = 0; i < triggerActionRects_.size(); ++i)
        {
            if (triggerActionRects_[i].contains(x, y))
            {
                cycleSelector(kTriggerActionSelectors[i], 1);
                return true;
            }
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i)
        {
            if (sliderRects_[i].contains(x, y))
            {
                activeSlider_ = static_cast<int>(i);
                updateSliderFromPosition(activeSlider_, x, y);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ >= 0)
        {
            updateSliderFromPosition(activeSlider_,
                                     static_cast<float>(ev.pos.getX()),
                                     static_cast<float>(ev.pos.getY()));
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        const int delta = ev.delta.getY() > 0.0f ? 1 : -1;

        if (bpmRect_.contains(x, y))
        {
            nudgeSlider(kBpmSliderIndex, static_cast<float>(delta));
            return true;
        }

        for (std::size_t i = 0; i < laneTargetRects_.size(); ++i)
        {
            if (laneTargetRects_[i].contains(x, y))
            {
                cycleSelector(kLaneTargetSelectors[i], delta);
                return true;
            }
            if (laneShapeRects_[i].contains(x, y))
            {
                cycleSelector(kLaneShapeSelectors[i], delta);
                return true;
            }
        }

        for (std::size_t i = 0; i < triggerActionRects_.size(); ++i)
        {
            if (triggerActionRects_[i].contains(x, y))
            {
                cycleSelector(kTriggerActionSelectors[i], delta);
                return true;
            }
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i)
        {
            if (sliderRects_[i].contains(x, y))
            {
                nudgeSlider(static_cast<int>(i), static_cast<float>(delta));
                return true;
            }
        }

        return false;
    }

private:
    static constexpr int kBpmSliderIndex = static_cast<int>(kLaneSliders.size() + kTriggerSliders.size());

    std::array<float, kGremlinDriverParameterCount> values_ {};
    std::array<Rect, kLaneSliders.size() + kTriggerSliders.size() + 1> sliderRects_ {};
    std::array<Rect, 2> clockRects_ {};
    Rect bpmRect_ {};
    Rect randomizeRect_ {};
    std::array<Rect, kLaneCount> laneTargetRects_ {};
    std::array<Rect, kLaneCount> laneShapeRects_ {};
    std::array<Rect, kTriggerCount> triggerActionRects_ {};
    int activeSlider_ = -1;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(9, 13, 18, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(23, 28, 36, 255);
        rect(0.0f, 0.0f, width, height * 0.33f);
        fill();
        closePath();

        beginPath();
        fillColor(111, 176, 205, 18);
        roundedRect(width - 360.0f, 26.0f, 300.0f, 240.0f, 54.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(17, 22, 29, 238);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, w, 4.0f, 2.0f);
        fillColor(99, 169, 214, 255);
        fill();
        closePath();

        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(242, 245, 247, 255);
        text(x + 22.0f, y + 14.0f, "GremlinDriver", nullptr);

        fontSize(13.0f);
        fillColor(159, 168, 178, 255);
        text(x + 24.0f, y + 52.0f, "Macro lanes, action triggers, and patch scrambles for feeding Gremlin", nullptr);

        drawPill(x + w - 286.0f, y + 18.0f, 132.0f, 28.0f,
                 values_[kParamStatusTransport] > 0.5f ? "Transport Live" : "Transport Idle",
                 values_[kParamStatusTransport] > 0.5f ? 106 : 146,
                 values_[kParamStatusTransport] > 0.5f ? 194 : 112,
                 values_[kParamStatusTransport] > 0.5f ? 154 : 112);

        const std::string bpmLabel = formatBpm(values_[kParamStatusBpm] > 1.0f ? values_[kParamStatusBpm] : values_[kParamBpm]);
        drawPill(x + w - 142.0f, y + 18.0f, 118.0f, 28.0f, bpmLabel.c_str(), 211, 160, 76);
    }

    void drawPill(const float x, const float y, const float w, const float h, const char* label, const int r, const int g, const int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 34);
        fill();
        strokeColor(r, g, b, 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(r, g, b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawGlobalBar(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(17, 22, 29, 240);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(173, 181, 190, 255);
        text(x + 18.0f, y + 14.0f, "Clock and Patch Flow", nullptr);

        clockRects_[0] = {x + 18.0f, y + 40.0f, 88.0f, 28.0f};
        clockRects_[1] = {x + 112.0f, y + 40.0f, 112.0f, 28.0f};
        drawToggleButton(clockRects_[0], "Free", values_[kParamClockMode] < 0.5f, 92, 174, 196);
        drawToggleButton(clockRects_[1], "Transport", values_[kParamClockMode] >= 0.5f, 92, 174, 196);

        bpmRect_ = {x + 252.0f, y + 42.0f, 420.0f, 24.0f};
        sliderRects_[kBpmSliderIndex] = bpmRect_;
        drawHorizontalSlider(bpmRect_, "BPM", values_[kParamBpm], 40.0f, 220.0f, 105, 175, 214);

        randomizeRect_ = {x + w - 174.0f, y + 34.0f, 156.0f, 40.0f};
        drawActionButton(randomizeRect_, "Randomise Patch", 213, 126, 85);
    }

    void drawToggleButton(const Rect& rect, const char* label, const bool active, const int r, const int g, const int b)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(r, g, b, active ? 72 : 24);
        fill();
        strokeColor(r, g, b, active ? 210 : 130);
        strokeWidth(active ? 1.8f : 1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? 246 : 222, active ? 248 : 228, active ? 249 : 232, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void drawActionButton(const Rect& rect, const char* label, const int r, const int g, const int b)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 12.0f);
        fillColor(r, g, b, 44);
        fill();
        strokeColor(r, g, b, 180);
        strokeWidth(1.2f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(246, 240, 236, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void drawHorizontalSlider(const Rect& rect,
                              const char* label,
                              const float value,
                              const float minimum,
                              const float maximum,
                              const int r,
                              const int g,
                              const int b)
    {
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fillColor(208, 214, 220, 255);
        text(rect.x, rect.y - 6.0f, label, nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 12.0f);
        fillColor(35, 42, 53, 255);
        fill();
        closePath();

        const float normalized = clampf((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, rect.w * normalized, rect.h, 12.0f);
        fillColor(r, g, b, 255);
        fill();
        closePath();

        const std::string valueLabel = formatBpm(value);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(247, 248, 249, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, valueLabel.c_str(), nullptr);
    }

    void drawLanes(const float x, const float y, const float w)
    {
        const float gap = 14.0f;
        const float cardW = (w - gap * 3.0f) / 4.0f;

        for (std::size_t lane = 0; lane < kLaneCount; ++lane)
            drawLaneCard(lane, x + lane * (cardW + gap), y, cardW, 274.0f);
    }

    void drawLaneCard(const std::size_t laneIndex, const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(17, 22, 29, 242);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 231, 235, 255);
        const std::string laneTitle = "Lane " + std::to_string(laneIndex + 1);
        text(x + 16.0f, y + 14.0f, laneTitle.c_str(), nullptr);

        laneTargetRects_[laneIndex] = {x + 16.0f, y + 40.0f, w - 32.0f, 28.0f};
        laneShapeRects_[laneIndex] = {x + 16.0f, y + 76.0f, w - 32.0f, 28.0f};
        drawSelector(laneTargetRects_[laneIndex], "Target", selectorLabel(kLaneTargetSelectors[laneIndex], values_[kLaneTargetSelectors[laneIndex].index]), 103, 175, 214);
        drawSelector(laneShapeRects_[laneIndex], "Shape", selectorLabel(kLaneShapeSelectors[laneIndex], values_[kLaneShapeSelectors[laneIndex].index]), 198, 150, 72);

        const float sliderY = y + 124.0f;
        const float sliderGap = 10.0f;
        const float sliderW = (w - 32.0f - sliderGap * 2.0f) / 3.0f;
        const float sliderH = 112.0f;
        const std::size_t base = laneIndex * 3;
        for (std::size_t i = 0; i < 3; ++i)
        {
            const int sliderIndex = static_cast<int>(base + i);
            sliderRects_[sliderIndex] = {
                x + 16.0f + i * (sliderW + sliderGap),
                sliderY,
                sliderW,
                sliderH
            };
            drawVerticalSlider(sliderIndex, kLaneSliders[base + i], sliderRects_[sliderIndex], 103, 175, 214);
        }

        const float laneValue = values_[kParamStatusLaneStart + laneIndex];
        beginPath();
        roundedRect(x + 16.0f, y + h - 30.0f, w - 32.0f, 10.0f, 5.0f);
        fillColor(35, 42, 52, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x + 16.0f, y + h - 30.0f, (w - 32.0f) * clampf(laneValue, 0.0f, 1.0f), 10.0f, 5.0f);
        fillColor(103, 175, 214, 255);
        fill();
        closePath();
    }

    void drawSelector(const Rect& rect, const char* label, const std::string& value, const int r, const int g, const int b)
    {
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fillColor(147, 157, 169, 255);
        text(rect.x, rect.y - 4.0f, label, nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(r, g, b, 26);
        fill();
        strokeColor(r, g, b, 140);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(231, 235, 239, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, value.c_str(), nullptr);
    }

    void drawVerticalSlider(const int sliderIndex,
                            const SliderBinding& binding,
                            const Rect& rect,
                            const int r,
                            const int g,
                            const int b)
    {
        const float value = values_[binding.index];
        const float normalized = clampf((value - binding.minimum) / (binding.maximum - binding.minimum), 0.0f, 1.0f);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 12.0f);
        fillColor(27, 33, 43, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 10.0f, rect.y + 24.0f, rect.w - 20.0f, rect.h - 46.0f, 6.0f);
        fillColor(38, 45, 56, 255);
        fill();
        closePath();

        const float fillHeight = (rect.h - 46.0f) * normalized;
        beginPath();
        roundedRect(rect.x + 10.0f, rect.y + rect.h - 12.0f - fillHeight, rect.w - 20.0f, fillHeight, 6.0f);
        fillColor(r, g, b, 255);
        fill();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 7.0f, binding.label, nullptr);

        const std::string label = binding.index == kParamBpm ? formatBpm(value) : formatPercent(normalized);
        fillColor(151, 161, 172, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h - 16.0f, label.c_str(), nullptr);
    }

    void drawTriggers(const float x, const float y, const float w)
    {
        const float gap = 16.0f;
        const float cardW = (w - gap) / 2.0f;
        for (std::size_t trigger = 0; trigger < kTriggerCount; ++trigger)
            drawTriggerCard(trigger, x + trigger * (cardW + gap), y, cardW, 198.0f);
    }

    void drawTriggerCard(const std::size_t triggerIndex, const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(17, 22, 29, 242);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 231, 235, 255);
        const std::string title = "Trigger " + std::to_string(triggerIndex + 1);
        text(x + 16.0f, y + 14.0f, title.c_str(), nullptr);

        triggerActionRects_[triggerIndex] = {x + 16.0f, y + 40.0f, w - 32.0f, 28.0f};
        drawSelector(triggerActionRects_[triggerIndex], "Action", selectorLabel(kTriggerActionSelectors[triggerIndex], values_[kTriggerActionSelectors[triggerIndex].index]), 197, 128, 82);

        const std::size_t base = triggerIndex * 2;
        sliderRects_[kLaneSliders.size() + base + 0] = {x + 16.0f, y + 90.0f, 86.0f, 92.0f};
        sliderRects_[kLaneSliders.size() + base + 1] = {x + 116.0f, y + 90.0f, 86.0f, 92.0f};
        drawVerticalSlider(static_cast<int>(kLaneSliders.size() + base + 0), kTriggerSliders[base + 0], sliderRects_[kLaneSliders.size() + base + 0], 197, 128, 82);
        drawVerticalSlider(static_cast<int>(kLaneSliders.size() + base + 1), kTriggerSliders[base + 1], sliderRects_[kLaneSliders.size() + base + 1], 197, 128, 82);

        const float flash = values_[kParamStatusTriggerStart + triggerIndex];
        beginPath();
        roundedRect(x + w - 90.0f, y + 98.0f, 58.0f, 58.0f, 14.0f);
        fillColor(36, 43, 53, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x + w - 90.0f, y + 98.0f, 58.0f, 58.0f, 14.0f);
        fillColor(204, 136, 86, static_cast<int>(40 + flash * 180.0f));
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(240, 243, 246, 255);
        text(x + w - 61.0f, y + 127.0f, "Fire", nullptr);
    }

    void cycleSelector(const SelectorBinding& binding, const int delta)
    {
        const int count = binding.count;
        int value = static_cast<int>(std::lround(values_[binding.index]));
        value = (value + delta) % count;
        if (value < 0)
            value += count;
        setValueParameter(binding.index, static_cast<float>(value));
    }

    void updateSliderFromPosition(const int sliderIndex, const float x, const float y)
    {
        if (sliderIndex == kBpmSliderIndex)
        {
            const float normalized = clampf((x - bpmRect_.x) / bpmRect_.w, 0.0f, 1.0f);
            setValueParameter(kParamBpm, 40.0f + normalized * 180.0f);
            return;
        }

        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kLaneSliders.size() + kTriggerSliders.size()))
            return;

        const bool laneSlider = sliderIndex < static_cast<int>(kLaneSliders.size());
        const SliderBinding& binding = laneSlider
            ? kLaneSliders[sliderIndex]
            : kTriggerSliders[sliderIndex - static_cast<int>(kLaneSliders.size())];
        const Rect& rect = sliderRects_[sliderIndex];
        const float normalized = clampf((rect.y + rect.h - 12.0f - y) / (rect.h - 46.0f), 0.0f, 1.0f);
        const float value = binding.minimum + normalized * (binding.maximum - binding.minimum);
        setValueParameter(binding.index, binding.integer ? std::round(value) : value);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        if (sliderIndex == kBpmSliderIndex)
        {
            setValueParameter(kParamBpm, clampf(values_[kParamBpm] + direction * 1.0f, 40.0f, 220.0f));
            return;
        }

        const bool laneSlider = sliderIndex < static_cast<int>(kLaneSliders.size());
        const SliderBinding& binding = laneSlider
            ? kLaneSliders[sliderIndex]
            : kTriggerSliders[sliderIndex - static_cast<int>(kLaneSliders.size())];
        const float step = binding.integer ? 1.0f : 0.02f;
        setValueParameter(binding.index,
                          clampf(values_[binding.index] + direction * step, binding.minimum, binding.maximum));
    }

    void setValueParameter(const uint32_t index, const float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    void triggerParameter(const uint32_t index)
    {
        editParameter(index, true);
        setParameterValue(index, 1.0f);
        editParameter(index, false);
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GremlinDriverUI)
};

UI* createUI()
{
    return new GremlinDriverUI();
}

END_NAMESPACE_DISTRHO
