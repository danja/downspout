#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamTotalBars = 0,
    kParamDivision,
    kParamSteps,
    kParamOffset,
    kParamFadeBars,
    kParamGranularity,
    kParamMaintain,
    kParamFade,
    kParamCut,
    kParamFadeDurMax,
    kParamBias,
    kParamVelocityFades,
    kParamMute,
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
    const char* hint;
    float min;
    float max;
    bool integer;
};

struct ToggleDef {
    uint32_t index;
    const char* label;
    const char* offLabel;
    const char* onLabel;
};

constexpr std::array<SliderDef, 11> kSliders = {{
    {kParamTotalBars, "Total Bars", "Transport cycle length", 1.0f, 4096.0f, true},
    {kParamDivision, "Division", "Blocks in the cycle", 1.0f, 512.0f, true},
    {kParamSteps, "Steps", "Active Euclidean blocks", 0.0f, 512.0f, true},
    {kParamOffset, "Offset", "Pattern rotation", 0.0f, 511.0f, true},
    {kParamFadeBars, "Block Fade", "Velocity ramp inside active blocks", 0.0f, 4096.0f, false},
    {kParamGranularity, "Granularity", "Probabilistic decision window", 1.0f, 32.0f, true},
    {kParamMaintain, "Maintain", "Keep current pass/block state", 0.0f, 100.0f, false},
    {kParamFade, "Fade", "Move to next state by velocity", 0.0f, 100.0f, false},
    {kParamCut, "Cut", "Hard switch to next state", 0.0f, 100.0f, false},
    {kParamFadeDurMax, "Fade Dur Max", "Longest probabilistic fade share", 0.125f, 1.0f, false},
    {kParamBias, "Open Bias", "Chance the next state is pass", 0.0f, 100.0f, false},
}};

constexpr std::array<ToggleDef, 2> kToggles = {{
    {kParamVelocityFades, "Velocity Fades", "Off", "On"},
    {kParamMute, "Mute", "Live", "Muted"},
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
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

[[nodiscard]] float controlMax(const SliderDef& def, const std::array<float, kParameterCount>& values)
{
    switch (def.index) {
    case kParamSteps: return std::max(0.0f, values[kParamDivision]);
    case kParamOffset: return std::max(0.0f, values[kParamDivision] - 1.0f);
    case kParamFadeBars: return std::max(0.0f, values[kParamTotalBars]);
    default: return def.max;
    }
}

[[nodiscard]] std::string formatValue(const SliderDef& def, const float value)
{
    char buf[64];
    if (def.index == kParamMaintain || def.index == kParamFade || def.index == kParamCut || def.index == kParamBias) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value);
    } else if (def.index == kParamFadeDurMax) {
        std::snprintf(buf, sizeof(buf), "%.3f", value);
    } else if (def.index == kParamFadeBars) {
        std::snprintf(buf, sizeof(buf), "%.2f bars", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    }
    return buf;
}

[[nodiscard]] float weightSum(const std::array<float, kParameterCount>& values)
{
    return std::max(1.0f, values[kParamMaintain] + values[kParamFade] + values[kParamCut]);
}

}  // namespace

