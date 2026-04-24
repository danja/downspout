#include "DistrhoUI.hpp"

#include "e_mix_engine.hpp"

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
    kParameterCount
};

using CoreParameters = downspout::emix::Parameters;

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
    bool logarithmic;
};

constexpr std::array<SliderDef, 5> kSliders = {{
    {kParamTotalBars, "Total Bars", "Cycle length", 1.0f, 4096.0f, true},
    {kParamDivision, "Division", "Blocks in the cycle", 1.0f, 512.0f, true},
    {kParamSteps, "Steps", "Active Euclidean pulses", 0.0f, 512.0f, false},
    {kParamOffset, "Offset", "Pattern rotation", 0.0f, 511.0f, false},
    {kParamFadeBars, "Fade", "Fade bars inside active blocks", 0.0f, 4096.0f, true},
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const SliderDef* sliderDefForIndex(const uint32_t index)
{
    for (const SliderDef& def : kSliders) {
        if (def.index == index)
            return &def;
    }
    return nullptr;
}

[[nodiscard]] std::string formatInteger(const float value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    return buf;
}

[[nodiscard]] std::string formatBars(const double value)
{
    char buf[64];
    if (std::fabs(value - std::round(value)) < 0.001)
        std::snprintf(buf, sizeof(buf), "%.0f bars", value);
    else
        std::snprintf(buf, sizeof(buf), "%.2f bars", value);
    return buf;
}

[[nodiscard]] std::string formatPercent(const float value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
    return buf;
}

[[nodiscard]] std::string formatValue(const uint32_t index, const CoreParameters& parameters)
{
    switch (index)
    {
    case kParamTotalBars: return formatBars(parameters.totalBars);
    case kParamDivision: return formatInteger(parameters.division);
    case kParamSteps: return formatInteger(parameters.steps);
    case kParamOffset: return formatInteger(parameters.offset);
    case kParamFadeBars: return formatBars(parameters.fadeBars);
    default: return "0";
    }
}

[[nodiscard]] float parameterValue(const CoreParameters& parameters, const uint32_t index)
{
    switch (index)
    {
    case kParamTotalBars: return parameters.totalBars;
    case kParamDivision: return parameters.division;
    case kParamSteps: return parameters.steps;
    case kParamOffset: return parameters.offset;
    case kParamFadeBars: return parameters.fadeBars;
    default: return 0.0f;
    }
}

void setCoreParameterValue(CoreParameters& parameters, const uint32_t index, const float value)
{
    switch (index)
    {
    case kParamTotalBars: parameters.totalBars = value; break;
    case kParamDivision: parameters.division = value; break;
    case kParamSteps: parameters.steps = value; break;
    case kParamOffset: parameters.offset = value; break;
    case kParamFadeBars: parameters.fadeBars = value; break;
    }
}

[[nodiscard]] float peakGain(const CoreParameters& parameters)
{
    const float division = std::max(1.0f, parameters.division);
    const float blockBars = parameters.totalBars / division;
    if (parameters.fadeBars <= 0.0f || blockBars <= 0.0f)
        return 1.0f;
    return std::min(1.0f, blockBars / (2.0f * parameters.fadeBars));
}

[[nodiscard]] float normalizedFromValue(const SliderDef& def, float value, const float minValue, const float maxValue)
{
    if (maxValue <= minValue)
        return 0.0f;

    value = clampf(value, minValue, maxValue);
    if (!def.logarithmic || minValue <= 0.0f)
        return (value - minValue) / (maxValue - minValue);

    const double minLog = std::log(static_cast<double>(minValue));
    const double maxLog = std::log(static_cast<double>(maxValue));
    const double valueLog = std::log(static_cast<double>(value));
    return static_cast<float>((valueLog - minLog) / (maxLog - minLog));
}

[[nodiscard]] float valueFromNormalized(const SliderDef& def, float normalized, const float minValue, const float maxValue)
{
    normalized = clampf(normalized, 0.0f, 1.0f);
    if (maxValue <= minValue)
        return minValue;

    if (!def.logarithmic || minValue <= 0.0f)
        return minValue + normalized * (maxValue - minValue);

    const double minLog = std::log(static_cast<double>(minValue));
    const double maxLog = std::log(static_cast<double>(maxValue));
    const double valueLog = minLog + normalized * (maxLog - minLog);
    return static_cast<float>(std::exp(valueLog));
}

[[nodiscard]] float controlMax(const SliderDef& def, const CoreParameters& parameters)
{
    switch (def.index)
    {
    case kParamSteps:
        return parameters.division;
    case kParamOffset:
        return std::max(0.0f, parameters.division - 1.0f);
    case kParamFadeBars:
        return parameters.totalBars;
    default:
        return def.max;
    }
}

[[nodiscard]] std::string controlRangeText(const SliderDef& def, const CoreParameters& parameters)
{
    const float maxValue = controlMax(def, parameters);
    char buf[64];
    if (def.index == kParamTotalBars || def.index == kParamFadeBars)
        std::snprintf(buf, sizeof(buf), "%.0f-%.0f bars", def.min, maxValue);
    else
        std::snprintf(buf, sizeof(buf), "%.0f-%.0f", def.min, maxValue);
    return buf;
}

[[nodiscard]] float envelopeGain(const CoreParameters& parameters, const float localBar)
{
    const float blockBars = parameters.totalBars / std::max(1.0f, parameters.division);
    if (parameters.fadeBars <= 0.0f)
        return 1.0f;

    const float inGain = clampf(localBar / parameters.fadeBars, 0.0f, 1.0f);
    const float outGain = clampf((blockBars - localBar) / parameters.fadeBars, 0.0f, 1.0f);
    return std::min(inGain, outGain);
}

}  // namespace

class EMixUI : public UI
{
public:
    EMixUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        const CoreParameters defaults = downspout::emix::clampParameters(CoreParameters {});
        storeParameters(defaults);

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= values_.size())
            return;

        CoreParameters parameters = currentParameters();
        setCoreParameterValue(parameters, index, value);
        parameters = downspout::emix::clampParameters(parameters);
        storeParameters(parameters);
        repaint();
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 24.0f;
        const float headerH = 96.0f;
        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = 360.0f;
        const float rightX = pad + leftW + 28.0f;
        const float rightW = width - rightX - pad;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);
        drawControls(pad, contentY, leftW, contentH);
        drawVisuals(rightX, contentY, rightW, contentH);
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
    int draggingSlider_ = -1;

    [[nodiscard]] CoreParameters currentParameters() const
    {
        CoreParameters parameters;
        parameters.totalBars = values_[kParamTotalBars];
        parameters.division = values_[kParamDivision];
        parameters.steps = values_[kParamSteps];
        parameters.offset = values_[kParamOffset];
        parameters.fadeBars = values_[kParamFadeBars];
        return downspout::emix::clampParameters(parameters);
    }

    void storeParameters(const CoreParameters& parameters)
    {
        values_[kParamTotalBars] = parameters.totalBars;
        values_[kParamDivision] = parameters.division;
        values_[kParamSteps] = parameters.steps;
        values_[kParamOffset] = parameters.offset;
        values_[kParamFadeBars] = parameters.fadeBars;
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(12, 18, 26, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(18, 33, 40, 255);
        rect(0.0f, 0.0f, width, height * 0.38f);
        fill();
        closePath();

        beginPath();
        fillColor(210, 129, 47, 18);
        rect(width - 260.0f, 0.0f, 260.0f, height);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(16, 24, 33, 228);
        fill();
        closePath();

        fontSize(34.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 242, 246, 255);
        text(x + 24.0f, y + 18.0f, "E-Mix", nullptr);

        fontSize(14.0f);
        fillColor(155, 172, 183, 255);
        text(x + 26.0f,
             y + 58.0f,
             "Euclidean transport gate with visible block timing and fade shape",
             nullptr);

        drawPill(x + w - 218.0f, y + 18.0f, 190.0f, 30.0f, "Stereo Euclidean Gate");
    }

    void drawPill(float x, float y, float w, float h, const char* label)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(63, 139, 165, 34);
        fill();
        strokeColor(89, 180, 210, 190);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(148, 221, 237, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawControls(const float x, const float y, const float w, const float h)
    {
        const CoreParameters parameters = currentParameters();

        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 234, 239, 255);
        text(x, y, "Pattern Controls", nullptr);

        beginPath();
        rect(x + w + 14.0f, y, 1.0f, h);
        fillColor(57, 71, 82, 255);
        fill();
        closePath();

        const float rowY0 = y + 42.0f;
        const float rowGap = 26.0f;
        const float rowH = 78.0f;
        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            const float rowY = rowY0 + static_cast<float>(i) * (rowH + rowGap);
            sliderRects_[i] = {x, rowY + 38.0f, w, 18.0f};
            drawSlider(kSliders[i], sliderRects_[i], parameters, draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const CoreParameters& parameters, const bool active)
    {
        const float maxValue = controlMax(def, parameters);
        const float value = parameterValue(parameters, def.index);

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 231, 235, 255);
        text(rect.x, rect.y - 34.0f, def.label, nullptr);

        fontSize(11.0f);
        fillColor(122, 139, 151, 255);
        text(rect.x, rect.y - 16.0f, def.hint, nullptr);

        const std::string rangeText = controlRangeText(def, parameters);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(103, 119, 132, 255);
        text(rect.x + rect.w, rect.y - 16.0f, rangeText.c_str(), nullptr);

        const std::string valueText = formatValue(def.index, parameters);
        fontSize(14.0f);
        fillColor(241, 205, 171, 255);
        text(rect.x + rect.w, rect.y - 34.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(37, 49, 60, 255);
        fill();
        closePath();

        const float t = normalizedFromValue(def, value, def.min, maxValue);
        beginPath();
        roundedRect(rect.x, rect.y, std::max(14.0f, rect.w * t), rect.h, 8.0f);
        fillColor(active ? 233 : 205, active ? 145 : 123, 71, 255);
        fill();
        closePath();

        beginPath();
        circle(rect.x + rect.w * t, rect.y + rect.h * 0.5f, 8.0f);
        fillColor(248, 237, 226, 255);
        fill();
        closePath();
    }

    void drawVisuals(const float x, const float y, const float w, const float h)
    {
        const CoreParameters parameters = currentParameters();

        drawSummary(x, y, w, 110.0f, parameters);
        drawPatternStrip(x, y + 144.0f, w, 156.0f, parameters);
        drawEnvelope(x, y + 336.0f, w, 180.0f, parameters);
    }

    void drawSummary(const float x, const float y, const float w, const float h, const CoreParameters& parameters)
    {
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 234, 239, 255);
        text(x, y, "Cycle Summary", nullptr);

        const float top = y + 34.0f;
        beginPath();
        roundedRect(x, top, w, h - 34.0f, 8.0f);
        fillColor(15, 24, 33, 210);
        fill();
        closePath();

        const float blockBars = parameters.totalBars / std::max(1.0f, parameters.division);
        const float density = parameters.steps / std::max(1.0f, parameters.division);
        const float fadeShare = blockBars > 0.0f ? (parameters.fadeBars / blockBars) : 0.0f;

        drawMetric(x + 18.0f, top + 16.0f, "Cycle", formatBars(parameters.totalBars).c_str());
        drawMetric(x + 18.0f + w * 0.32f, top + 16.0f, "Block", formatBars(blockBars).c_str());
        drawMetric(x + 18.0f + w * 0.64f, top + 16.0f, "Density", formatPercent(density).c_str());
        drawMetric(x + 18.0f, top + 58.0f, "Peak Gain", formatPercent(peakGain(parameters)).c_str());
        drawMetric(x + 18.0f + w * 0.32f, top + 58.0f, "Fade Share", formatPercent(fadeShare).c_str());
        drawMetric(x + 18.0f + w * 0.64f, top + 58.0f, "Offset", formatInteger(parameters.offset).c_str());
    }

    void drawMetric(const float x, const float y, const char* label, const char* value)
    {
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(108, 126, 138, 255);
        text(x, y, label, nullptr);

        fontSize(16.0f);
        fillColor(239, 242, 245, 255);
        text(x, y + 14.0f, value, nullptr);
    }

    void drawPatternStrip(const float x, const float y, const float w, const float h, const CoreParameters& parameters)
    {
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 234, 239, 255);
        text(x, y, "Cycle Pattern", nullptr);

        const float top = y + 34.0f;
        beginPath();
        roundedRect(x, top, w, h - 34.0f, 8.0f);
        fillColor(15, 24, 33, 210);
        fill();
        closePath();

        fontSize(12.0f);
        fillColor(144, 160, 171, 255);
        const std::string summary = formatInteger(parameters.steps) + " active / "
                                  + formatInteger(parameters.division) + " total";
        text(x + 18.0f, top + 14.0f, summary.c_str(), nullptr);

        drawLegend(x + w - 172.0f, top + 12.0f, "Active", 205, 123, 71);
        drawLegend(x + w - 88.0f, top + 12.0f, "Silent", 55, 70, 83);

        const int division = static_cast<int>(std::lround(parameters.division));
        float gap = 0.0f;
        if (division <= 24) gap = 4.0f;
        else if (division <= 64) gap = 2.0f;
        else if (division <= 128) gap = 1.0f;

        float stripX = x + 18.0f;
        float stripY = top + 48.0f;
        float stripW = w - 36.0f;
        float blockW = (stripW - gap * static_cast<float>(std::max(0, division - 1))) / static_cast<float>(division);
        if (blockW < 1.0f) {
            gap = 0.0f;
            blockW = stripW / static_cast<float>(division);
        }

        const float blockH = 34.0f;
        for (int i = 0; i < division; ++i) {
            const float bx = stripX + static_cast<float>(i) * (blockW + gap);
            const bool active = downspout::emix::blockIsActive(parameters, i);

            beginPath();
            roundedRect(bx, stripY, std::max(1.0f, blockW), blockH, blockW > 6.0f ? 4.0f : 1.0f);
            if (active)
                fillColor(205, 123, 71, 255);
            else
                fillColor(55, 70, 83, 255);
            fill();
            closePath();
        }

        beginPath();
        rect(stripX, stripY - 10.0f, 2.0f, blockH + 20.0f);
        fillColor(147, 210, 225, 255);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(115, 132, 144, 255);
        text(stripX, stripY + blockH + 10.0f, "Cycle start", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        const std::string blockCount = formatInteger(parameters.division) + " blocks";
        text(stripX + stripW, stripY + blockH + 10.0f, blockCount.c_str(), nullptr);
    }

    void drawLegend(const float x, const float y, const char* label, const int r, const int g, const int b)
    {
        beginPath();
        roundedRect(x, y, 14.0f, 14.0f, 3.0f);
        fillColor(r, g, b, 255);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(141, 158, 169, 255);
        text(x + 20.0f, y + 1.0f, label, nullptr);
    }

    void drawEnvelope(const float x, const float y, const float w, const float h, const CoreParameters& parameters)
    {
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 234, 239, 255);
        text(x, y, "Active Block Envelope", nullptr);

        const float top = y + 34.0f;
        beginPath();
        roundedRect(x, top, w, h - 34.0f, 8.0f);
        fillColor(15, 24, 33, 210);
        fill();
        closePath();

        const float plotX = x + 18.0f;
        const float plotY = top + 24.0f;
        const float plotW = w - 36.0f;
        const float plotH = h - 84.0f;

        beginPath();
        roundedRect(plotX, plotY, plotW, plotH, 8.0f);
        fillColor(20, 30, 39, 255);
        fill();
        closePath();

        for (int i = 1; i < 4; ++i) {
            const float gy = plotY + (plotH * static_cast<float>(i) / 4.0f);
            beginPath();
            moveTo(plotX, gy);
            lineTo(plotX + plotW, gy);
            strokeColor(39, 50, 61, 255);
            strokeWidth(1.0f);
            stroke();
            closePath();
        }

        const float blockBars = parameters.totalBars / std::max(1.0f, parameters.division);
        beginPath();
        moveTo(plotX, plotY + plotH);
        constexpr int pointCount = 64;
        for (int i = 0; i <= pointCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(pointCount);
            const float gain = envelopeGain(parameters, t * blockBars);
            lineTo(plotX + plotW * t, plotY + plotH - gain * plotH);
        }
        lineTo(plotX + plotW, plotY + plotH);
        fillColor(89, 180, 210, 60);
        fill();
        closePath();

        beginPath();
        for (int i = 0; i <= pointCount; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(pointCount);
            const float gain = envelopeGain(parameters, t * blockBars);
            if (i == 0)
                moveTo(plotX + plotW * t, plotY + plotH - gain * plotH);
            else
                lineTo(plotX + plotW * t, plotY + plotH - gain * plotH);
        }
        strokeColor(89, 180, 210, 255);
        strokeWidth(2.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(141, 158, 169, 255);
        text(x + 18.0f, top + h - 78.0f, formatBars(blockBars).c_str(), nullptr);
        const std::string peakText = "Peak " + formatPercent(peakGain(parameters));
        text(x + 18.0f + w * 0.34f, top + h - 78.0f, peakText.c_str(), nullptr);
        const std::string fadeText = "Fade " + formatBars(parameters.fadeBars);
        text(x + 18.0f + w * 0.62f, top + h - 78.0f, fadeText.c_str(), nullptr);
    }

    void updateSliderFromPosition(const int sliderIndex, const float x)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kSliders.size()))
            return;

        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const Rect rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const CoreParameters parameters = currentParameters();
        const float maxValue = controlMax(def, parameters);
        const float normalized = clampf((x - rect.x) / rect.w, 0.0f, 1.0f);
        float value = valueFromNormalized(def, normalized, def.min, maxValue);
        value = std::round(value);
        commitEditedParameters(def.index, value);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kSliders.size()))
            return;

        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const CoreParameters parameters = currentParameters();
        const float maxValue = controlMax(def, parameters);
        const float current = parameterValue(parameters, def.index);
        const float value = clampf(current + direction, def.min, maxValue);
        commitEditedParameters(def.index, value);
    }

    void commitEditedParameters(const uint32_t changedIndex, const float changedValue)
    {
        CoreParameters parameters = currentParameters();
        setCoreParameterValue(parameters, changedIndex, changedValue);
        parameters = downspout::emix::clampParameters(parameters);

        const std::array<float, kParameterCount> nextValues = {{
            parameters.totalBars,
            parameters.division,
            parameters.steps,
            parameters.offset,
            parameters.fadeBars,
        }};

        for (std::size_t i = 0; i < nextValues.size(); ++i) {
            if (std::fabs(values_[i] - nextValues[i]) <= 0.001f)
                continue;

            editParameter(static_cast<uint32_t>(i), true);
            setParameterValue(static_cast<uint32_t>(i), nextValues[i]);
            editParameter(static_cast<uint32_t>(i), false);
            values_[i] = nextValues[i];
        }

        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EMixUI)
};

UI* createUI()
{
    return new EMixUI();
}

END_NAMESPACE_DISTRHO
