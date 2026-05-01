#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamGenre = 0,
    kParamStyleMode,
    kParamChannel,
    kParamKitMap,
    kParamBars,
    kParamResolution,
    kParamDensity,
    kParamVariation,
    kParamFillAmount,
    kParamSeed,
    kParamKickAmount,
    kParamBackbeatAmount,
    kParamHatAmount,
    kParamAuxAmount,
    kParamActionNew,
    kParamActionMutate,
    kParamActionFill,
    kParamTomAmount,
    kParamMetalAmount,
    kParamVary,
    kParameterCount
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

struct SliderDef {
    uint32_t index;
    const char* label;
    float min;
    float max;
    bool integer;
};

struct SelectorDef {
    uint32_t index;
    const char* label;
    const char* const* items;
    int count;
    int valueOffset;
};

struct ButtonDef {
    uint32_t index;
    const char* label;
    int red;
    int green;
    int blue;
};

constexpr std::array<SliderDef, 11> kSliders = {{
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamVariation, "Variation", 0.0f, 1.0f, false},
    {kParamFillAmount, "Fill Amount", 0.0f, 1.0f, false},
    {kParamVary, "Vary", 0.0f, 100.0f, true},
    {kParamKickAmount, "Kick Amount", 0.0f, 1.0f, false},
    {kParamBackbeatAmount, "Backbeat", 0.0f, 1.0f, false},
    {kParamHatAmount, "Hat Amount", 0.0f, 1.0f, false},
    {kParamAuxAmount, "Perc Amount", 0.0f, 1.0f, false},
    {kParamTomAmount, "Tom Amount", 0.0f, 1.0f, false},
    {kParamMetalAmount, "Metal Amount", 0.0f, 1.0f, false},
    {kParamSeed, "Seed", 0.0f, 65535.0f, true},
}};

constexpr const char* kGenreNames[] = {
    "Rock", "Disco", "Shuffle", "Electro", "Dub", "Motorik", "Bossa", "Afro"
};

constexpr const char* kStyleNames[] = {
    "Auto", "Straight", "Reel", "Waltz", "Jig", "Slip Jig"
};

constexpr const char* kKitMapNames[] = {
    "Flues Drumkit", "GM"
};

constexpr const char* kResolutionNames[] = {
    "1/8", "1/16", "1/16T"
};

constexpr const char* kBarNames[] = {
    "1 Bar", "2 Bars", "3 Bars", "4 Bars"
};

constexpr const char* kChannelNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr std::array<SelectorDef, 6> kSelectors = {{
    {kParamGenre, "Genre", kGenreNames, 8, 0},
    {kParamStyleMode, "Style", kStyleNames, 6, 0},
    {kParamKitMap, "Kit Map", kKitMapNames, 2, 0},
    {kParamResolution, "Resolution", kResolutionNames, 3, 0},
    {kParamBars, "Bars", kBarNames, 4, 1},
    {kParamChannel, "Channel", kChannelNames, 16, 1},
}};

constexpr std::array<ButtonDef, 3> kButtons = {{
    {kParamActionNew, "New", 62, 148, 112},
    {kParamActionMutate, "Mutate", 192, 127, 57},
    {kParamActionFill, "Fill", 74, 126, 188},
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] std::string formatSliderValue(const uint32_t index, const float value)
{
    char buf[64];
    switch (index) {
    case kParamSeed:
        std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(std::lround(value)));
        break;
    case kParamVary:
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value)));
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
        break;
    }
    return buf;
}

[[nodiscard]] std::string formatSelectorValue(const SelectorDef& def, const float value)
{
    const int item = clampi(static_cast<int>(std::lround(value)) - def.valueOffset, 0, def.count - 1);
    return def.items[item];
}

[[nodiscard]] const SliderDef* sliderDefForIndex(const uint32_t index)
{
    for (const SliderDef& def : kSliders) {
        if (def.index == index) {
            return &def;
        }
    }
    return nullptr;
}

}  // namespace

