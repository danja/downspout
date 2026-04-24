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

constexpr int kControlsPerRow = 7;
constexpr std::array<SliderDef, 14> kSliders = {{
    {kParamKey, "KEY", 0.0f, 11.0f, true},
    {kParamScale, "SCALE", 0.0f, 11.0f, true},
    {kParamCycleBars, "BARS", 1.0f, 8.0f, true},
    {kParamGranularity, "GRID", 0.0f, 2.0f, true},
    {kParamComplexity, "COMPLEXITY", 0.0f, 1.0f, false},
    {kParamMovement, "MOVE", 0.0f, 1.0f, false},
    {kParamVary, "VARY", 0.0f, 100.0f, true},
    {kParamComp, "COMP", 0.0f, 100.0f, true},
    {kParamChordSize, "SIZE", 0.0f, 1.0f, true},
    {kParamNoteLength, "LENGTH", 0.10f, 1.0f, false},
    {kParamRegister, "REG", 0.0f, 2.0f, true},
    {kParamSpread, "VOICE", 0.0f, 2.0f, true},
    {kParamPassInput, "PASS", 0.0f, 1.0f, true},
    {kParamOutputChannel, "CHAN", 0.0f, 16.0f, true},
}};

constexpr const char* kNoteNames[12] = {
    "C", "C#", "D", "D#", "E", "F",
    "F#", "G", "G#", "A", "A#", "B"
};

constexpr const char* kScaleNames[12] = {
    "Chrom", "Major", "Nat Min", "Harm Min", "Pent Maj", "Pent Min",
    "Blues", "Dorian", "Mixolyd", "Phryg", "Locrian", "Phryg Dom"
};

constexpr const char* kGranularityNames[3] = {
    "Beat", "Half Bar", "Bar"
};

constexpr const char* kChordSizeNames[2] = {
    "Triads", "Sevenths"
};

constexpr const char* kRegisterNames[3] = {
    "Low", "Mid", "High"
};

constexpr const char* kSpreadNames[3] = {
    "Close", "Open", "Drop-2"
};

constexpr const char* kToggleNames[2] = {
    "Off", "On"
};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const char* label_from_index(const char* const* labels, const int count, int index)
{
    index = clampi(index, 0, count - 1);
    return labels[index];
}

