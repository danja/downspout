#include "DistrhoUI.hpp"

#include "orchid_engine.hpp"
#include "orchid_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using CoreParameters = downspout::orchid::Parameters;

using downspout::orchid::kParamCooldownMs;
using downspout::orchid::kParamCaptureTiming;
using downspout::orchid::kParamGrid;
using downspout::orchid::kParamHoldUnits;
using downspout::orchid::kParamLiveUnder;
using downspout::orchid::kParamLoopPeriods;
using downspout::orchid::kParamMix;
using downspout::orchid::kParamPeriodicity;
using downspout::orchid::kParamPitchHigh;
using downspout::orchid::kParamPitchLow;
using downspout::orchid::kParamReleaseMs;
using downspout::orchid::kParamRetrigger;
using downspout::orchid::kParamSensitivity;
using downspout::orchid::kParamStabilityMs;
using downspout::orchid::kParamStatusConfidence;
using downspout::orchid::kParamStatusHoldProgress;
using downspout::orchid::kParamStatusPitch;
using downspout::orchid::kParamStatusRms;
using downspout::orchid::kParamStatusState;
using downspout::orchid::kParamStatusTransport;
using downspout::orchid::kParameterCount;
using downspout::orchid::kProcessorStateNames;

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

enum class ValueKind {
    Percent,
    Milliseconds,
    Hertz,
    Integer,
    Choice,
};

struct SliderDef {
    std::uint32_t index;
    const char* label;
    const char* hint;
    float min;
    float max;
    ValueKind kind;
};

constexpr std::array<SliderDef, 14> kSliders = {{
    {kParamSensitivity, "Sensitivity", "input gate", 0.0f, 100.0f, ValueKind::Percent},
    {kParamPeriodicity, "Periodicity", "voicing threshold", 0.0f, 100.0f, ValueKind::Percent},
    {kParamStabilityMs, "Stability", "stable before capture", 5.0f, 250.0f, ValueKind::Milliseconds},
    {kParamPitchLow, "Pitch Low", "detector floor", 40.0f, 800.0f, ValueKind::Hertz},
    {kParamPitchHigh, "Pitch High", "detector ceiling", 120.0f, 2000.0f, ValueKind::Hertz},
    {kParamCaptureTiming, "Timing", "capture start", 0.0f, 1.0f, ValueKind::Choice},
    {kParamRetrigger, "Retrigger", "replace stronger tones", 0.0f, 100.0f, ValueKind::Percent},
    {kParamGrid, "Grid", "blocks per bar", 1.0f, 16.0f, ValueKind::Integer},
    {kParamHoldUnits, "Hold", "grid units", 1.0f, 8.0f, ValueKind::Integer},
    {kParamReleaseMs, "Release", "fade back", 2.0f, 500.0f, ValueKind::Milliseconds},
    {kParamCooldownMs, "Cooldown", "recapture gap", 0.0f, 1000.0f, ValueKind::Milliseconds},
    {kParamLoopPeriods, "Loop Periods", "captured cycles", 2.0f, 16.0f, ValueKind::Integer},
    {kParamMix, "Mix", "held loop level", 0.0f, 100.0f, ValueKind::Percent},
    {kParamLiveUnder, "Live Under", "dry during hold", 0.0f, 100.0f, ValueKind::Percent},
}};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const SliderDef* sliderForIndex(const std::uint32_t index)
{
    for (const SliderDef& slider : kSliders) {
        if (slider.index == index)
            return &slider;
    }
    return nullptr;
}

[[nodiscard]] float normalizedFromValue(const SliderDef& slider, const float value)
{
    if (slider.max <= slider.min)
        return 0.0f;
    return clampf((value - slider.min) / (slider.max - slider.min), 0.0f, 1.0f);
}

[[nodiscard]] float valueFromNormalized(const SliderDef& slider, float normalized)
{
    normalized = clampf(normalized, 0.0f, 1.0f);
    float value = slider.min + normalized * (slider.max - slider.min);
    if (slider.kind == ValueKind::Integer || slider.kind == ValueKind::Choice)
        value = static_cast<float>(std::lround(value));
    return value;
}

