#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamChannel = 0,
    kParamBars,
    kParamRegister,
    kParamRegisterLow,
    kParamRegisterHigh,
    kParamDensity,
    kParamRisk,
    kParamHumanize,
    kParamMute,
    kParamGenerate,
    kParamPlay,
    kParamAccept,
    kParamRetry,
    kParamSource,
    kParamStatusReady,
    kParamConnectionStatus,
    kParameterCount
};

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
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamRisk, "Risk", 0.0f, 1.0f, false},
    {kParamHumanize, "Humanize", 0.0f, 1.0f, false},
    {kParamRegisterLow, "Low Note", 0.0f, 127.0f, true},
    {kParamRegisterHigh, "High Note", 0.0f, 127.0f, true},
};

constexpr const char* kRegisterNames[] = {
    "Low", "Mid", "High", "Custom"
};

constexpr const char* kBarsNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8"
};

constexpr const char* kChannelNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr const char* kToggleNames[] = {
    "Open", "Mute"
};

constexpr const char* kSourceNames[] = {
    "Local", "Server"
};

constexpr const char* kConnectionNames[] = {
    "Local", "Requesting", "Ready", "Server Offline", "Server Error"
};

constexpr SelectorDef kSelectors[] = {
    {kParamChannel, "Channel", kChannelNames, 16, 1},
    {kParamBars, "Bars", kBarsNames, 8, 1},
    {kParamRegister, "Register", kRegisterNames, 4, 0},
    {kParamSource, "Source", kSourceNames, 2, 0},
    {kParamMute, "Output", kToggleNames, 2, 0},
};

constexpr ButtonDef kButtons[] = {
    {kParamGenerate, "Generate"},
    {kParamPlay, "Play"},
    {kParamRetry, "Retry"},
    {kParamAccept, "Accept"},
};

