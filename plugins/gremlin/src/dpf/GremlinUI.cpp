#include "DistrhoUI.hpp"

#include "gremlin_dpf_shared.hpp"
#include "gremlin_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::gremlin::kActionNames;
using downspout::gremlin::kActionReseed;
using downspout::gremlin::kActionBurst;
using downspout::gremlin::kActionRandomSource;
using downspout::gremlin::kActionRandomDelay;
using downspout::gremlin::kActionRandomAll;
using downspout::gremlin::kActionPanic;
using downspout::gremlin::kEffectiveParamCount;
using downspout::gremlin::kGremlinParameterCount;
using downspout::gremlin::kLiveParamCount;
using downspout::gremlin::kModeNames;
using downspout::gremlin::kMomentaryNames;
using downspout::gremlin::kParamInputHiddenStart;
using downspout::gremlin::kParamMacroStart;
using downspout::gremlin::kParamMasterTrim;
using downspout::gremlin::kParamMomentaryStart;
using downspout::gremlin::kParamSceneTriggerStart;
using downspout::gremlin::kParamStatusActivity;
using downspout::gremlin::kParamStatusScene;
using downspout::gremlin::kParamStatusStart;
using downspout::gremlin::kSceneNames;

constexpr uint32_t kInvalidParam = 0xffffffffu;

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
    uint32_t inputIndex;
    uint32_t statusIndex;
    const char* label;
};

struct ButtonDef {
    uint32_t parameterIndex;
    const char* label;
    int r;
    int g;
    int b;
};

constexpr std::array<SliderDef, 9> kMacroSliderDefs = {{
    {kParamMasterTrim, kInvalidParam, "Master"},
    {kParamMacroStart + 0, kInvalidParam, "Source"},
    {kParamMacroStart + 1, kInvalidParam, "Pitch"},
    {kParamMacroStart + 2, kInvalidParam, "Break"},
    {kParamMacroStart + 3, kInvalidParam, "Delay"},
    {kParamMacroStart + 4, kInvalidParam, "Space"},
    {kParamMacroStart + 5, kInvalidParam, "Stutter"},
    {kParamMacroStart + 6, kInvalidParam, "Tone"},
    {kParamMacroStart + 7, kInvalidParam, "Output"},
}};

constexpr std::array<SliderDef, 9> kPrimarySliderDefs = {{
    {1, kParamStatusStart + 1, "Damage"},
    {2, kParamStatusStart + 2, "Chaos"},
    {3, kParamStatusStart + 3, "Noise"},
    {4, kParamStatusStart + 4, "Drift"},
    {5, kParamStatusStart + 5, "Crunch"},
    {6, kParamStatusStart + 6, "Fold"},
    {14, kParamStatusStart + 14, "Attack"},
    {15, kParamStatusStart + 15, "Release"},
    {kInvalidParam, kInvalidParam, ""},
}};

constexpr std::array<SliderDef, 9> kDelaySliderDefs = {{
    {7, kParamStatusStart + 7, "Delay"},
    {8, kParamStatusStart + 8, "Feedback"},
    {9, kParamStatusStart + 9, "Warp"},
    {10, kParamStatusStart + 10, "Stutter"},
    {11, kParamStatusStart + 11, "Tone"},
    {12, kParamStatusStart + 12, "Damping"},
    {13, kParamStatusStart + 13, "Space"},
    {16, kParamStatusStart + 16, "Output"},
    {kInvalidParam, kInvalidParam, ""},
}};

constexpr std::array<SliderDef, 9> kHiddenSliderDefs = {{
    {kParamInputHiddenStart + 0, kParamStatusStart + kLiveParamCount + 0, "Source"},
    {kParamInputHiddenStart + 1, kParamStatusStart + kLiveParamCount + 1, "Burst"},
    {kParamInputHiddenStart + 2, kParamStatusStart + kLiveParamCount + 2, "Pitch"},
    {kParamInputHiddenStart + 3, kParamStatusStart + kLiveParamCount + 3, "Delay Mix"},
    {kParamInputHiddenStart + 4, kParamStatusStart + kLiveParamCount + 4, "Cross Fb"},
    {kParamInputHiddenStart + 5, kParamStatusStart + kLiveParamCount + 5, "Glitch"},
    {kParamInputHiddenStart + 6, kParamStatusStart + kLiveParamCount + 6, "Chaos Rate"},
    {kParamInputHiddenStart + 7, kParamStatusStart + kLiveParamCount + 7, "Duck"},
    {kInvalidParam, kInvalidParam, ""},
}};

