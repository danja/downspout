#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamGranularity = 0,
    kParamMaintain,
    kParamFade,
    kParamCut,
    kParamFadeDurMax,
    kParamBias,
    kParamMute,
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
    const char* hint;
    float min;
    float max;
    bool integer;
};

struct ButtonDef {
    uint32_t index;
    const char* label;
};

constexpr std::array<SliderDef, 6> kSliders = {{
    {kParamGranularity, "Granularity", "Decision window in bars", 1.0f, 32.0f, true},
    {kParamMaintain, "Maintain", "Keep the current state", 0.0f, 100.0f, true},
    {kParamFade, "Fade", "Crossfade to next state", 0.0f, 100.0f, true},
    {kParamCut, "Cut", "Hard switch to next state", 0.0f, 100.0f, true},
    {kParamFadeDurMax, "Fade Dur Max", "Longest fade fraction", 0.125f, 1.0f, false},
    {kParamBias, "Bias", "Target audible ratio", 0.0f, 100.0f, true},
}};

constexpr ButtonDef kMuteButton {kParamMute, "Mute Output"};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] std::string formatValue(const SliderDef& def, const float value)
{
    char buf[64];
    if (def.index == kParamFadeDurMax) {
        std::snprintf(buf, sizeof(buf), "%.3f", value);
    } else if (def.index == kParamBias || def.index == kParamMaintain || def.index == kParamFade || def.index == kParamCut) {
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    }
    return buf;
}

[[nodiscard]] float normalizedWeightSum(const std::array<float, kParameterCount>& values)
{
    return std::max(1.0f, values[kParamMaintain] + values[kParamFade] + values[kParamCut]);
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

} // namespace