class DrumgenUI : public UI
{
public:
    DrumgenUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        buttonPulse_.fill(0);

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif

        values_[kParamGenre] = 0.0f;
        values_[kParamStyleMode] = 0.0f;
        values_[kParamChannel] = 10.0f;
        values_[kParamKitMap] = 0.0f;
        values_[kParamBars] = 2.0f;
        values_[kParamResolution] = 1.0f;
        values_[kParamDensity] = 0.58f;
        values_[kParamVariation] = 0.35f;
        values_[kParamFillAmount] = 0.30f;
        values_[kParamSeed] = 1.0f;
        values_[kParamKickAmount] = 0.78f;
        values_[kParamBackbeatAmount] = 0.76f;
        values_[kParamHatAmount] = 0.82f;
        values_[kParamAuxAmount] = 0.28f;
        values_[kParamTomAmount] = 0.30f;
        values_[kParamMetalAmount] = 0.26f;
        values_[kParamVary] = 0.0f;
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values_.size()) {
            values_[index] = value;
            repaint();
        }
    }

    void uiIdle() override
    {
        bool needsRepaint = false;
        for (int& pulse : buttonPulse_) {
            if (pulse > 0) {
                --pulse;
                needsRepaint = true;
            }
        }
        if (needsRepaint) {
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;
        const float headerH = 84.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);

        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.38f;
        const float rightW = width - leftW - pad * 3.0f;

        drawPatternPanel(pad, contentY, leftW, contentH);
        drawMacroPanel(pad * 2.0f + leftW, contentY, rightW, contentH);

        if (openSelector_ >= 0) {
            drawOpenSelectorMenu(openSelector_);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) {
            return false;
        }

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            draggingSlider_ = -1;
            return false;
        }

        if (openSelector_ >= 0) {
            if (handleOpenSelectorClick(x, y)) {
                return true;
            }
            openSelector_ = -1;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                draggingSlider_ = static_cast<int>(i);
                updateSliderFromPosition(draggingSlider_, x);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i) {
            if (selectorRects_[i].contains(x, y)) {
                openSelector_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }

        for (std::size_t i = 0; i < buttonRects_.size(); ++i) {
            if (buttonRects_[i].contains(x, y)) {
                triggerButton(static_cast<int>(i));
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingSlider_ >= 0) {
            updateSliderFromPosition(draggingSlider_, static_cast<float>(ev.pos.getX()));
            return true;
        }

        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                nudgeSlider(static_cast<int>(i), ev.delta.getY() > 0.0f ? 1.0f : -1.0f);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i) {
            if (selectorRects_[i].contains(x, y)) {
                if (openSelector_ == static_cast<int>(i)) {
                    openSelector_ = -1;
                    repaint();
                } else {
                    cycleSelector(static_cast<int>(i), ev.delta.getY() > 0.0f ? -1 : 1);
                }
                return true;
            }
        }

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    std::array<Rect, kSelectors.size()> selectorRects_ {};
    std::array<Rect, kButtons.size()> buttonRects_ {};
    std::array<int, kButtons.size()> buttonPulse_ {};
    int draggingSlider_ = -1;
    int openSelector_ = -1;

    static constexpr float kSelectorItemHeight = 30.0f;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(14, 20, 26, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(24, 44, 40, 255);
        rect(0.0f, 0.0f, width, height * 0.34f);
        fill();
        closePath();

        beginPath();
        fillColor(168, 128, 58, 20);
        roundedRect(width - 290.0f, 28.0f, 240.0f, 240.0f, 42.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(18, 31, 35, 236);
        fill();
        closePath();

        fontSize(31.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(238, 241, 243, 255);
        text(x + 22.0f, y + 16.0f, "DrumGen", nullptr);

        fontSize(13.0f);
        fillColor(157, 174, 176, 255);
        text(x + 24.0f, y + 53.0f, "Transport-synced polyphonic drum MIDI generator", nullptr);

        drawPill(x + w - 226.0f, y + 18.0f, 204.0f, 30.0f, "DPF VST3 port", 92, 191, 147);
        drawPill(x + w - 170.0f, y + 52.0f, 148.0f, 22.0f, "Action buttons armed", 197, 147, 68);
    }

    void drawPill(float x, float y, float w, float h, const char* label, int r, int g, int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 34);
        fill();
        strokeColor(r, g, b, 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(h >= 26.0f ? 13.0f : 11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(r, g, b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawPatternPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(18, 25, 31, 244);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 231, 234, 255);
        text(x + 20.0f, y + 18.0f, "Pattern", nullptr);

        float cy = y + 54.0f;
        for (std::size_t i = 0; i < kSelectors.size(); ++i) {
            selectorRects_[i] = {x + 20.0f, cy, w - 40.0f, 54.0f};
            drawSelector(static_cast<int>(i), kSelectors[i], selectorRects_[i]);
            cy += 66.0f;
        }

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 231, 234, 255);
        text(x + 20.0f, cy + 2.0f, "Actions", nullptr);
        cy += 30.0f;

        const float buttonGap = 10.0f;
        const float buttonW = (w - 40.0f - buttonGap * 2.0f) / 3.0f;
        for (std::size_t i = 0; i < kButtons.size(); ++i) {
            buttonRects_[i] = {x + 20.0f + static_cast<float>(i) * (buttonW + buttonGap), cy, buttonW, 50.0f};
            drawButton(static_cast<int>(i), kButtons[i], buttonRects_[i]);
        }
    }

    void drawSelector(const int selectorIndex, const SelectorDef& def, const Rect& rect)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 16.0f);
        fillColor(31, 40, 49, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(151, 167, 178, 255);
        text(rect.x + 16.0f, rect.y + 11.0f, def.label, nullptr);

        fontSize(18.0f);
        fillColor(236, 240, 242, 255);
        const std::string current = formatSelectorValue(def, values_[def.index]);
        text(rect.x + 16.0f, rect.y + 30.0f, current.c_str(), nullptr);

        fontSize(18.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(126, 143, 154, 255);
        text(rect.x + rect.w - 18.0f,
             rect.y + rect.h * 0.5f + 1.0f,
             openSelector_ == selectorIndex ? "^" : "v",
             nullptr);
    }

    void drawButton(const int buttonIndex, const ButtonDef& def, const Rect& rect)
    {
        const float pulse = static_cast<float>(buttonPulse_[buttonIndex]) / 8.0f;
        const int fillR = static_cast<int>(def.red + (228 - def.red) * pulse);
        const int fillG = static_cast<int>(def.green + (214 - def.green) * pulse);
        const int fillB = static_cast<int>(def.blue + (184 - def.blue) * pulse);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 16.0f);
        fillColor(fillR, fillG, fillB, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 15.0f);
        strokeColor(241, 244, 246, static_cast<int>(75 + pulse * 90.0f));
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(16.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(245, 247, 248, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, def.label, nullptr);
    }

    void drawMacroPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(18, 24, 30, 244);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 231, 234, 255);
        text(x + 20.0f, y + 18.0f, "Mix", nullptr);

        const float innerX = x + 20.0f;
        const float innerY = y + 52.0f;
        const float innerW = w - 40.0f;
        const float colGap = 18.0f;
        const float colW = (innerW - colGap) * 0.5f;
        const float rowH = 52.0f;
        const float rowGap = 18.0f;

        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            Rect rect;
            if (i == kSliders.size() - 1) {
                rect = {innerX, innerY + 5.0f * (rowH + rowGap), innerW, 22.0f};
            } else {
                const int col = static_cast<int>(i % 2);
                const int row = static_cast<int>(i / 2);
                rect = {innerX + static_cast<float>(col) * (colW + colGap),
                        innerY + static_cast<float>(row) * (rowH + rowGap) + 18.0f,
                        colW,
                        22.0f};
            }

            sliderRects_[i] = rect;
            drawSlider(kSliders[i], rect, values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const float value, const bool active)
    {
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(152, 168, 179, 255);
        text(rect.x, rect.y - 18.0f, def.label, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(229, 234, 237, 255);
        const std::string valueText = formatSliderValue(def.index, value);
        text(rect.x + rect.w, rect.y - 18.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 11.0f);
        fillColor(35, 44, 54, 255);
        fill();
        closePath();

        const float t = clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, std::max(14.0f, rect.w * t), rect.h, 11.0f);
        fillColor(active ? 219 : 179, active ? 153 : 121, 74, 255);
        fill();
        closePath();
    }

    void drawOpenSelectorMenu(const int selectorIndex)
    {
        const Rect base = selectorRects_[selectorIndex];
        const SelectorDef& def = kSelectors[selectorIndex];
        const Rect menuRect = {
            base.x,
            base.y + base.h + 6.0f,
            base.w,
            static_cast<float>(def.count) * kSelectorItemHeight
        };

        beginPath();
        roundedRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 16.0f);
        fillColor(22, 28, 35, 252);
        fill();
        closePath();

        const int selected = clampi(static_cast<int>(std::lround(values_[def.index])) - def.valueOffset, 0, def.count - 1);

        for (int i = 0; i < def.count; ++i) {
            const float rowY = menuRect.y + static_cast<float>(i) * kSelectorItemHeight;
            if (i == selected) {
                beginPath();
                roundedRect(menuRect.x + 4.0f, rowY + 3.0f, menuRect.w - 8.0f, kSelectorItemHeight - 6.0f, 10.0f);
                fillColor(74, 103, 114, 255);
                fill();
                closePath();
            }

            fontSize(13.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(237, 240, 242, 255);
            text(menuRect.x + 14.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, def.items[i], nullptr);
        }
    }

    bool handleOpenSelectorClick(const float x, const float y)
    {
        const Rect& base = selectorRects_[openSelector_];
        if (base.contains(x, y)) {
            openSelector_ = -1;
            repaint();
            return true;
        }

        const SelectorDef& def = kSelectors[openSelector_];
        const Rect menuRect = {base.x, base.y + base.h + 6.0f, base.w, static_cast<float>(def.count) * kSelectorItemHeight};
        if (!menuRect.contains(x, y)) {
            return false;
        }

        const int item = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, def.count - 1);
        setParameter(def.index, static_cast<float>(item + def.valueOffset));
        openSelector_ = -1;
        repaint();
        return true;
    }

    void updateSliderFromPosition(const int sliderIndex, const float mouseX)
    {
        const SliderDef& def = kSliders[sliderIndex];
        const Rect& rect = sliderRects_[sliderIndex];
        const float t = clampf((mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        float value = def.min + t * (def.max - def.min);
        if (def.integer) {
            value = std::round(value);
        }
        setParameter(def.index, value);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        const SliderDef& def = kSliders[sliderIndex];
        const float step = def.integer ? 1.0f : ((def.max - def.min) / 100.0f);
        setParameter(def.index, values_[def.index] + direction * step);
    }

    void cycleSelector(const int selectorIndex, const int direction)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const int current = clampi(static_cast<int>(std::lround(values_[def.index])) - def.valueOffset, 0, def.count - 1);
        const int next = clampi(current + direction, 0, def.count - 1);
        setParameter(def.index, static_cast<float>(next + def.valueOffset));
    }

    void triggerButton(const int buttonIndex)
    {
        const uint32_t index = kButtons[buttonIndex].index;
        editParameter(index, true);
        setParameterValue(index, 1.0f);
        editParameter(index, false);
        buttonPulse_[buttonIndex] = 8;
        repaint();
    }

    void setParameter(const uint32_t index, float value)
    {
        if (const SliderDef* def = sliderDefForIndex(index)) {
            value = clampf(value, def->min, def->max);
            if (def->integer) {
                value = std::round(value);
            }
        }

        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumgenUI)
};

UI* createUI()
{
    return new DrumgenUI();
}

END_NAMESPACE_DISTRHO