constexpr std::array<ButtonDef, 4> kModeButtons = {{
    {0, "Shard", 207, 148, 74},
    {0, "Servo", 92, 176, 167},
    {0, "Spray", 128, 143, 222},
    {0, "Collapse", 192, 101, 126},
}};

constexpr std::array<ButtonDef, 4> kSceneButtons = {{
    {kParamSceneTriggerStart + 0, "Splinter", 214, 170, 82},
    {kParamSceneTriggerStart + 1, "Melt", 104, 182, 128},
    {kParamSceneTriggerStart + 2, "Rust", 199, 110, 84},
    {kParamSceneTriggerStart + 3, "Tunnel", 120, 132, 214},
}};

constexpr std::array<ButtonDef, 6> kActionButtons = {{
    {kActionReseed, "Reseed", 110, 163, 205},
    {kActionBurst, "Burst", 230, 160, 78},
    {kActionRandomSource, "Rand Source", 94, 181, 124},
    {kActionRandomDelay, "Rand Delay", 122, 140, 220},
    {kActionRandomAll, "Rand All", 201, 111, 155},
    {kActionPanic, "Panic", 205, 86, 86},
}};

constexpr std::array<ButtonDef, 8> kMomentaryButtons = {{
    {kParamMomentaryStart + 0, "Freeze", 105, 174, 217},
    {kParamMomentaryStart + 1, "Stutter Max", 205, 142, 70},
    {kParamMomentaryStart + 2, "Chaos Boost", 156, 113, 214},
    {kParamMomentaryStart + 3, "Crunch Blast", 201, 100, 88},
    {kParamMomentaryStart + 4, "Feedback Push", 210, 118, 92},
    {kParamMomentaryStart + 5, "Warp Push", 118, 153, 223},
    {kParamMomentaryStart + 6, "Noise Burst", 133, 177, 104},
    {kParamMomentaryStart + 7, "Duck Kill", 210, 90, 122},
}};

float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

std::string formatPercent(const float value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    return buffer;
}

std::string formatMode(const float value)
{
    const int index = std::clamp(static_cast<int>(std::lround(value)), 0, 3);
    return kModeNames[static_cast<std::size_t>(index)];
}

}  // namespace

class GremlinUI : public UI
{
public:
    GremlinUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        touched_.fill(false);