class PMixUI : public UI
{
public:
    PMixUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamGranularity] = 4.0f;
        values_[kParamMaintain] = 50.0f;
        values_[kParamFade] = 25.0f;
        values_[kParamCut] = 25.0f;
        values_[kParamFadeDurMax] = 1.0f;
        values_[kParamBias] = 50.0f;
        values_[kParamMute] = 0.0f;

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

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 78.0f);

        const float contentY = pad + 96.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.6f;
        const float rightW = width - leftW - pad * 3.0f;

        drawControlPanel(pad, contentY, leftW, contentH);
        drawInsightPanel(pad * 2.0f + leftW, contentY, rightW, contentH);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) {
            return false;
        }

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (! ev.press) {
            draggingSlider_ = -1;
            return false;
        }

        if (muteButtonRect_.contains(x, y)) {
            toggleMute();
            return true;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                draggingSlider_ = static_cast<int>(i);
                updateSliderFromPosition(draggingSlider_, x);
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

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    Rect muteButtonRect_ {};
    int draggingSlider_ = -1;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(10, 16, 24, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(19, 37, 51, 255);
        rect(0.0f, 0.0f, width, height * 0.32f);
        fill();
        closePath();

        beginPath();
        fillColor(171, 117, 41, 24);
        roundedRect(width - 250.0f, 28.0f, 220.0f, 220.0f, 40.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(18, 28, 39, 235);
        fill();
        closePath();

        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 242, 245, 255);
        text(x + 22.0f, y + 16.0f, "P-Mix", nullptr);

        fontSize(13.0f);
        fillColor(151, 168, 183, 255);
        text(x + 24.0f, y + 50.0f, "Probabilistic transport-locked mixer", nullptr);

        drawPill(x + w - 182.0f, y + 18.0f, 160.0f, 28.0f, "Stereo VST3", 100, 190, 170);
    }

    void drawPill(float x, float y, float w, float h, const char* label, int r, int g, int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 36);
        fill();
        strokeColor(r, g, b, 170);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(r, g, b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawControlPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(17, 23, 31, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 232, 236, 255);
        text(x + 22.0f, y + 18.0f, "Control Surface", nullptr);

        const float innerX = x + 22.0f;
        const float buttonY = y + 52.0f;
        muteButtonRect_ = {innerX, buttonY, w - 44.0f, 40.0f};
        drawToggleButton(kMuteButton, muteButtonRect_, values_[kParamMute] >= 0.5f);

        const float innerY = buttonY + 62.0f;
        const float innerW = w - 44.0f;
        const float rowGap = 18.0f;
        const float rowH = 56.0f;
        const float colGap = 18.0f;
        const float colW = (innerW - colGap) * 0.5f;

        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const float rx = innerX + col * (colW + colGap);
            const float ry = innerY + row * (rowH + rowGap);
            sliderRects_[i] = {rx, ry + 26.0f, colW, 20.0f};
            drawSlider(kSliders[i], sliderRects_[i], values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawToggleButton(const ButtonDef& def, const Rect& rect, const bool active)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(active ? 186 : 45, active ? 82 : 111, active ? 67 : 71, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        strokeColor(active ? 231 : 120, active ? 128 : 180, active ? 118 : 120, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(240, 243, 246, 255);
        text(rect.x + 14.0f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(active ? 255 : 166, active ? 235 : 213, active ? 230 : 185, 255);
        text(rect.x + rect.w - 14.0f,
             rect.y + rect.h * 0.5f + 1.0f,
             active ? "Muted" : "Live",
             nullptr);
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const float value, const bool active)
    {
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(161, 175, 187, 255);
        text(rect.x, rect.y - 24.0f, def.label, nullptr);

        fontSize(11.0f);
        fillColor(113, 130, 145, 255);
        text(rect.x, rect.y + 26.0f, def.hint, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(233, 236, 239, 255);
        const std::string valueText = formatValue(def, value);
        text(rect.x + rect.w, rect.y - 24.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(40, 49, 61, 255);
        fill();
        closePath();

        const float t = clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, std::max(14.0f, rect.w * t), rect.h, 10.0f);
        fillColor(active ? 233 : 196, active ? 147 : 119, 65, 255);
        fill();
        closePath();
    }

    void drawInsightPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 20.0f);
        fillColor(19, 26, 36, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 232, 236, 255);
        text(x + 20.0f, y + 18.0f, "Behavior", nullptr);

        const float weightsY = y + 58.0f;
        drawWeightBar(x + 20.0f, weightsY, w - 40.0f, 20.0f);
        drawStats(x + 20.0f, weightsY + 42.0f, w - 40.0f);
        drawNotes(x + 20.0f, y + h - 158.0f, w - 40.0f, 138.0f);
    }

    void drawWeightBar(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(34, 43, 55, 255);
        fill();
        closePath();

        const float sum = normalizedWeightSum(values_);
        const float maintainW = w * (values_[kParamMaintain] / sum);
        const float fadeW = w * (values_[kParamFade] / sum);
        const float cutW = w * (values_[kParamCut] / sum);

        beginPath();
        roundedRect(x, y, maintainW, h, h * 0.5f);
        fillColor(94, 154, 118, 255);
        fill();
        closePath();

        beginPath();
        rect(x + maintainW, y, fadeW, h);
        fillColor(217, 162, 70, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x + maintainW + fadeW, y, cutW, h, h * 0.5f);
        fillColor(195, 92, 74, 255);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(146, 161, 176, 255);
        text(x, y - 18.0f, "Transition weighting", nullptr);
    }

    void drawStats(float x, float y, float w)
    {
        const float sum = normalizedWeightSum(values_);
        const float maintainPct = values_[kParamMaintain] * 100.0f / sum;
        const float fadePct = values_[kParamFade] * 100.0f / sum;
        const float cutPct = values_[kParamCut] * 100.0f / sum;

        drawMetric(x, y, w, "Maintain share", maintainPct, 94, 154, 118);
        drawMetric(x, y + 38.0f, w, "Fade share", fadePct, 217, 162, 70);
        drawMetric(x, y + 76.0f, w, "Cut share", cutPct, 195, 92, 74);
        drawMetric(x, y + 124.0f, w, "Target play ratio", values_[kParamBias], 109, 170, 210);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(154, 169, 183, 255);
        const std::string cycleText = "Transitions every " + std::to_string(static_cast<int>(std::lround(values_[kParamGranularity]))) + " bar(s)";
        text(x, y + 170.0f, cycleText.c_str(), nullptr);
    }

    void drawMetric(float x, float y, float w, const char* label, float value, int r, int g, int b)
    {
        beginPath();
        roundedRect(x, y + 18.0f, w, 12.0f, 6.0f);
        fillColor(36, 44, 55, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y + 18.0f, w * clampf(value / 100.0f, 0.0f, 1.0f), 12.0f, 6.0f);
        fillColor(r, g, b, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(221, 227, 232, 255);
        text(x, y, label, nullptr);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", value);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(r, g, b, 255);
        text(x + w, y, buf, nullptr);
    }

    void drawNotes(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 16.0f);
        fillColor(14, 20, 27, 255);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 233, 237, 255);
        text(x + 14.0f, y + 14.0f, "Use on running transport.", nullptr);

        fontSize(12.0f);
        fillColor(148, 163, 178, 255);
        text(x + 14.0f, y + 40.0f, "Mute Output forces silence", nullptr);
        text(x + 14.0f, y + 58.0f, "immediately for a quick check.", nullptr);
        text(x + 14.0f, y + 80.0f, "Maintain, Fade, and Cut", nullptr);
        text(x + 14.0f, y + 98.0f, "work as relative weights.", nullptr);
        text(x + 14.0f, y + 120.0f, "Bias sets how often the", nullptr);
        text(x + 14.0f, y + 138.0f, "track should stay audible.", nullptr);
    }

    void updateSliderFromPosition(int sliderIndex, float mouseX)
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

    void nudgeSlider(int sliderIndex, float direction)
    {
        const SliderDef& def = kSliders[sliderIndex];
        const float step = def.integer ? 1.0f : ((def.max - def.min) / 100.0f);
        setParameter(def.index, values_[def.index] + direction * step);
    }

    void setParameter(uint32_t index, float value)
    {
        const SliderDef* def = sliderDefForIndex(index);
        if (def == nullptr) {
            return;
        }

        const float clamped = clampf(value, def->min, def->max);
        commitParameter(index, clamped);
    }

    void toggleMute()
    {
        const float next = values_[kParamMute] >= 0.5f ? 0.0f : 1.0f;
        commitParameter(kParamMute, next);
    }

    void commitParameter(uint32_t index, float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PMixUI)
};

UI* createUI()
{
    return new PMixUI();
}

END_NAMESPACE_DISTRHO