[[nodiscard]] std::string formatValue(const SliderDef& slider, const float value)
{
    char buf[48];
    switch (slider.kind) {
    case ValueKind::Percent:
        std::snprintf(buf, sizeof(buf), "%.0f%%", value);
        break;
    case ValueKind::Milliseconds:
        std::snprintf(buf, sizeof(buf), "%.0f ms", value);
        break;
    case ValueKind::Hertz:
        std::snprintf(buf, sizeof(buf), "%.0f Hz", value);
        break;
    case ValueKind::Integer:
        std::snprintf(buf, sizeof(buf), "%.0f", value);
        break;
    case ValueKind::Choice:
        std::snprintf(buf, sizeof(buf), "%s", value >= 0.5f ? "Grid" : "Immediate");
        break;
    }
    return buf;
}

[[nodiscard]] float parameterValue(const CoreParameters& parameters, const std::uint32_t index)
{
    switch (index) {
    case kParamSensitivity: return parameters.sensitivity;
    case kParamPeriodicity: return parameters.periodicity;
    case kParamStabilityMs: return parameters.stabilityMs;
    case kParamPitchLow: return parameters.pitchLow;
    case kParamPitchHigh: return parameters.pitchHigh;
    case kParamGrid: return parameters.grid;
    case kParamHoldUnits: return parameters.holdUnits;
    case kParamReleaseMs: return parameters.releaseMs;
    case kParamCooldownMs: return parameters.cooldownMs;
    case kParamLoopPeriods: return parameters.loopPeriods;
    case kParamMix: return parameters.mix;
    case kParamLiveUnder: return parameters.liveUnder;
    case kParamCaptureTiming: return parameters.captureTiming;
    case kParamRetrigger: return parameters.retrigger;
    default: return 0.0f;
    }
}

void setCoreParameterValue(CoreParameters& parameters, const std::uint32_t index, const float value)
{
    switch (index) {
    case kParamSensitivity: parameters.sensitivity = value; break;
    case kParamPeriodicity: parameters.periodicity = value; break;
    case kParamStabilityMs: parameters.stabilityMs = value; break;
    case kParamPitchLow: parameters.pitchLow = value; break;
    case kParamPitchHigh: parameters.pitchHigh = value; break;
    case kParamGrid: parameters.grid = value; break;
    case kParamHoldUnits: parameters.holdUnits = value; break;
    case kParamReleaseMs: parameters.releaseMs = value; break;
    case kParamCooldownMs: parameters.cooldownMs = value; break;
    case kParamLoopPeriods: parameters.loopPeriods = value; break;
    case kParamMix: parameters.mix = value; break;
    case kParamLiveUnder: parameters.liveUnder = value; break;
    case kParamCaptureTiming: parameters.captureTiming = value; break;
    case kParamRetrigger: parameters.retrigger = value; break;
    default: break;
    }
}

}  // namespace

class OrchidUI : public UI
{
public:
    OrchidUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        const CoreParameters defaults = downspout::orchid::clampParameters(CoreParameters {});
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

        values_[index] = value;
        if (index < kParamStatusState) {
            CoreParameters parameters = currentParameters();
            setCoreParameterValue(parameters, index, value);
            parameters = downspout::orchid::clampParameters(parameters);
            storeParameters(parameters);
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;
        const float headerH = 94.0f;
        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.61f;
        const float rightX = pad * 2.0f + leftW;
        const float rightW = width - rightX - pad;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);
        drawSliderPanel(pad, contentY, leftW, contentH);
        drawDetectorPanel(rightX, contentY, rightW, contentH);
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
        if (draggingSlider_ < 0)
            return false;

        updateSliderFromPosition(draggingSlider_, static_cast<float>(ev.pos.getX()));
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (!sliderRects_[i].contains(x, y))
                continue;

            const SliderDef& slider = kSliders[i];
            const float direction = ev.delta.getY() > 0.0f ? 1.0f : -1.0f;
            float step = (slider.max - slider.min) * 0.01f;
            if (slider.kind == ValueKind::Integer || slider.kind == ValueKind::Choice)
                step = 1.0f;
            else if (slider.kind == ValueKind::Milliseconds)
                step = slider.max > 600.0f ? 10.0f : 5.0f;
            else if (slider.kind == ValueKind::Hertz)
                step = 10.0f;

