#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamKey = 0,
    kParamScale,
    kParamCycleBars,
    kParamGranularity,
    kParamComplexity,
    kParamMovement,
    kParamChordSize,
    kParamNoteLength,
    kParamRegister,
    kParamSpread,
    kParamPassInput,
    kParamOutputChannel,
    kParamActionLearn,
    kParamStatusReady,
    kParamVary,
    kParamComp,
    kParamColor,
    kParamArpeggio,
    kParamStatusInput,
    kParamStatusOutput,
    kParameterCount
};

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
};

constexpr SliderDef kSliders[] = {
    {kParamComplexity, "Complexity", 0.0f, 1.0f, false},
    {kParamMovement, "Movement", 0.0f, 1.0f, false},
    {kParamSpread, "Spread", 0.0f, 1.0f, false},
    {kParamColor, "Color", 0.0f, 1.0f, false},
    {kParamNoteLength, "Note Len", 0.10f, 1.0f, false},
    {kParamArpeggio, "Arpeggio", 0.0f, 1.0f, false},
    {kParamComp, "Comping", 0.0f, 100.0f, true},
    {kParamVary, "Variation", 0.0f, 100.0f, true},
};

constexpr const char* kNoteNames[] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

constexpr const char* kScaleNames[] = {
    "Chrom", "Major", "Ionian", "Nat Min", "Harm Min", "Mel Min",
    "Dorian", "Phrygian", "Lydian", "Mixolyd", "Locrian", "Phryg Dom",
    "Neo Maj", "Neo Min",
    "Pent Maj", "Pent Min", "Blues",
    "Whole", "Altered", "H-W Dim", "W-H Dim",
    "Bebop Dom", "Bebop Maj", "Bebop Min"
};

constexpr const char* kGranularityNames[] = {
    "Beat", "Half Bar", "Bar"
};

constexpr const char* kChordSizeNames[] = {
    "Triads", "Sevenths", "Extended"
};

constexpr const char* kRegisterNames[] = {
    "Low", "Mid", "High"
};

constexpr const char* kToggleNames[] = {
    "Off", "On"
};

constexpr const char* kCycleBarNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8"
};

constexpr const char* kOutputChannelNames[] = {
    "Input", "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr SelectorDef kSelectors[] = {
    {kParamKey, "Key", kNoteNames, 12, 0},
    {kParamScale, "Scale", kScaleNames, 24, 0},
    {kParamCycleBars, "Cycle", kCycleBarNames, 8, 1},
    {kParamGranularity, "Segment", kGranularityNames, 3, 0},
    {kParamChordSize, "Voicing", kChordSizeNames, 3, 0},
    {kParamRegister, "Register", kRegisterNames, 3, 0},
    {kParamPassInput, "Pass Input", kToggleNames, 2, 0},
    {kParamOutputChannel, "Output", kOutputChannelNames, 17, 0},
};

constexpr ButtonDef kButtons[] = {
    {kParamActionLearn, "Learn"},
};

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
    char buffer[48];
    switch (index) {
    case kParamVary:
    case kParamComp:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value)));
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
        break;
    }
    return buffer;
}

[[nodiscard]] int selectorValueForDisplay(const SelectorDef& def, const float rawValue)
{
    return clampi(static_cast<int>(std::lround(rawValue)) - def.valueOffset, 0, def.count - 1);
}

[[nodiscard]] float selectorParameterValue(const SelectorDef& def, const int item)
{
    return static_cast<float>(item + def.valueOffset);
}

}  // namespace