class MMixUI : public UI
{
public:
    MMixUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamTotalBars] = 128.0f;
        values_[kParamDivision] = 16.0f;
        values_[kParamSteps] = 8.0f;
        values_[kParamOffset] = 0.0f;
        values_[kParamFadeBars] = 0.0f;
        values_[kParamGranularity] = 4.0f;
        values_[kParamMaintain] = 50.0f;
        values_[kParamFade] = 25.0f;
        values_[kParamCut] = 25.0f;
        values_[kParamFadeDurMax] = 1.0f;
        values_[kParamBias] = 50.0f;
        values_[kParamVelocityFades] = 1.0f;

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

        const float top = pad + 96.0f;
        const float controlsW = width * 0.63f;
        const float sideX = pad * 2.0f + controlsW;
        const float sideW = width - sideX - pad;
        drawControls(pad, top, controlsW, height - top - pad);
        drawSummary(sideX, top, sideW, height - top - pad);
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

        for (std::size_t i = 0; i < toggleRects_.size(); ++i) {
            if (toggleRects_[i].contains(x, y)) {
                toggleParameter(kToggles[i]);
                return true;
            }
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                draggingSlider_ = static_cast<int>(i);
                updateSlider(draggingSlider_, x);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingSlider_ >= 0) {
            updateSlider(draggingSlider_, static_cast<float>(ev.pos.getX()));
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
                const SliderDef& def = kSliders[i];
                const float step = def.integer ? 1.0f : ((controlMax(def, values_) - def.min) / 100.0f);
                commitSlider(def, values_[def.index] + (ev.delta.getY() > 0.0f ? step : -step));
                return true;
            }
        }

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    std::array<Rect, kToggles.size()> toggleRects_ {};
    int draggingSlider_ = -1;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        rect(0.0f, 0.0f, width, height);
        fillColor(13, 17, 22, 255);
        fill();
        closePath();

        beginPath();
        rect(0.0f, 0.0f, width, 156.0f);
        fillColor(24, 36, 43, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(width - 220.0f, 30.0f, 172.0f, 172.0f, 28.0f);
        fillColor(197, 126, 55, 22);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 16.0f);
        fillColor(18, 26, 32, 242);
        fill();
        closePath();

        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(241, 244, 245, 255);
        text(x + 22.0f, y + 15.0f, "M-Mix", nullptr);

        fontSize(13.0f);
        fillColor(161, 177, 185, 255);
        text(x + 24.0f, y + 50.0f,
             DOWNSPOUT_PLUGIN_VERSION_STRING "  |  MIDI pass/block pipeline with Euclidean timing and probabilistic transitions",
             nullptr);
    }

    void drawControls(const float x, const float y, const float w, const float h)
    {
        drawPanel(x, y, w, h, "Controls");

        const float innerX = x + 22.0f;
        const float innerW = w - 44.0f;
        const float colGap = 18.0f;
        const float colW = (innerW - colGap) * 0.5f;
        const float rowH = 52.0f;
        const float rowGap = 16.0f;
        const float startY = y + 58.0f;

        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const float sx = innerX + static_cast<float>(col) * (colW + colGap);
            const float sy = startY + static_cast<float>(row) * (rowH + rowGap);
            sliderRects_[i] = {sx, sy + 22.0f, colW, 18.0f};
            drawSlider(kSliders[i], sliderRects_[i], draggingSlider_ == static_cast<int>(i));
        }

        const float toggleY = y + h - 62.0f;
        for (std::size_t i = 0; i < kToggles.size(); ++i) {
            const float tx = innerX + static_cast<float>(i) * (colW + colGap);
            toggleRects_[i] = {tx, toggleY, colW, 38.0f};
            drawToggle(kToggles[i], toggleRects_[i], values_[kToggles[i].index] >= 0.5f);
        }
    }

    void drawSummary(const float x, const float y, const float w, const float h)
    {
        drawPanel(x, y, w, h, "Gate Shape");
        drawEuclideanStrip(x + 20.0f, y + 58.0f, w - 40.0f, 70.0f);
        drawWeightBar(x + 20.0f, y + 166.0f, w - 40.0f, 22.0f);
        drawMetrics(x + 20.0f, y + 224.0f, w - 40.0f);
        drawModeBox(x + 20.0f, y + h - 132.0f, w - 40.0f, 108.0f);
    }

    void drawPanel(const float x, const float y, const float w, const float h, const char* title)
    {
        beginPath();
        roundedRect(x, y, w, h, 16.0f);
        fillColor(18, 23, 29, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(232, 236, 238, 255);
        text(x + 20.0f, y + 18.0f, title, nullptr);
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const bool active)
    {
        const float maxValue = controlMax(def, values_);
        const float value = clampf(values_[def.index], def.min, maxValue);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(169, 182, 189, 255);
        text(rect.x, rect.y - 22.0f, def.label, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(234, 237, 239, 255);
        const std::string valueText = formatValue(def, value);
        text(rect.x + rect.w, rect.y - 22.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 9.0f);
        fillColor(39, 46, 52, 255);
        fill();
        closePath();

        const float t = maxValue > def.min ? clampf((value - def.min) / (maxValue - def.min), 0.0f, 1.0f) : 0.0f;
        beginPath();
        roundedRect(rect.x, rect.y, std::max(12.0f, rect.w * t), rect.h, 9.0f);
        fillColor(active ? 231 : 90, active ? 155 : 169, active ? 80 : 139, 255);
        fill();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(111, 127, 136, 255);
        text(rect.x, rect.y + 22.0f, def.hint, nullptr);
    }

    void drawToggle(const ToggleDef& def, const Rect& rect, const bool active)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(active ? 66 : 45, active ? 137 : 58, active ? 113 : 68, 255);
        fill();
        closePath();

        strokeColor(active ? 116 : 115, active ? 202 : 126, active ? 172 : 101, 255);
        strokeWidth(1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(236, 239, 240, 255);
        text(rect.x + 13.0f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(active ? 209 : 188, active ? 246 : 194, active ? 232 : 170, 255);
        text(rect.x + rect.w - 13.0f, rect.y + rect.h * 0.5f + 1.0f, active ? def.onLabel : def.offLabel, nullptr);
    }

    void drawEuclideanStrip(const float x, const float y, const float w, const float h)
    {
        const int division = std::max(1, static_cast<int>(std::lround(values_[kParamDivision])));
        const int steps = std::max(0, std::min(division, static_cast<int>(std::lround(values_[kParamSteps]))));
        const int offset = static_cast<int>(std::lround(values_[kParamOffset])) % division;
        const int visible = std::min(division, 32);
        const float gap = 3.0f;
        const float cellW = (w - gap * static_cast<float>(visible - 1)) / static_cast<float>(visible);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(151, 166, 174, 255);
        text(x, y - 24.0f, "Euclidean pass blocks", nullptr);

        for (int i = 0; i < visible; ++i) {
            const int base = (i + offset) % division;
            const bool active = steps >= division || (steps > 0 && ((base * steps) % division) < steps);
            beginPath();
            roundedRect(x + static_cast<float>(i) * (cellW + gap), y, cellW, h, 5.0f);
            fillColor(active ? 80 : 41, active ? 165 : 50, active ? 132 : 58, 255);
            fill();
            closePath();
        }
    }

    void drawWeightBar(const float x, const float y, const float w, const float h)
    {
        const float sum = weightSum(values_);
        const float maintainW = w * values_[kParamMaintain] / sum;
        const float fadeW = w * values_[kParamFade] / sum;
        const float cutW = w * values_[kParamCut] / sum;

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(151, 166, 174, 255);
        text(x, y - 24.0f, "Probabilistic transition weights", nullptr);

        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(39, 46, 52, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, maintainW, h, h * 0.5f);
        fillColor(92, 156, 112, 255);
        fill();
        closePath();

        beginPath();
        rect(x + maintainW, y, fadeW, h);
        fillColor(220, 157, 73, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x + maintainW + fadeW, y, cutW, h, h * 0.5f);
        fillColor(196, 82, 78, 255);
        fill();
        closePath();
    }

    void drawMetrics(const float x, const float y, const float w)
    {
        const float cycleBars = std::max(1.0f, values_[kParamTotalBars]);
        const float division = std::max(1.0f, values_[kParamDivision]);
        const float blockBars = cycleBars / division;
        const float openPct = division > 0.0f ? (values_[kParamSteps] / division) * 100.0f : 0.0f;
        drawMetric(x, y, w, "Euclidean open share", openPct, 80, 165, 132);
        drawMetric(x, y + 42.0f, w, "Open bias", values_[kParamBias], 88, 155, 208);
        drawMetric(x, y + 84.0f, w, "Velocity fade depth", values_[kParamFadeBars] <= 0.0f ? 0.0f : std::min(100.0f, values_[kParamFadeBars] * 100.0f / std::max(0.001f, blockBars)), 220, 157, 73);
    }

    void drawMetric(const float x, const float y, const float w, const char* label, const float percent, const int r, const int g, const int b)
    {
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 230, 255);
        text(x, y, label, nullptr);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", clampf(percent, 0.0f, 100.0f));
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(r, g, b, 255);
        text(x + w, y, buf, nullptr);

        beginPath();
        roundedRect(x, y + 20.0f, w, 11.0f, 6.0f);
        fillColor(39, 46, 52, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y + 20.0f, w * clampf(percent / 100.0f, 0.0f, 1.0f), 11.0f, 6.0f);
        fillColor(r, g, b, 255);
        fill();
        closePath();
    }

    void drawModeBox(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 12.0f);
        fillColor(13, 18, 23, 255);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(231, 235, 237, 255);
        text(x + 14.0f, y + 14.0f, values_[kParamMute] >= 0.5f ? "Output muted" : "Pipeline active", nullptr);

        fontSize(12.0f);
        fillColor(146, 161, 169, 255);
        const std::string fadeMode = values_[kParamVelocityFades] >= 0.5f
            ? "Fades scale note-on velocity."
            : "Fades act as pass/block only.";
        text(x + 14.0f, y + 42.0f, fadeMode.c_str(), nullptr);

        char buf[96];
        std::snprintf(buf,
                      sizeof(buf),
                      "Decisions every %d bar(s).",
                      static_cast<int>(std::lround(values_[kParamGranularity])));
        text(x + 14.0f, y + 64.0f, buf, nullptr);
    }

    void updateSlider(const int sliderIndex, const float mouseX)
    {
        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const Rect& rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const float maxValue = controlMax(def, values_);
        const float t = clampf((mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        float value = def.min + t * (maxValue - def.min);
        if (def.integer) {
            value = std::round(value);
        }
        commitSlider(def, value);
    }

    void commitSlider(const SliderDef& def, const float value)
    {
        const float maxValue = controlMax(def, values_);
        float clamped = clampf(value, def.min, maxValue);
        if (def.integer) {
            clamped = std::round(clamped);
        }

        commitParameter(def.index, clamped);

        if (def.index == kParamDivision) {
            commitParameter(kParamSteps, clampf(std::round(values_[kParamSteps]), 0.0f, values_[kParamDivision]));
            commitParameter(kParamOffset, clampf(std::round(values_[kParamOffset]), 0.0f, std::max(0.0f, values_[kParamDivision] - 1.0f)));
        } else if (def.index == kParamTotalBars) {
            commitParameter(kParamFadeBars, clampf(values_[kParamFadeBars], 0.0f, values_[kParamTotalBars]));
        }
    }

    void toggleParameter(const ToggleDef& def)
    {
        commitParameter(def.index, values_[def.index] >= 0.5f ? 0.0f : 1.0f);
    }

    void commitParameter(const uint32_t index, const float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MMixUI)
};

UI* createUI()
{
    return new MMixUI();
}

END_NAMESPACE_DISTRHO