            commitParameter(slider.index, values_[slider.index] + direction * step);
            return true;
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
        parameters.sensitivity = values_[kParamSensitivity];
        parameters.periodicity = values_[kParamPeriodicity];
        parameters.stabilityMs = values_[kParamStabilityMs];
        parameters.pitchLow = values_[kParamPitchLow];
        parameters.pitchHigh = values_[kParamPitchHigh];
        parameters.grid = values_[kParamGrid];
        parameters.holdUnits = values_[kParamHoldUnits];
        parameters.releaseMs = values_[kParamReleaseMs];
        parameters.cooldownMs = values_[kParamCooldownMs];
        parameters.loopPeriods = values_[kParamLoopPeriods];
        parameters.mix = values_[kParamMix];
        parameters.liveUnder = values_[kParamLiveUnder];
        parameters.captureTiming = values_[kParamCaptureTiming];
        parameters.retrigger = values_[kParamRetrigger];
        return downspout::orchid::clampParameters(parameters);
    }

    void storeParameters(const CoreParameters& parameters)
    {
        values_[kParamSensitivity] = parameters.sensitivity;
        values_[kParamPeriodicity] = parameters.periodicity;
        values_[kParamStabilityMs] = parameters.stabilityMs;
        values_[kParamPitchLow] = parameters.pitchLow;
        values_[kParamPitchHigh] = parameters.pitchHigh;
        values_[kParamGrid] = parameters.grid;
        values_[kParamHoldUnits] = parameters.holdUnits;
        values_[kParamReleaseMs] = parameters.releaseMs;
        values_[kParamCooldownMs] = parameters.cooldownMs;
        values_[kParamLoopPeriods] = parameters.loopPeriods;
        values_[kParamMix] = parameters.mix;
        values_[kParamLiveUnder] = parameters.liveUnder;
        values_[kParamCaptureTiming] = parameters.captureTiming;
        values_[kParamRetrigger] = parameters.retrigger;
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(15, 18, 22, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(25, 33, 36, 255);
        rect(0.0f, 0.0f, width, height * 0.32f);
        fill();
        closePath();

        beginPath();
        fillColor(91, 122, 116, 24);
        rect(width - 365.0f, 0.0f, 365.0f, height);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 14.0f);
        fillColor(26, 34, 39, 245);
        fill();
        closePath();

        fontSize(32.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 242, 238, 255);
        text(x + 20.0f, y + 14.0f, "Orchid", nullptr);

        fontSize(13.0f);
        fillColor(161, 174, 174, 255);
        text(x + 22.0f, y + 53.0f, "Voiced freeze effect with transport-timed holds", nullptr);

        drawStatusCard(x + w - 456.0f, y + 14.0f, 134.0f, h - 28.0f, "State", stateName(), stateColorR(), stateColorG(), stateColorB());
        drawStatusCard(x + w - 308.0f, y + 14.0f, 134.0f, h - 28.0f, "Pitch", pitchText().c_str(), 102, 154, 210);
        drawStatusCard(x + w - 160.0f, y + 14.0f, 140.0f, h - 28.0f, "Transport", values_[kParamStatusTransport] >= 0.5f ? "Locked" : "Waiting", values_[kParamStatusTransport] >= 0.5f ? 99 : 204, values_[kParamStatusTransport] >= 0.5f ? 184 : 112, values_[kParamStatusTransport] >= 0.5f ? 135 : 82);
    }

    void drawStatusCard(const float x,
                        const float y,
                        const float w,
                        const float h,
                        const char* label,
                        const char* value,
                        const int r,
                        const int g,
                        const int b)
    {
        beginPath();
        roundedRect(x, y, w, h, 10.0f);
        fillColor(17, 22, 27, 255);
        fill();
        strokeColor(r, g, b, 150);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(139, 153, 156, 255);
        text(x + 12.0f, y + 10.0f, label, nullptr);

        fontSize(18.0f);
        fillColor(232, 237, 232, 255);
        text(x + 12.0f, y + 34.0f, value, nullptr);
    }

    void drawSliderPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 14.0f);
        fillColor(22, 28, 34, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 234, 234, 255);
        text(x + 18.0f, y + 16.0f, "Controls", nullptr);

        const float colGap = 28.0f;
        const float colW = (w - 36.0f - colGap) * 0.5f;
        const float rowH = 68.0f;
        const float startY = y + 58.0f;
        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            const int col = static_cast<int>(i % 2u);
            const int row = static_cast<int>(i / 2u);
            const float sx = x + 18.0f + static_cast<float>(col) * (colW + colGap);
            const float sy = startY + static_cast<float>(row) * rowH;
            sliderRects_[i] = {sx, sy + 32.0f, colW, 16.0f};
            drawSlider(kSliders[i], sliderRects_[i], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& slider, const Rect& rect, const bool active)
    {
        const float value = values_[slider.index];
        const float t = normalizedFromValue(slider, value);

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 232, 232, 255);
        text(rect.x, rect.y - 31.0f, slider.label, nullptr);

        fontSize(10.0f);
        fillColor(130, 144, 148, 255);
        text(rect.x, rect.y - 14.0f, slider.hint, nullptr);

        const std::string valueText = formatValue(slider, value);
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(active ? 239 : 203, active ? 213 : 204, active ? 157 : 185, 255);
        text(rect.x + rect.w, rect.y - 31.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(37, 45, 52, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, rect.y, std::max(8.0f, rect.w * t), rect.h, 8.0f);
        fillColor(active ? 221 : 188, active ? 166 : 137, active ? 88 : 82, 255);
        fill();
        closePath();

        beginPath();
        circle(rect.x + rect.w * t, rect.y + rect.h * 0.5f, active ? 8.0f : 7.0f);
        fillColor(241, 244, 235, 255);
        fill();
        strokeColor(77, 154, 147, 230);
        strokeWidth(1.4f);
        stroke();
        closePath();
    }

    void drawDetectorPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 14.0f);
        fillColor(22, 28, 34, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(229, 234, 234, 255);
        text(x + 18.0f, y + 16.0f, "Detector", nullptr);

        drawScope(x + 24.0f, y + 58.0f, w - 48.0f, 190.0f);
        drawMeter(x + 24.0f, y + 288.0f, w - 48.0f, "Confidence", values_[kParamStatusConfidence], 97, 181, 159);
        drawMeter(x + 24.0f, y + 338.0f, w - 48.0f, "Input RMS", clampf(values_[kParamStatusRms] * 8.0f, 0.0f, 1.0f), 102, 154, 210);
        drawMeter(x + 24.0f, y + 388.0f, w - 48.0f, "Hold Progress", values_[kParamStatusHoldProgress], 218, 158, 86);

        const float state = clampf(values_[kParamStatusState] / 4.0f, 0.0f, 1.0f);
        drawStateRail(x + 24.0f, y + h - 116.0f, w - 48.0f, 56.0f, state);
    }

    void drawScope(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 12.0f);
        fillColor(16, 22, 27, 255);
        fill();
        strokeColor(60, 76, 82, 150);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (int i = 1; i < 4; ++i) {
            const float yy = y + h * static_cast<float>(i) / 4.0f;
            beginPath();
            moveTo(x + 12.0f, yy);
            lineTo(x + w - 12.0f, yy);
            strokeColor(43, 56, 62, 130);
            strokeWidth(1.0f);
            stroke();
            closePath();
        }

        const float confidence = clampf(values_[kParamStatusConfidence], 0.0f, 1.0f);
        const float pitchNorm = clampf(values_[kParamStatusPitch] / 1000.0f, 0.0f, 1.0f);
        const float hold = clampf(values_[kParamStatusHoldProgress], 0.0f, 1.0f);

        beginPath();
        for (int i = 0; i < 96; ++i) {
            const float t = static_cast<float>(i) / 95.0f;
            const float wobble = std::sin((t * 8.0f + pitchNorm * 4.0f) * 3.1415926f);
            const float px = x + 14.0f + (w - 28.0f) * t;
            const float py = y + h * 0.55f - wobble * (18.0f + confidence * 48.0f) - hold * h * 0.15f;
            if (i == 0)
                moveTo(px, py);
            else
                lineTo(px, py);
        }
        strokeColor(stateColorR(), stateColorG(), stateColorB(), 230);
        strokeWidth(2.2f);
        stroke();
        closePath();

        beginPath();
        circle(x + w * 0.5f, y + h * 0.5f, 18.0f + confidence * 44.0f);
        fillColor(stateColorR(), stateColorG(), stateColorB(), 30 + static_cast<int>(confidence * 70.0f));
        fill();
        closePath();

        fontSize(34.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(236, 240, 236, 235);
        text(x + w * 0.5f, y + h * 0.48f, pitchText().c_str(), nullptr);

        fontSize(12.0f);
        fillColor(144, 157, 160, 255);
        text(x + w * 0.5f, y + h * 0.72f, stateName(), nullptr);
    }

    void drawMeter(const float x,
                   const float y,
                   const float w,
                   const char* label,
                   const float value,
                   const int r,
                   const int g,
                   const int b)
    {
        const float clamped = clampf(value, 0.0f, 1.0f);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(144, 157, 160, 255);
        text(x, y - 18.0f, label, nullptr);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", clamped * 100.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(203, 208, 204, 255);
        text(x + w, y - 18.0f, buf, nullptr);

        beginPath();
        roundedRect(x, y, w, 14.0f, 7.0f);
        fillColor(38, 46, 53, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y, std::max(7.0f, w * clamped), 14.0f, 7.0f);
        fillColor(r, g, b, 230);
        fill();
        closePath();
    }

    void drawStateRail(const float x, const float y, const float w, const float h, const float stateNorm)
    {
        beginPath();
        roundedRect(x, y, w, h, 12.0f);
        fillColor(17, 22, 27, 255);
        fill();
        strokeColor(63, 78, 84, 150);
        strokeWidth(1.0f);
        stroke();
        closePath();

        const float segmentW = w / static_cast<float>(kProcessorStateNames.size());
        const int active = clampi(static_cast<int>(std::lround(stateNorm * 4.0f)), 0, 4);
        for (std::size_t i = 0; i < kProcessorStateNames.size(); ++i) {
            const float sx = x + segmentW * static_cast<float>(i);
            if (static_cast<int>(i) == active) {
                beginPath();
                roundedRect(sx + 5.0f, y + 8.0f, segmentW - 10.0f, h - 16.0f, 9.0f);
                fillColor(stateColorR(), stateColorG(), stateColorB(), 210);
                fill();
                closePath();
            }

            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(static_cast<int>(i) == active ? 25 : 142, static_cast<int>(i) == active ? 30 : 156, static_cast<int>(i) == active ? 33 : 158, 255);
            text(sx + segmentW * 0.5f, y + h * 0.5f + 1.0f, kProcessorStateNames[i], nullptr);
        }
    }

    [[nodiscard]] const char* stateName() const
    {
        const int state = clampi(static_cast<int>(std::lround(values_[kParamStatusState])), 0, 4);
        return kProcessorStateNames[static_cast<std::size_t>(state)];
    }

    [[nodiscard]] int stateColorR() const
    {
        const int state = clampi(static_cast<int>(std::lround(values_[kParamStatusState])), 0, 4);
        switch (state) {
        case 1: return 205;
        case 2: return 99;
        case 3: return 218;
        case 4: return 126;
        default: return 102;
        }
    }

    [[nodiscard]] int stateColorG() const
    {
        const int state = clampi(static_cast<int>(std::lround(values_[kParamStatusState])), 0, 4);
        switch (state) {
        case 1: return 166;
        case 2: return 184;
        case 3: return 158;
        case 4: return 133;
        default: return 154;
        }
    }

    [[nodiscard]] int stateColorB() const
    {
        const int state = clampi(static_cast<int>(std::lround(values_[kParamStatusState])), 0, 4);
        switch (state) {
        case 1: return 84;
        case 2: return 135;
        case 3: return 86;
        case 4: return 181;
        default: return 210;
        }
    }

    [[nodiscard]] std::string pitchText() const
    {
        const float pitch = values_[kParamStatusPitch];
        if (pitch <= 1.0f)
            return "-- Hz";
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f Hz", pitch);
        return buf;
    }

    void updateSliderFromPosition(const int sliderIndex, const float x)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kSliders.size()))
            return;

        const SliderDef& slider = kSliders[static_cast<std::size_t>(sliderIndex)];
        const Rect& rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const float normalized = rect.w > 0.0f ? (x - rect.x) / rect.w : 0.0f;
        commitParameter(slider.index, valueFromNormalized(slider, normalized));
    }

    void commitParameter(const std::uint32_t index, const float value)
    {
        CoreParameters parameters = currentParameters();
        setCoreParameterValue(parameters, index, value);
        parameters = downspout::orchid::clampParameters(parameters);
        const float committed = parameterValue(parameters, index);

        values_[index] = committed;
        editParameter(index, true);
        setParameterValue(index, committed);
        editParameter(index, false);
        storeParameters(parameters);
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OrchidUI)
};

UI* createUI()
{
    return new OrchidUI();
}

END_NAMESPACE_DISTRHO