[[nodiscard]] std::string formatValueLabel(const uint32_t index, const float value)
{
    char buffer[48];
    switch (index) {
    case kParamKey:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kNoteNames, 12, static_cast<int>(std::lround(value))));
        break;
    case kParamScale:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kScaleNames, 12, static_cast<int>(std::lround(value))));
        break;
    case kParamCycleBars:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    case kParamGranularity:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kGranularityNames, 3, static_cast<int>(std::lround(value))));
        break;
    case kParamComplexity:
    case kParamMovement:
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
        break;
    case kParamVary:
    case kParamComp:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value)));
        break;
    case kParamChordSize:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kChordSizeNames, 2, static_cast<int>(std::lround(value))));
        break;
    case kParamNoteLength:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
        break;
    case kParamRegister:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kRegisterNames, 3, static_cast<int>(std::lround(value))));
        break;
    case kParamSpread:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kSpreadNames, 3, static_cast<int>(std::lround(value))));
        break;
    case kParamPassInput:
        std::snprintf(buffer, sizeof(buffer), "%s", label_from_index(kToggleNames, 2, static_cast<int>(std::lround(value))));
        break;
    case kParamOutputChannel:
        if (static_cast<int>(std::lround(value)) <= 0)
            std::snprintf(buffer, sizeof(buffer), "Input");
        else
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
        break;
    }
    return buffer;
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
        values_[kParamChordSize] = 0.0f;
        values_[kParamNoteLength] = 1.0f;
        values_[kParamRegister] = 1.0f;
        values_[kParamSpread] = 0.0f;
        values_[kParamPassInput] = 1.0f;
        values_[kParamOutputChannel] = 0.0f;
        values_[kParamStatusReady] = 0.0f;
        values_[kParamVary] = 0.0f;
        values_[kParamComp] = 0.0f;
        learnPulse_ = 0;

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
        if (learnPulse_ > 0) {
            --learnPulse_;
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
        drawControlGroups(pad, pad + 104.0f, width - pad * 2.0f, height - 220.0f);
        drawFooter(pad, height - 94.0f, width - pad * 2.0f, 72.0f);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            activeSlider_ = -1;
            return false;
        }

        if (learnButtonRect_.contains(x, y)) {
            triggerLearn();
            return true;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                activeSlider_ = static_cast<int>(i);
                updateSliderFromY(activeSlider_, y);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0)
            return false;

        updateSliderFromY(activeSlider_, static_cast<float>(ev.pos.getY()));
        return true;
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

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    Rect learnButtonRect_ {};
    int activeSlider_ = -1;
    int learnPulse_ = 0;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(12, 14, 18, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(24, 28, 36, 255);
        rect(0.0f, 0.0f, width, height * 0.28f);
        fill();
        closePath();

        beginPath();
        fillColor(234, 179, 68, 16);
        roundedRect(width - 280.0f, 28.0f, 240.0f, 220.0f, 38.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(22, 27, 35, 236);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, w, 4.0f, 2.0f);
        fillColor(238, 183, 72, 255);
        fill();
        closePath();

        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(242, 244, 247, 255);
        text(x + 22.0f, y + 16.0f, "Cadence", nullptr);

        fontSize(13.0f);
        fillColor(163, 171, 182, 255);
        text(x + 24.0f, y + 50.0f, "Cycle-learned MIDI harmonizer", nullptr);

        drawReadyPill(x + w - 202.0f, y + 20.0f, 178.0f, 30.0f, values_[kParamStatusReady] >= 0.5f);
    }

    void drawReadyPill(const float x, const float y, const float w, const float h, const bool ready)
    {
        const int fr = ready ? 72 : 72;
        const int fg = ready ? 174 : 94;
        const int fb = ready ? 108 : 94;

        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(fr, fg, fb, 36);
        fill();
        strokeColor(fr, fg, fb, 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(ready ? 210 : 178, ready ? 245 : 182, ready ? 220 : 182, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, ready ? "Ready" : "Learning", nullptr);
    }

    void drawControlGroups(const float x, const float y, const float w, const float h)
    {
        const float groupGap = 18.0f;
        const float groupH = (h - groupGap) * 0.5f;

        drawGroup(x, y, w, groupH, 0, "Context");
        drawGroup(x, y + groupH + groupGap, w, groupH, kControlsPerRow, "Voicing And Routing");
    }

    void drawGroup(const float x, const float y, const float w, const float h, const int startIndex, const char* title)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(18, 21, 28, 246);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        strokeColor(42, 48, 58, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(178, 184, 194, 255);
        text(x + w * 0.5f, y + 16.0f, title, nullptr);

        const float innerX = x + 24.0f;
        const float innerY = y + 46.0f;
        const float innerW = w - 48.0f;
        const float colGap = 12.0f;
        const float colW = (innerW - colGap * (kControlsPerRow - 1)) / static_cast<float>(kControlsPerRow);

        for (int i = 0; i < kControlsPerRow; ++i) {
            const int sliderIndex = startIndex + i;
            const float rx = innerX + i * (colW + colGap);
            sliderRects_[static_cast<std::size_t>(sliderIndex)] = {rx, innerY + 22.0f, colW, h - 98.0f};
            drawSlider(kSliders[static_cast<std::size_t>(sliderIndex)],
                       sliderRects_[static_cast<std::size_t>(sliderIndex)],
                       values_[kSliders[static_cast<std::size_t>(sliderIndex)].index],
                       activeSlider_ == sliderIndex);
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const float value, const bool active)
    {
        const float trackW = 28.0f;
        const float trackH = rect.h - 48.0f;
        const float trackX = rect.x + (rect.w - trackW) * 0.5f;
        const float trackY = rect.y + 8.0f;
        const float knobH = 18.0f;

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(218, 222, 228, 255);
        text(rect.x + rect.w * 0.5f, rect.y - 18.0f, def.label, nullptr);

        beginPath();
        roundedRect(trackX, trackY, trackW, trackH, 8.0f);
        fillColor(34, 39, 48, 255);
        fill();
        closePath();

        const float denom = std::max(0.0001f, def.max - def.min);
        const float t = clampf((value - def.min) / denom, 0.0f, 1.0f);
        const float knobY = trackY + (1.0f - t) * (trackH - knobH);

        beginPath();
        roundedRect(trackX - 3.0f, knobY, trackW + 6.0f, knobH, 7.0f);
        fillColor(active ? 241 : 224, active ? 188 : 166, 76, 255);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(187, 193, 202, 255);
        const std::string label = formatValueLabel(def.index, value);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h - 18.0f, label.c_str(), nullptr);
    }

    void drawFooter(const float x, const float y, const float w, const float h)
    {
        const float buttonW = 156.0f;
        const float buttonH = 38.0f;
        learnButtonRect_ = {x + w * 0.5f - buttonW - 12.0f, y + 8.0f, buttonW, buttonH};
        drawLearnButton(learnButtonRect_);

        drawReadyPanel(x + w * 0.5f + 12.0f, y + 8.0f, buttonW, buttonH, values_[kParamStatusReady] >= 0.5f);

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fillColor(132, 139, 148, 255);
        text(x + w * 0.5f,
             y + h,
             "Comp learns rhythmic shape from the input line. Vary evolves the progression at cycle seams. Chan=Input follows the source channel.",
             nullptr);
    }

    void drawLearnButton(const Rect& rect)
    {
        const float pulse = static_cast<float>(learnPulse_) / 8.0f;
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(64 + static_cast<int>(pulse * 28.0f),
                  73 + static_cast<int>(pulse * 24.0f),
                  86 + static_cast<int>(pulse * 20.0f),
                  255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        strokeColor(238, 183, 72, 255);
        strokeWidth(1.5f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(238, 183, 72, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, "Learn", nullptr);
    }

    void drawReadyPanel(const float x, const float y, const float w, const float h, const bool ready)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(ready ? 42 : 34, ready ? 102 : 48, ready ? 58 : 52, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        strokeColor(ready ? 128 : 94, ready ? 220 : 118, ready ? 156 : 124, 255);
        strokeWidth(1.4f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(ready ? 222 : 194, ready ? 244 : 202, ready ? 230 : 206, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, ready ? "Ready" : "Learning", nullptr);
    }

    void triggerLearn()
    {
        editParameter(kParamActionLearn, true);
        setParameterValue(kParamActionLearn, 1.0f);
        editParameter(kParamActionLearn, false);
        values_[kParamStatusReady] = 0.0f;
        learnPulse_ = 8;
        repaint();
    }

    void updateSliderFromY(const int sliderIndex, const float mouseY)
    {
        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const Rect& rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const float trackY = rect.y + 8.0f;
        const float trackH = rect.h - 48.0f;
        const float knobH = 18.0f;
        float t = 1.0f - ((mouseY - trackY - knobH * 0.5f) / std::max(1.0f, trackH - knobH));
        t = clampf(t, 0.0f, 1.0f);
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

    void setParameter(const uint32_t index, float value)
    {
        const SliderDef& def = kSliders[sliderIndexForParameter(index)];
        value = clampf(value, def.min, def.max);
        if (def.integer)
            value = std::round(value);

        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    [[nodiscard]] std::size_t sliderIndexForParameter(const uint32_t index) const
    {
        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            if (kSliders[i].index == index)
                return i;
        }
        return 0;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CadenceUI)
};

UI* createUI()
{
    return new CadenceUI();
}

END_NAMESPACE_DISTRHO