        values_[0] = 0.0f;
        for (std::size_t i = 1; i < 17; ++i)
            values_[i] = downspout::gremlin::kLiveParamSpecs[i].defaultValue;
        for (std::size_t i = 0; i < downspout::gremlin::kHiddenParamCount; ++i)
            values_[kParamInputHiddenStart + i] = downspout::gremlin::kHiddenParamSpecs[i].defaultValue;
        values_[kParamMasterTrim] = 0.45f;
        for (std::size_t i = 0; i < downspout::gremlin::kMacroCount; ++i)
            values_[kParamMacroStart + i] = 0.5f;

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
            touched_[index] = true;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 84.0f);
        drawButtonBands(pad, 124.0f, width - pad * 2.0f);
        drawSliders(pad, 312.0f, width - pad * 2.0f);
        drawMomentaries(pad, height - 118.0f, width - pad * 2.0f);
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
            if (activeMomentary_ >= 0)
            {
                setBooleanParameter(kMomentaryButtons[activeMomentary_].parameterIndex, false);
                activeMomentary_ = -1;
                return true;
            }
            return false;
        }

        for (std::size_t i = 0; i < modeRects_.size(); ++i)
        {
            if (modeRects_[i].contains(x, y))
            {
                setValueParameter(0, static_cast<float>(i));
                return true;
            }
        }

        for (std::size_t i = 0; i < sceneRects_.size(); ++i)
        {
            if (sceneRects_[i].contains(x, y))
            {
                triggerParameter(kSceneButtons[i].parameterIndex);
                return true;
            }
        }

        for (std::size_t i = 0; i < actionRects_.size(); ++i)
        {
            if (actionRects_[i].contains(x, y))
            {
                triggerParameter(kActionButtons[i].parameterIndex);
                return true;
            }
        }

        for (std::size_t i = 0; i < momentaryRects_.size(); ++i)
        {
            if (momentaryRects_[i].contains(x, y))
            {
                activeMomentary_ = static_cast<int>(i);
                setBooleanParameter(kMomentaryButtons[i].parameterIndex, true);
                return true;
            }
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i)
        {
            if (sliderRects_[i].contains(x, y) && sliderDefs_[i].inputIndex != kInvalidParam)
            {
                activeSlider_ = static_cast<int>(i);
                updateSliderFromY(activeSlider_, y);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ >= 0)
        {
            updateSliderFromY(activeSlider_, static_cast<float>(ev.pos.getY()));
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        for (std::size_t i = 0; i < sliderRects_.size(); ++i)
        {
            if (sliderRects_[i].contains(x, y) && sliderDefs_[i].inputIndex != kInvalidParam)
            {
                nudgeSlider(static_cast<int>(i), ev.delta.getY() > 0.0f ? 1.0f : -1.0f);
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kGremlinParameterCount> values_ {};
    std::array<bool, kGremlinParameterCount> touched_ {};
    std::array<SliderDef, 36> sliderDefs_ {};
    std::array<Rect, 36> sliderRects_ {};
    std::array<Rect, 4> modeRects_ {};
    std::array<Rect, 4> sceneRects_ {};
    std::array<Rect, 6> actionRects_ {};
    std::array<Rect, 8> momentaryRects_ {};
    int activeSlider_ = -1;
    int activeMomentary_ = -1;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(11, 14, 18, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(27, 32, 39, 255);
        rect(0.0f, 0.0f, width, height * 0.28f);
        fill();
        closePath();

        beginPath();
        fillColor(210, 146, 72, 20);
        roundedRect(width - 320.0f, 34.0f, 270.0f, 250.0f, 44.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(19, 24, 31, 238);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, w, 4.0f, 2.0f);
        fillColor(229, 162, 81, 255);
        fill();
        closePath();

        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(243, 245, 247, 255);
        text(x + 22.0f, y + 14.0f, "Gremlin", nullptr);

        fontSize(13.0f);
        fillColor(163, 170, 181, 255);
        text(x + 24.0f, y + 50.0f, "Chaotic malfunction instrument and gesture-driven glitch box", nullptr);

        const std::string modeLabel = formatMode(displayValue(kParamStatusStart, 0));
        drawPill(x + w - 290.0f, y + 18.0f, 128.0f, 28.0f, modeLabel.c_str(), 228, 163, 81);
        drawPill(x + w - 146.0f, y + 18.0f, 122.0f, 28.0f, kSceneNames[sceneIndex()], 102, 170, 215);

        const float activity = displayValue(kParamStatusActivity, kParamStatusActivity);
        beginPath();
        roundedRect(x + w - 290.0f, y + 54.0f, 266.0f, 14.0f, 7.0f);
        fillColor(32, 39, 48, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x + w - 290.0f, y + 54.0f, 266.0f * clampf(activity, 0.0f, 1.0f), 14.0f, 7.0f);
        fillColor(95, 188, 150, 255);
        fill();
        closePath();
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

    void drawButtonBands(const float x, const float y, const float w)
    {
        drawBand(x, y, w, 70.0f, "Modes", modeRects_.data(), kModeButtons.data(), kModeButtons.size(), true, false);
        drawBand(x, y + 82.0f, w * 0.44f - 10.0f, 70.0f, "Scenes", sceneRects_.data(), kSceneButtons.data(), kSceneButtons.size(), false, true);
        drawBand(x + w * 0.44f + 10.0f, y + 82.0f, w * 0.56f - 10.0f, 70.0f, "Actions", actionRects_.data(), kActionButtons.data(), kActionButtons.size(), false, false);
    }

    void drawBand(const float x,
                  const float y,
                  const float w,
                  const float h,
                  const char* title,
                  Rect* rects,
                  const ButtonDef* defs,
                  const std::size_t count,
                  const bool modeBand,
                  const bool sceneBand)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(18, 22, 28, 238);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(176, 183, 192, 255);
        text(x + 18.0f, y + 14.0f, title, nullptr);

        const float innerX = x + 18.0f;
        const float innerY = y + 34.0f;
        const float gap = 10.0f;
        const float buttonW = (w - 36.0f - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        const float buttonH = 24.0f;

        for (std::size_t i = 0; i < count; ++i)
        {
            rects[i] = {innerX + static_cast<float>(i) * (buttonW + gap), innerY, buttonW, buttonH};
            const bool active = modeBand
                ? (std::clamp(static_cast<int>(std::lround(displayValue(kParamStatusStart, kParamStatusStart))), 0, 3) == static_cast<int>(i))
                : sceneBand
                    ? (sceneIndex() == static_cast<int>(i + 1))
                    : false;
            drawButton(rects[i], defs[i], active, false);
        }
    }

    void drawSliders(const float x, const float y, const float w)
    {
        beginPath();
        roundedRect(x, y, w, 338.0f, 20.0f);
        fillColor(18, 22, 28, 240);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(179, 185, 194, 255);
        text(x + 18.0f, y + 14.0f, "Performance Surface", nullptr);

        constexpr std::array<const char*, 4> kRowTitles = {{
            "Master and Macros",
            "Source Behaviour",
            "Delay and Space",
            "Hidden Breakage",
        }};
        const std::array<const SliderDef*, 4> rowDefs = {{
            kMacroSliderDefs.data(),
            kPrimarySliderDefs.data(),
            kDelaySliderDefs.data(),
            kHiddenSliderDefs.data(),
        }};

        const float rowYStart = y + 42.0f;
        const float rowGap = 68.0f;
        const float sliderGap = 10.0f;
        const float sliderW = (w - 36.0f - sliderGap * 8.0f) / 9.0f;
        const float sliderH = 56.0f;

        std::size_t sliderIndex = 0;
        for (std::size_t row = 0; row < 4; ++row)
        {
            fontSize(11.0f);
            fillColor(120, 128, 139, 255);
            text(x + 18.0f, rowYStart + row * rowGap - 2.0f, kRowTitles[row], nullptr);

            for (std::size_t column = 0; column < 9; ++column, ++sliderIndex)
            {
                sliderDefs_[sliderIndex] = rowDefs[row][column];
                sliderRects_[sliderIndex] = {
                    x + 18.0f + column * (sliderW + sliderGap),
                    rowYStart + row * rowGap + 14.0f,
                    sliderW,
                    sliderH
                };
                drawSlider(static_cast<int>(sliderIndex), sliderDefs_[sliderIndex], sliderRects_[sliderIndex]);
            }
        }
    }

    void drawSlider(const int sliderIndex, const SliderDef& def, const Rect& rect)
    {
        if (def.inputIndex == kInvalidParam)
            return;

        const float value = currentSliderValue(sliderIndex);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(27, 32, 39, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 10.0f, rect.y + 26.0f, rect.w - 20.0f, rect.h - 36.0f, 6.0f);
        fillColor(38, 45, 56, 255);
        fill();
        closePath();

        const float fillHeight = (rect.h - 36.0f) * clampf(value, 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x + 10.0f, rect.y + rect.h - 10.0f - fillHeight, rect.w - 20.0f, fillHeight, 6.0f);
        const Color tint = statusTint(def.statusIndex);
        fillColor(tint);
        fill();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(223, 227, 232, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 8.0f, def.label, nullptr);

        const std::string label = formatPercent(value);
        fontSize(10.0f);
        fillColor(153, 163, 175, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h - 16.0f, label.c_str(), nullptr);
    }

    Color statusTint(const uint32_t statusIndex) const
    {
        if (statusIndex == kInvalidParam)
            return Color(235, 171, 87, 255);

        const float activity = displayValue(kParamStatusActivity, kParamStatusActivity);
        const std::uint8_t alpha = static_cast<std::uint8_t>(170 + 85 * clampf(activity, 0.0f, 1.0f));
        return Color(235, 171, 87, alpha);
    }

    void drawMomentaries(const float x, const float y, const float w)
    {
        beginPath();
        roundedRect(x, y, w, 84.0f, 18.0f);
        fillColor(18, 22, 28, 240);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(176, 183, 192, 255);
        text(x + 18.0f, y + 14.0f, "Hold Pads", nullptr);

        const float gap = 10.0f;
        const float buttonW = (w - 36.0f - gap * 7.0f) / 8.0f;
        for (std::size_t i = 0; i < momentaryRects_.size(); ++i)
        {
            momentaryRects_[i] = {x + 18.0f + i * (buttonW + gap), y + 38.0f, buttonW, 28.0f};
            const bool active = values_[kMomentaryButtons[i].parameterIndex] >= 0.5f;
            drawButton(momentaryRects_[i], kMomentaryButtons[i], active, activeMomentary_ == static_cast<int>(i));
        }
    }

    void drawButton(const Rect& rect, const ButtonDef& def, const bool active, const bool held)
    {
        const int baseAlpha = active || held ? 70 : 24;
        const int strokeAlpha = active || held ? 220 : 130;

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(def.r, def.g, def.b, baseAlpha);
        fill();
        strokeColor(def.r, def.g, def.b, strokeAlpha);
        strokeWidth(active || held ? 1.8f : 1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active || held ? 250 : 218, active || held ? 246 : 222, active || held ? 238 : 228, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);
    }

    int sceneIndex() const
    {
        return std::clamp(static_cast<int>(std::lround(displayValue(kParamStatusScene, kParamStatusScene))), 0, 4);
    }

    float displayValue(const uint32_t preferredIndex, const uint32_t fallbackIndex) const
    {
        if (preferredIndex != kInvalidParam && touched_[preferredIndex])
            return values_[preferredIndex];
        return values_[fallbackIndex];
    }

    float currentSliderValue(const int sliderIndex) const
    {
        const SliderDef& def = sliderDefs_[sliderIndex];
        if (def.inputIndex == kInvalidParam)
            return 0.0f;

        if (activeSlider_ == sliderIndex)
            return values_[def.inputIndex];

        if (def.statusIndex != kInvalidParam && touched_[def.statusIndex])
            return values_[def.statusIndex];

        return values_[def.inputIndex];
    }

    void updateSliderFromY(const int sliderIndex, const float y)
    {
        const SliderDef& def = sliderDefs_[sliderIndex];
        if (def.inputIndex == kInvalidParam)
            return;

        const Rect& rect = sliderRects_[sliderIndex];
        const float normalized = clampf((rect.y + rect.h - 10.0f - y) / (rect.h - 36.0f), 0.0f, 1.0f);
        setValueParameter(def.inputIndex, normalized);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        const SliderDef& def = sliderDefs_[sliderIndex];
        if (def.inputIndex == kInvalidParam)
            return;
        const float step = 0.02f * direction;
        setValueParameter(def.inputIndex, clampf(values_[def.inputIndex] + step, 0.0f, 1.0f));
    }

    void setValueParameter(const uint32_t index, const float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        touched_[index] = true;
        repaint();
    }

    void setBooleanParameter(const uint32_t index, const bool enabled)
    {
        setValueParameter(index, enabled ? 1.0f : 0.0f);
    }

    void triggerParameter(const uint32_t index)
    {
        editParameter(index, true);
        setParameterValue(index, 1.0f);
        editParameter(index, false);
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GremlinUI)
};

UI* createUI()
{
    return new GremlinUI();
}

END_NAMESPACE_DISTRHO