class CadenceUI : public UI
{
public:
    CadenceUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamKey] = 0.0f;
        values_[kParamScale] = 2.0f;
        values_[kParamCycleBars] = 2.0f;
        values_[kParamGranularity] = 1.0f;
        values_[kParamComplexity] = 0.45f;
        values_[kParamMovement] = 0.65f;
        values_[kParamColor] = 0.0f;
        values_[kParamArpeggio] = 0.0f;
        values_[kParamChordSize] = 0.0f;
        values_[kParamNoteLength] = 1.0f;
        values_[kParamRegister] = 1.0f;
        values_[kParamSpread] = 0.0f;
        values_[kParamPassInput] = 1.0f;
        values_[kParamOutputChannel] = 0.0f;
        values_[kParamStatusReady] = 0.0f;
        values_[kParamVary] = 0.0f;
        values_[kParamComp] = 0.0f;
        values_[kParamStatusInput] = 0.0f;
        values_[kParamStatusOutput] = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
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
        if (learnPulse_ > 0) {
            --learnPulse_;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 20.0f;
        const float headerH = 72.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);

        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.60f;
        const float rightW = width - leftW - pad * 3.0f;

        updateLayout(width, height);
        drawSliderPanel(pad, contentY, leftW, contentH);
        drawRightPanel(pad * 2.0f + leftW, contentY, rightW, contentH);

        if (openSelector_ >= 0)
            drawOpenSelectorMenu(openSelector_);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        updateLayout(static_cast<float>(getWidth()), static_cast<float>(getHeight()));

        if (!ev.press) {
            draggingSlider_ = -1;
            return false;
        }

        if (openSelector_ >= 0) {
            if (handleOpenSelectorClick(x, y))
                return true;
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
        updateLayout(static_cast<float>(getWidth()), static_cast<float>(getHeight()));

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
    std::array<Rect, std::size(kSliders)> sliderRects_ {};
    std::array<Rect, std::size(kSelectors)> selectorRects_ {};
    std::array<Rect, std::size(kButtons)> buttonRects_ {};
    int draggingSlider_ = -1;
    int openSelector_ = -1;
    int learnPulse_ = 0;

    static constexpr float kSelectorItemHeight = 24.0f;

    void updateLayout(const float width, const float height)
    {
        const float pad = 20.0f;
        const float headerH = 72.0f;
        const float selectorH = 44.0f;
        const float buttonH = 44.0f;
        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.60f;
        const float rightX = pad * 2.0f + leftW;
        const float rightW = width - leftW - pad * 3.0f;

        const float innerX = pad + 20.0f;
        const float innerY = contentY + 52.0f;
        const float innerW = leftW - 40.0f;
        const float rowGap = 10.0f;
        const float rowH = 40.0f;
        const float columnGap = 16.0f;
        const float columnW = (innerW - columnGap) * 0.5f;

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            const float cx = innerX + static_cast<float>(i % 2) * (columnW + columnGap);
            const float ry = innerY + static_cast<float>(i / 2) * (rowH + rowGap);
            sliderRects_[i] = {cx, ry + 18.0f, columnW, 22.0f};
        }

        const float selectorGap = 6.0f;
        float cy = contentY + 54.0f;
        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            selectorRects_[i] = {rightX + 20.0f, cy, rightW - 40.0f, selectorH};
            cy += selectorH + selectorGap;
        }

        const float buttonY = contentY + contentH - 20.0f - buttonH;
        buttonRects_[0] = {rightX + 20.0f, buttonY, rightW - 40.0f, buttonH};
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(17, 21, 27, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(27, 35, 45, 255);
        rect(0.0f, 0.0f, width, height * 0.28f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(31, 42, 55, 230);
        fill();
        closePath();

        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(238, 241, 244, 255);
        text(x + 20.0f, y + 16.0f, "Cadence", nullptr);

        fontSize(13.0f);
        fillColor(154, 169, 183, 255);
        text(x + 22.0f, y + 48.0f, "Transport-synced MIDI harmonizer", nullptr);

        drawStatusPill(x + w - 404.0f, y + 16.0f, 140.0f, 30.0f, values_[kParamStatusReady] >= 0.5f ? "Ready" : "Learning",
                       values_[kParamStatusReady] >= 0.5f ? 103 : 195,
                       values_[kParamStatusReady] >= 0.5f ? 185 : 123,
                       values_[kParamStatusReady] >= 0.5f ? 134 : 73);
        drawActivityPill(x + w - 250.0f, y + 16.0f, 104.0f, 30.0f, "MIDI In", values_[kParamStatusInput]);
        drawActivityPill(x + w - 132.0f, y + 16.0f, 112.0f, 30.0f, "MIDI Out", values_[kParamStatusOutput]);
    }

    void drawStatusPill(const float x, const float y, const float w, const float h, const char* label, const int r, const int g, const int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 36);
        fill();
        strokeColor(r, g, b, 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(r, g, b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawActivityPill(const float x, const float y, const float w, const float h, const char* label, const float activity)
    {
        const float level = clampf(activity, 0.0f, 1.0f);
        const int r = 76 + static_cast<int>(level * 119.0f);
        const int g = 96 + static_cast<int>(level * 89.0f);
        const int b = 120 + static_cast<int>(level * 14.0f);
        drawStatusPill(x, y, w, h, label, r, g, b);
    }

    void drawSliderPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(24, 29, 37, 250);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(x + 20.0f, y + 18.0f, "Shape", nullptr);

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            drawSlider(kSliders[i], sliderRects_[i], values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const float value, const bool active)
    {
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(154, 169, 183, 255);
        text(rect.x, rect.y - 18.0f, def.label, nullptr);

        const std::string valueText = formatSliderValue(def.index, value);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(223, 230, 236, 255);
        text(rect.x + rect.w, rect.y - 18.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(42, 50, 62, 255);
        fill();
        closePath();

        const float denom = std::max(0.0001f, def.max - def.min);
        const float t = clampf((value - def.min) / denom, 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, rect.w * t, rect.h, 10.0f);
        fillColor(active ? 225 : 195, active ? 123 : 90, 73, 255);
        fill();
        closePath();
    }

    void drawRightPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(24, 29, 37, 250);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(x + 20.0f, y + 18.0f, "Setup", nullptr);

        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            drawSelector(kSelectors[i], selectorRects_[i], selectorValueForDisplay(kSelectors[i], values_[kSelectors[i].index]), static_cast<int>(i));
        }

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(x + 20.0f, buttonRects_[0].y - 28.0f, "Actions", nullptr);
        drawButton(kButtons[0], buttonRects_[0]);
    }

    void drawSelector(const SelectorDef& def, const Rect& rect, const int value, const int selectorIndex)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 14.0f);
        fillColor(34, 43, 55, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(152, 166, 181, 255);
        text(rect.x + 16.0f, rect.y + 8.0f, def.label, nullptr);

        fontSize(18.0f);
        fillColor(235, 239, 242, 255);
        text(rect.x + 16.0f, rect.y + 28.0f, def.items[clampi(value, 0, def.count - 1)], nullptr);

        fontSize(18.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(117, 133, 149, 255);
        text(rect.x + rect.w - 18.0f, rect.y + rect.h * 0.5f + 1.0f, openSelector_ == selectorIndex ? "^" : "v", nullptr);
    }

    void drawButton(const ButtonDef& def, const Rect& rect)
    {
        const float pulse = static_cast<float>(learnPulse_) / 8.0f;
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 16.0f);
        fillColor(76 + static_cast<int>(pulse * 30.0f), 96 + static_cast<int>(pulse * 28.0f), 120, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 15.0f);
        strokeColor(165, 186, 209, 110);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(16.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(240, 244, 247, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, def.label, nullptr);
    }

    void updateSliderFromPosition(const int sliderIndex, const float mouseX)
    {
        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const Rect& rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const float t = clampf((mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        float value = def.min + t * (def.max - def.min);
        if (def.integer)
            value = std::round(value);
        setParameter(def.index, value);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const float step = def.integer ? 1.0f : ((def.max - def.min) / 100.0f);
        setParameter(def.index, values_[def.index] + direction * step);
    }

    void cycleSelector(const int selectorIndex, const int direction)
    {
        const SelectorDef& def = kSelectors[static_cast<std::size_t>(selectorIndex)];
        const int value = selectorValueForDisplay(def, values_[def.index]);
        const int next = clampi(value + direction, 0, def.count - 1);
        setParameter(def.index, selectorParameterValue(def, next));
    }

    void drawOpenSelectorMenu(const int selectorIndex)
    {
        const SelectorDef& def = kSelectors[static_cast<std::size_t>(selectorIndex)];
        const int selected = selectorValueForDisplay(def, values_[def.index]);
        const Rect menuRect = selectorMenuRect(selectorIndex);
        const int columns = selectorMenuColumnCount(def);
        const int rows = selectorMenuRowCount(def);
        const float columnW = menuRect.w / static_cast<float>(columns);

        beginPath();
        roundedRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 14.0f);
        fillColor(22, 28, 36, 248);
        fill();
        strokeColor(93, 112, 134, 220);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (int i = 0; i < def.count; ++i) {
            const int col = i / rows;
            const int row = i % rows;
            const float itemX = menuRect.x + static_cast<float>(col) * columnW;
            const float rowY = menuRect.y + static_cast<float>(row) * kSelectorItemHeight;
            if (i == selected) {
                beginPath();
                roundedRect(itemX + 4.0f, rowY + 3.0f, columnW - 8.0f, kSelectorItemHeight - 6.0f, 10.0f);
                fillColor(74, 96, 122, 255);
                fill();
                closePath();
            }

            fontSize(columns > 1 ? 11.0f : 12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(236, 240, 243, 255);
            text(itemX + 14.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, def.items[i], nullptr);
        }
    }

    bool handleOpenSelectorClick(const float x, const float y)
    {
        const Rect& base = selectorRects_[static_cast<std::size_t>(openSelector_)];
        if (base.contains(x, y)) {
            openSelector_ = -1;
            repaint();
            return true;
        }

        const SelectorDef& def = kSelectors[static_cast<std::size_t>(openSelector_)];
        const Rect menuRect = selectorMenuRect(openSelector_);
        if (!menuRect.contains(x, y))
            return false;

        const int columns = selectorMenuColumnCount(def);
        const int rows = selectorMenuRowCount(def);
        const float columnW = menuRect.w / static_cast<float>(columns);
        const int col = clampi(static_cast<int>((x - menuRect.x) / columnW), 0, columns - 1);
        const int row = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, rows - 1);
        const int item = col * rows + row;
        if (item >= def.count)
            return true;

        setParameter(def.index, selectorParameterValue(def, item));
        openSelector_ = -1;
        repaint();
        return true;
    }

    [[nodiscard]] int selectorMenuColumnCount(const SelectorDef& def) const
    {
        return def.count > 12 ? 2 : 1;
    }

    [[nodiscard]] int selectorMenuRowCount(const SelectorDef& def) const
    {
        const int columns = selectorMenuColumnCount(def);
        return (def.count + columns - 1) / columns;
    }

    [[nodiscard]] Rect selectorMenuRect(const int selectorIndex) const
    {
        const SelectorDef& def = kSelectors[static_cast<std::size_t>(selectorIndex)];
        const Rect& base = selectorRects_[static_cast<std::size_t>(selectorIndex)];
        const float menuH = static_cast<float>(selectorMenuRowCount(def)) * kSelectorItemHeight;
        const float margin = 14.0f;
        float menuY = base.y + base.h + 6.0f;
        if (menuY + menuH > static_cast<float>(getHeight()) - margin)
            menuY = std::max(margin, static_cast<float>(getHeight()) - margin - menuH);
        return {base.x, menuY, base.w, menuH};
    }

    void triggerButton(const int buttonIndex)
    {
        const uint32_t index = kButtons[static_cast<std::size_t>(buttonIndex)].index;
        editParameter(index, true);
        setParameterValue(index, 1.0f);
        setParameterValue(index, 0.0f);
        editParameter(index, false);
        values_[kParamStatusReady] = 0.0f;
        learnPulse_ = 8;
        repaint();
    }

    void setParameter(const uint32_t index, float value)
    {
        value = clampParameter(index, value);

        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    [[nodiscard]] float clampParameter(const uint32_t index, float value) const
    {
        for (const SliderDef& def : kSliders) {
            if (def.index == index) {
                value = clampf(value, def.min, def.max);
                return def.integer ? std::round(value) : value;
            }
        }

        for (const SelectorDef& def : kSelectors) {
            if (def.index == index)
                return static_cast<float>(clampi(static_cast<int>(std::lround(value)), def.valueOffset, def.count - 1 + def.valueOffset));
        }

        return value;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadenceUI)
};

UI* createUI()
{
    return new CadenceUI();
}

END_NAMESPACE_DISTRHO