[[nodiscard]] float clampf(const float value, const float minimum, const float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int clampi(const int value, const int minimum, const int maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int selectorValueForDisplay(const SelectorDef& def, const float rawValue)
{
    return clampi(static_cast<int>(std::lround(rawValue)) - def.valueOffset, 0, def.count - 1);
}

[[nodiscard]] float selectorParameterValue(const SelectorDef& def, const int item)
{
    return static_cast<float>(item + def.valueOffset);
}

[[nodiscard]] std::string formatSliderValue(const SliderDef& def, const float value)
{
    char buffer[48];
    if (def.integer)
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
    else
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    return buffer;
}

}  // namespace

class SidecarUI : public UI
{
public:
    SidecarUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamChannel] = 1.0f;
        values_[kParamBars] = 4.0f;
        values_[kParamRegister] = 1.0f;
        values_[kParamRegisterLow] = 55.0f;
        values_[kParamRegisterHigh] = 82.0f;
        values_[kParamDensity] = 0.5f;
        values_[kParamRisk] = 0.35f;
        values_[kParamHumanize] = 0.0f;
        values_[kParamSource] = 0.0f;
        values_[kParamStatusReady] = 1.0f;
        values_[kParamConnectionStatus] = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
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
        if (buttonPulse_ > 0) {
            --buttonPulse_;
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
        const float leftW = width * 0.58f;
        const float rightW = width - leftW - pad * 3.0f;

        drawPhrasePanel(pad, contentY, leftW, contentH);
        drawRequestPanel(pad * 2.0f + leftW, contentY, rightW, contentH);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            draggingSlider_ = -1;
            return false;
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
                cycleSelector(static_cast<int>(i), 1);
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
                cycleSelector(static_cast<int>(i), ev.delta.getY() > 0.0f ? -1 : 1);
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
    int buttonPulse_ = 0;
    int pulsedButton_ = -1;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(16, 19, 24, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(25, 31, 38, 255);
        rect(0.0f, 0.0f, width, height * 0.30f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(32, 41, 50, 232);
        fill();
        closePath();

        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(238, 242, 245, 255);
        text(x + 20.0f, y + 15.0f, "Sidecar", nullptr);

        fontSize(13.0f);
        fillColor(153, 168, 181, 255);
        text(x + 22.0f, y + 48.0f, "AI-ready phrase player", nullptr);

        drawStatusPill(x + w - 328.0f, y + 16.0f, 144.0f, 30.0f,
                       values_[kParamStatusReady] >= 0.5f ? "Phrase Ready" : "No Phrase",
                       values_[kParamStatusReady] >= 0.5f ? 105 : 204,
                       values_[kParamStatusReady] >= 0.5f ? 184 : 112,
                       values_[kParamStatusReady] >= 0.5f ? 134 : 82);
        drawConnectionPill(x + w - 172.0f, y + 16.0f, 152.0f, 30.0f);
    }

    void drawStatusPill(const float x,
                        const float y,
                        const float w,
                        const float h,
                        const char* label,
                        const int r,
                        const int g,
                        const int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 34);
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

    void drawConnectionPill(const float x, const float y, const float w, const float h)
    {
        const int status = clampi(static_cast<int>(std::lround(values_[kParamConnectionStatus])), 0, 4);
        int r = 136;
        int g = 151;
        int b = 166;
        if (status == 1) {
            r = 99;
            g = 158;
            b = 218;
        } else if (status == 2) {
            r = 105;
            g = 184;
            b = 134;
        } else if (status == 3) {
            r = 218;
            g = 151;
            b = 82;
        } else if (status == 4) {
            r = 204;
            g = 112;
            b = 82;
        }
        drawStatusPill(x, y, w, h, kConnectionNames[status], r, g, b);
    }

    void drawPhrasePanel(const float x, const float y, const float w, const float h)
    {
        drawPanel(x, y, w, h);

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(225, 230, 235, 255);
        text(x + 20.0f, y + 18.0f, "Phrase", nullptr);

        const float innerX = x + 20.0f;
        const float innerY = y + 55.0f;
        const float innerW = w - 40.0f;
        const float columnGap = 16.0f;
        const float rowGap = 18.0f;
        const float rowH = 44.0f;
        const float columnW = (innerW - columnGap) * 0.5f;

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            const float sx = innerX + static_cast<float>(i % 2) * (columnW + columnGap);
            const float sy = innerY + static_cast<float>(i / 2) * (rowH + rowGap);
            sliderRects_[i] = {sx, sy + 18.0f, columnW, 22.0f};
            drawSlider(kSliders[i], sliderRects_[i], values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawRequestPanel(const float x, const float y, const float w, const float h)
    {
        drawPanel(x, y, w, h);

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(225, 230, 235, 255);
        text(x + 20.0f, y + 18.0f, "Request", nullptr);

        const float selectorH = 42.0f;
        const float selectorGap = 10.0f;
        const float selectorColumnGap = 8.0f;
        const float selectorColumnW = (w - 40.0f - selectorColumnGap) * 0.5f;
        const float selectorStartY = y + 55.0f;
        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            const float sx = x + 20.0f + static_cast<float>(i % 2) * (selectorColumnW + selectorColumnGap);
            const float sy = selectorStartY + static_cast<float>(i / 2) * (selectorH + selectorGap);
            const float sw = i == std::size(kSelectors) - 1 && (i % 2) == 0 ? w - 40.0f : selectorColumnW;
            selectorRects_[i] = {sx, sy, sw, selectorH};
            drawSelector(kSelectors[i], selectorRects_[i], selectorValueForDisplay(kSelectors[i], values_[kSelectors[i].index]));
        }

        const float buttonH = 38.0f;
        const float buttonGap = 9.0f;
        const float buttonY = y + h - 20.0f - buttonH * 2.0f - buttonGap;
        const float buttonW = (w - 40.0f - buttonGap) / 2.0f;
        for (std::size_t i = 0; i < std::size(kButtons); ++i) {
            const float bx = x + 20.0f + static_cast<float>(i % 2) * (buttonW + buttonGap);
            const float by = buttonY + static_cast<float>(i / 2) * (buttonH + buttonGap);
            buttonRects_[i] = {bx, by, buttonW, buttonH};
            drawButton(kButtons[i], buttonRects_[i], pulsedButton_ == static_cast<int>(i));
        }
    }

    void drawPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(23, 28, 35, 250);
        fill();
        closePath();
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const float value, const bool active)
    {
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(153, 168, 181, 255);
        text(rect.x, rect.y - 18.0f, def.label, nullptr);

        const std::string valueText = formatSliderValue(def, value);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(225, 230, 235, 255);
        text(rect.x + rect.w, rect.y - 18.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(42, 50, 61, 255);
        fill();
        closePath();

        const float t = clampf((value - def.min) / std::max(0.0001f, def.max - def.min), 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, rect.w * t, rect.h, 10.0f);
        fillColor(active ? 228 : 194, active ? 132 : 96, 86, 255);
        fill();
        closePath();
    }

    void drawSelector(const SelectorDef& def, const Rect& rect, const int value)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 14.0f);
        fillColor(34, 43, 53, 255);
        fill();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(152, 166, 181, 255);
        text(rect.x + 14.0f, rect.y + 8.0f, def.label, nullptr);

        fontSize(14.0f);
        fillColor(236, 240, 243, 255);
        text(rect.x + 14.0f, rect.y + 27.0f, def.items[clampi(value, 0, def.count - 1)], nullptr);

        fontSize(17.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(117, 133, 149, 255);
        text(rect.x + rect.w - 16.0f, rect.y + rect.h * 0.5f + 1.0f, ">", nullptr);
    }

    void drawButton(const ButtonDef& def, const Rect& rect, const bool active)
    {
        const float pulse = active ? static_cast<float>(buttonPulse_) / 8.0f : 0.0f;

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 15.0f);
        fillColor(76 + static_cast<int>(pulse * 28.0f), 96 + static_cast<int>(pulse * 28.0f), 120, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 14.0f);
        strokeColor(164, 186, 208, 112);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(14.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(241, 244, 247, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);
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
        const int next = (value + direction + def.count) % def.count;
        setParameter(def.index, selectorParameterValue(def, next));
    }

    void triggerButton(const int buttonIndex)
    {
        const uint32_t index = kButtons[static_cast<std::size_t>(buttonIndex)].index;
        const float nextValue = std::max(values_[index], 0.0f) + 1.0f;
        editParameter(index, true);
        setParameterValue(index, nextValue);
        editParameter(index, false);
        values_[index] = nextValue;
        if ((index == kParamGenerate || index == kParamRetry) && values_[kParamSource] >= 0.5f) {
            values_[kParamStatusReady] = 0.0f;
            values_[kParamConnectionStatus] = 1.0f;
        } else if (index == kParamGenerate || index == kParamRetry) {
            values_[kParamStatusReady] = 1.0f;
            values_[kParamConnectionStatus] = 0.0f;
        } else if (index == kParamPlay) {
            values_[kParamStatusReady] = 1.0f;
        }
        buttonPulse_ = 8;
        pulsedButton_ = buttonIndex;
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidecarUI)
};

UI* createUI()
{
    return new SidecarUI();
}

END_NAMESPACE_DISTRHO
