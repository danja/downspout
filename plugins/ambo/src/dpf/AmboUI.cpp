#include "DistrhoUI.hpp"

#include "ambo_engine.hpp"
#include "ambo_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using CoreParameters = downspout::ambo::Parameters;

using downspout::ambo::kParamChain;
using downspout::ambo::kParamBypass;
using downspout::ambo::kParamDelay;
using downspout::ambo::kParamDrive;
using downspout::ambo::kParamFeedback;
using downspout::ambo::kParamMix;
using downspout::ambo::kParamOutput;
using downspout::ambo::kParamShimmer;
using downspout::ambo::kParamSpectral;
using downspout::ambo::kParamStatusFeedback;
using downspout::ambo::kParamStatusWet;
using downspout::ambo::kParamTape;
using downspout::ambo::kParamTime;
using downspout::ambo::kParameterCount;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }

    [[nodiscard]] bool containsExpanded(float px, float py, float amount) const noexcept
    {
        return px >= (x - amount) && px <= (x + w + amount)
            && py >= (y - amount) && py <= (y + h + amount);
    }
};

struct SliderDef {
    uint32_t index;
    const char* label;
    const char* hint;
    float min;
    float max;
    bool bipolar;
};

constexpr std::array<SliderDef, 9> kSliders = {{
    {kParamTime, "Time", "granular smear", 0.0f, 1.0f, false},
    {kParamSpectral, "Spectral", "frozen band haze", 0.0f, 1.0f, false},
    {kParamTape, "Tape", "wear and flutter", 0.0f, 1.0f, false},
    {kParamShimmer, "Shimmer", "bright ambient tank", 0.0f, 1.0f, false},
    {kParamDelay, "Delay", "ping-pong repeats", 0.0f, 1.0f, false},
    {kParamDrive, "Drive", "soft fold saturation", 0.0f, 1.0f, false},
    {kParamFeedback, "Feedback", "crossfed wet return", 0.0f, 0.96f, false},
    {kParamMix, "Mix", "dry to wet balance", 0.0f, 1.0f, false},
    {kParamOutput, "Output", "final trim", -24.0f, 12.0f, true},
}};

constexpr std::array<std::array<uint32_t, 6>, downspout::ambo::kChainCount> kChainParamOrder = {{
    {kParamTime, kParamSpectral, kParamTape, kParamShimmer, kParamDelay, kParamDrive},
    {kParamTape, kParamTime, kParamShimmer, kParamSpectral, kParamDelay, kParamDrive},
    {kParamSpectral, kParamShimmer, kParamTime, kParamDelay, kParamTape, kParamDrive},
    {kParamDrive, kParamTime, kParamSpectral, kParamDelay, kParamShimmer, kParamTape},
}};

constexpr std::array<uint32_t, 3> kUtilityParamOrder = {{
    kParamFeedback,
    kParamMix,
    kParamOutput,
}};

[[nodiscard]] float clampf(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const SliderDef* sliderForIndex(uint32_t index)
{
    for (const SliderDef& slider : kSliders) {
        if (slider.index == index)
            return &slider;
    }
    return nullptr;
}

[[nodiscard]] float parameterValue(const CoreParameters& parameters, uint32_t index)
{
    switch (index) {
    case kParamChain: return parameters.chain;
    case kParamTime: return parameters.time;
    case kParamSpectral: return parameters.spectral;
    case kParamTape: return parameters.tape;
    case kParamShimmer: return parameters.shimmer;
    case kParamDelay: return parameters.delay;
    case kParamDrive: return parameters.drive;
    case kParamFeedback: return parameters.feedback;
    case kParamMix: return parameters.mix;
    case kParamOutput: return parameters.output;
    case kParamBypass: return parameters.bypass;
    default: return 0.0f;
    }
}

void setCoreParameterValue(CoreParameters& parameters, uint32_t index, float value)
{
    switch (index) {
    case kParamChain: parameters.chain = value; break;
    case kParamTime: parameters.time = value; break;
    case kParamSpectral: parameters.spectral = value; break;
    case kParamTape: parameters.tape = value; break;
    case kParamShimmer: parameters.shimmer = value; break;
    case kParamDelay: parameters.delay = value; break;
    case kParamDrive: parameters.drive = value; break;
    case kParamFeedback: parameters.feedback = value; break;
    case kParamMix: parameters.mix = value; break;
    case kParamOutput: parameters.output = value; break;
    case kParamBypass: parameters.bypass = value; break;
    default: break;
    }
}

[[nodiscard]] float defaultParameterValue(uint32_t index)
{
    return parameterValue(downspout::ambo::clampParameters(CoreParameters {}), index);
}

[[nodiscard]] std::string formatValue(const SliderDef& def, float value)
{
    char buf[32];
    if (def.index == kParamOutput)
        std::snprintf(buf, sizeof(buf), "%+.1f dB", value);
    else
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
    return buf;
}

[[nodiscard]] float normalizedFromValue(const SliderDef& def, float value)
{
    if (def.max <= def.min)
        return 0.0f;
    return clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
}

[[nodiscard]] float valueFromNormalized(const SliderDef& def, float normalized)
{
    normalized = clampf(normalized, 0.0f, 1.0f);
    return def.min + normalized * (def.max - def.min);
}

[[nodiscard]] const char* parameterShortName(uint32_t index)
{
    const SliderDef* slider = sliderForIndex(index);
    return slider != nullptr ? slider->label : "";
}

}  // namespace

class AmboUI : public UI
{
public:
    AmboUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        const CoreParameters defaults = downspout::ambo::clampParameters(CoreParameters {});
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
        if (index != kParamStatusWet && index != kParamStatusFeedback) {
            CoreParameters parameters = currentParameters();
            setCoreParameterValue(parameters, index, value);
            parameters = downspout::ambo::clampParameters(parameters);
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
        const float chainH = 220.0f;
        const float contentY = pad + headerH + chainH + 34.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.61f;
        const float rightX = pad * 2.0f + leftW;
        const float rightW = width - rightX - pad;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);
        drawChainPanel(pad, pad + headerH + 16.0f, width - pad * 2.0f, chainH);
        drawSliderPanel(pad, contentY, leftW, contentH);
        drawActivityPanel(rightX, contentY, rightW, contentH);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            draggingSlider_ = -1;
            draggingModule_ = -1;
            draggingField_ = false;
            return false;
        }

        if (fieldRect_.contains(x, y)) {
            if ((ev.mod & kModifierControl) != 0) {
                commitParameter(kParamMix, defaultParameterValue(kParamMix));
                commitParameter(kParamFeedback, defaultParameterValue(kParamFeedback));
                return true;
            }
            draggingField_ = true;
            updateFieldFromPosition(x, y);
            return true;
        }

        for (std::size_t i = 0; i < chainRects_.size(); ++i) {
            if (chainRects_[i].contains(x, y)) {
                commitParameter(kParamChain, static_cast<float>(i));
                return true;
            }
        }


        for (std::size_t i = 0; i < moduleRects_.size(); ++i) {
            if (moduleRects_[i].contains(x, y)) {
                if ((ev.mod & kModifierControl) != 0) {
                    commitParameter(moduleParams_[i], defaultParameterValue(moduleParams_[i]));
                    return true;
                }
                draggingModule_ = static_cast<int>(i);
                updateModuleFromPosition(draggingModule_, y);
                return true;
            }
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].containsExpanded(x, y, 8.0f)) {
                const SliderDef* slider = sliderForIndex(kUtilityParamOrder[i]);
                if (slider == nullptr)
                    return false;
                if ((ev.mod & kModifierControl) != 0) {
                    commitParameter(slider->index, defaultParameterValue(slider->index));
                    return true;
                }
                draggingSlider_ = static_cast<int>(i);
                updateSliderFromPosition(draggingSlider_, x);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingField_) {
            updateFieldFromPosition(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
            return true;
        }


        if (draggingModule_ >= 0) {
            updateModuleFromPosition(draggingModule_, static_cast<float>(ev.pos.getY()));
            return true;
        }

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
            if (sliderRects_[i].containsExpanded(x, y, 8.0f)) {
                const SliderDef* sliderPtr = sliderForIndex(kUtilityParamOrder[i]);
                if (sliderPtr == nullptr)
                    return false;
                const SliderDef& slider = *sliderPtr;
                const float step = slider.index == kParamOutput ? 0.5f : 0.01f;
                const float direction = ev.delta.getY() > 0.0f ? 1.0f : -1.0f;
                const float next = clampf(values_[slider.index] + direction * step, slider.min, slider.max);
                commitParameter(slider.index, next);
                return true;
            }
        }

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kUtilityParamOrder.size()> sliderRects_ {};
    std::array<Rect, downspout::ambo::kChainCount> chainRects_ {};
    std::array<Rect, downspout::ambo::kModuleCount> moduleRects_ {};
    std::array<uint32_t, downspout::ambo::kModuleCount> moduleParams_ {};
    Rect fieldRect_ {};
    int draggingSlider_ = -1;
    int draggingModule_ = -1;
    bool draggingField_ = false;

    [[nodiscard]] CoreParameters currentParameters() const
    {
        CoreParameters parameters;
        parameters.chain = values_[kParamChain];
        parameters.time = values_[kParamTime];
        parameters.spectral = values_[kParamSpectral];
        parameters.tape = values_[kParamTape];
        parameters.shimmer = values_[kParamShimmer];
        parameters.delay = values_[kParamDelay];
        parameters.drive = values_[kParamDrive];
        parameters.feedback = values_[kParamFeedback];
        parameters.mix = values_[kParamMix];
        parameters.output = values_[kParamOutput];
        parameters.bypass = values_[kParamBypass];
        return downspout::ambo::clampParameters(parameters);
    }

    void storeParameters(const CoreParameters& parameters)
    {
        values_[kParamChain] = parameters.chain;
        values_[kParamTime] = parameters.time;
        values_[kParamSpectral] = parameters.spectral;
        values_[kParamTape] = parameters.tape;
        values_[kParamShimmer] = parameters.shimmer;
        values_[kParamDelay] = parameters.delay;
        values_[kParamDrive] = parameters.drive;
        values_[kParamFeedback] = parameters.feedback;
        values_[kParamMix] = parameters.mix;
        values_[kParamOutput] = parameters.output;
        values_[kParamBypass] = parameters.bypass;
    }

    void drawBackground(float width, float height)
    {
        beginPath();
        fillColor(13, 17, 22, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(21, 29, 35, 255);
        rect(0.0f, 0.0f, width, height * 0.34f);
        fill();
        closePath();

        beginPath();
        fillColor(51, 102, 99, 30);
        rect(width - 340.0f, 0.0f, 340.0f, height);
        fill();
        closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(26, 35, 45, 240);
        fill();
        closePath();

        fontSize(31.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 242, 245, 255);
        text(x + 20.0f, y + 15.0f, "Ambo", nullptr);

        fontSize(13.0f);
        fillColor(160, 174, 184, 255);
        text(x + 22.0f, y + 53.0f, "Ambient module chain with time smear, spectral haze, shimmer, delay, tape, drive, and feedback", nullptr);

    }

    void drawChainPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(22, 28, 36, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(230, 235, 239, 255);
        text(x + 18.0f, y + 15.0f, "Chain", nullptr);

        const int activeChain = clampi(static_cast<int>(std::lround(values_[kParamChain])),
                                       0,
                                       static_cast<int>(downspout::ambo::kChainCount) - 1);
        const float tabX = x + 92.0f;
        const float tabY = y + 12.0f;
        const float tabGap = 8.0f;
        const float tabW = 100.0f;
        const float tabH = 28.0f;
        for (std::size_t chain = 0; chain < downspout::ambo::kChainCount; ++chain) {
            const bool active = static_cast<int>(chain) == activeChain;
            const float tx = tabX + static_cast<float>(chain) * (tabW + tabGap);
            chainRects_[chain] = {tx, tabY, tabW, tabH};

            beginPath();
            roundedRect(tx, tabY, tabW, tabH, 8.0f);
            fillColor(active ? 42 : 29, active ? 64 : 38, active ? 66 : 45, 255);
            fill();
            strokeColor(active ? 116 : 64, active ? 205 : 88, active ? 181 : 96, active ? 230 : 120);
            strokeWidth(active ? 1.5f : 1.0f);
            stroke();
            closePath();

            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(active ? 232 : 171, active ? 243 : 185, active ? 236 : 190, 255);
            text(tx + tabW * 0.5f, tabY + tabH * 0.5f + 1.0f, downspout::ambo::kChainNames[chain], nullptr);
        }

        const float laneX = x + 18.0f;
        const float laneY = y + 58.0f;
        const float gap = 12.0f;
        const float cellW = (w - 36.0f - gap * 5.0f)
            / static_cast<float>(downspout::ambo::kModuleCount);
        const float cellH = h - 78.0f;
        const std::array<uint32_t, 6>& order = kChainParamOrder[static_cast<std::size_t>(activeChain)];

        for (std::size_t module = 0; module < order.size(); ++module) {
            const float cellX = laneX + static_cast<float>(module) * (cellW + gap);
            const float level = parameterValue(currentParameters(), order[module]);
            moduleRects_[module] = {cellX, laneY, cellW, cellH};
            moduleParams_[module] = order[module];

            beginPath();
            roundedRect(cellX, laneY, cellW, cellH, 14.0f);
            fillColor(27, 38, 44, 255);
            fill();
            strokeColor(80, 111, 113, 150);
            strokeWidth(1.0f);
            stroke();
            closePath();

            const float controlTop = laneY + 51.0f;
            const float controlBottom = laneY + cellH - 15.0f;
            const float handleY = controlBottom - (controlBottom - controlTop) * level;
            beginPath();
            roundedRect(cellX + cellW * 0.5f - 3.0f, controlTop, 6.0f, controlBottom - controlTop, 3.0f);
            fillColor(42, 57, 63, 255);
            fill();
            closePath();

            beginPath();
            roundedRect(cellX + 14.0f, handleY - 3.0f, cellW - 28.0f, 6.0f, 3.0f);
            fillColor(draggingModule_ == static_cast<int>(module) ? 233 : 181,
                      draggingModule_ == static_cast<int>(module) ? 217 : 205,
                      174,
                      255);
            fill();
            closePath();

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(132, 148, 154, 255);
            const std::string indexText = std::to_string(module + 1);
            text(cellX + 12.0f, laneY + 10.0f, indexText.c_str(), nullptr);

            fontSize(16.0f);
            fillColor(229, 237, 234, 255);
            text(cellX + 12.0f, laneY + 32.0f, parameterShortName(order[module]), nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(188, 210, 201, 255);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.0f%%", level * 100.0f);
            text(cellX + cellW - 12.0f, laneY + cellH - 24.0f, buf, nullptr);
        }
    }

    void drawSliderPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(22, 28, 36, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(230, 235, 239, 255);
        text(x + 18.0f, y + 16.0f, "Feedback / Mix / Output", nullptr);

        const float rowH = 92.0f;
        const float startY = y + 73.0f;
        for (std::size_t i = 0; i < kUtilityParamOrder.size(); ++i) {
            const SliderDef* slider = sliderForIndex(kUtilityParamOrder[i]);
            if (slider == nullptr)
                continue;
            const float sx = x + 18.0f;
            const float sy = startY + static_cast<float>(i) * rowH;
            sliderRects_[i] = {sx, sy + 28.0f, w - 36.0f, 14.0f};
            drawSlider(*slider, sliderRects_[i], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& slider, const Rect& rect, bool active)
    {
        const float value = values_[slider.index];
        const float t = normalizedFromValue(slider, value);
        constexpr float knobRadius = 7.0f;
        const float usableW = std::max(1.0f, rect.w - knobRadius * 2.0f);
        const float handleX = rect.x + knobRadius + usableW * t;

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(225, 230, 234, 255);
        text(rect.x, rect.y - 27.0f, slider.label, nullptr);

        fontSize(10.0f);
        fillColor(125, 140, 150, 255);
        text(rect.x, rect.y - 12.0f, slider.hint, nullptr);

        const std::string valueText = formatValue(slider, value);
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(active ? 235 : 199, active ? 220 : 201, active ? 168 : 181, 255);
        text(rect.x + rect.w, rect.y - 27.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(36, 45, 53, 255);
        fill();
        closePath();

        if (slider.bipolar) {
            const float zero = normalizedFromValue(slider, 0.0f);
            const float zeroX = rect.x + knobRadius + usableW * zero;
            const float fillX = std::min(zeroX, handleX);
            const float fillW = std::fabs(handleX - zeroX);
            beginPath();
            roundedRect(fillX, rect.y, std::max(6.0f, fillW), rect.h, 7.0f);
            fillColor(active ? 221 : 186, active ? 176 : 141, 95, 255);
            fill();
            closePath();
        } else {
            beginPath();
            roundedRect(rect.x, rect.y, std::max(7.0f, handleX - rect.x), rect.h, 7.0f);
            fillColor(active ? 114 : 86, active ? 203 : 159, active ? 182 : 155, 255);
            fill();
            closePath();
        }

        beginPath();
        circle(handleX, rect.y + rect.h * 0.5f, knobRadius);
        fillColor(241, 244, 238, 255);
        fill();
        closePath();
    }

    void drawActivityPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(22, 28, 36, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(230, 235, 239, 255);
        text(x + 18.0f, y + 16.0f, "Mix / Feedback XY", nullptr);

        fieldRect_ = {x + 24.0f, y + 66.0f, w - 48.0f, h - 120.0f};
        const float mix = clampf(values_[kParamMix], 0.0f, 1.0f);
        const float feedbackControl = clampf(values_[kParamFeedback] / 0.96f, 0.0f, 1.0f);
        constexpr float handleInset = 10.0f;
        const float usableW = std::max(1.0f, fieldRect_.w - handleInset * 2.0f);
        const float usableH = std::max(1.0f, fieldRect_.h - handleInset * 2.0f);
        const float px = fieldRect_.x + handleInset + usableW * mix;
        const float py = fieldRect_.y + handleInset + usableH * (1.0f - feedbackControl);

        beginPath();
        roundedRect(fieldRect_.x, fieldRect_.y, fieldRect_.w, fieldRect_.h, 14.0f);
        fillColor(17, 23, 29, 255);
        fill();
        strokeColor(draggingField_ ? 122 : 72, draggingField_ ? 210 : 111, draggingField_ ? 187 : 122, draggingField_ ? 230 : 140);
        strokeWidth(draggingField_ ? 2.0f : 1.0f);
        stroke();
        closePath();

        for (int i = 1; i < 4; ++i) {
            const float tx = fieldRect_.x + fieldRect_.w * static_cast<float>(i) / 4.0f;
            const float ty = fieldRect_.y + fieldRect_.h * static_cast<float>(i) / 4.0f;

            beginPath();
            moveTo(tx, fieldRect_.y + 10.0f);
            lineTo(tx, fieldRect_.y + fieldRect_.h - 10.0f);
            strokeColor(46, 60, 68, 120);
            strokeWidth(1.0f);
            stroke();
            closePath();

            beginPath();
            moveTo(fieldRect_.x + 10.0f, ty);
            lineTo(fieldRect_.x + fieldRect_.w - 10.0f, ty);
            strokeColor(46, 60, 68, 120);
            strokeWidth(1.0f);
            stroke();
            closePath();
        }

        beginPath();
        moveTo(px, fieldRect_.y + 10.0f);
        lineTo(px, fieldRect_.y + fieldRect_.h - 10.0f);
        moveTo(fieldRect_.x + 10.0f, py);
        lineTo(fieldRect_.x + fieldRect_.w - 10.0f, py);
        strokeColor(188, 212, 204, 120);
        strokeWidth(1.0f);
        stroke();
        closePath();

        beginPath();
        circle(px, py, draggingField_ ? 12.0f : 10.0f);
        fillColor(228, 235, 221, 245);
        fill();
        strokeColor(92, 188, 170, 240);
        strokeWidth(2.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(138, 153, 161, 255);
        text(fieldRect_.x, fieldRect_.y + fieldRect_.h + 12.0f, "X: Mix", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(fieldRect_.x + fieldRect_.w, fieldRect_.y + fieldRect_.h + 12.0f, "Y: Feedback", nullptr);
    }

    void updateSliderFromPosition(int sliderIndex, float x)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(sliderRects_.size()))
            return;

        const SliderDef* sliderPtr = sliderForIndex(kUtilityParamOrder[static_cast<std::size_t>(sliderIndex)]);
        if (sliderPtr == nullptr)
            return;

        const SliderDef& slider = *sliderPtr;
        const Rect& rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        constexpr float knobRadius = 7.0f;
        const float usableW = rect.w - knobRadius * 2.0f;
        const float normalized = usableW > 0.0f ? (x - rect.x - knobRadius) / usableW : 0.0f;
        const float value = valueFromNormalized(slider, normalized);
        commitParameter(slider.index, value);
    }

    void updateModuleFromPosition(int moduleIndex, float y)
    {
        if (moduleIndex < 0 || moduleIndex >= static_cast<int>(moduleRects_.size()))
            return;

        const Rect& rect = moduleRects_[static_cast<std::size_t>(moduleIndex)];
        const float controlTop = rect.y + 51.0f;
        const float controlBottom = rect.y + rect.h - 15.0f;
        const float usableH = controlBottom - controlTop;
        const float normalized = usableH > 0.0f ? (controlBottom - y) / usableH : 0.0f;
        const uint32_t parameter = moduleParams_[static_cast<std::size_t>(moduleIndex)];
        const SliderDef* slider = sliderForIndex(parameter);
        if (slider != nullptr)
            commitParameter(parameter, valueFromNormalized(*slider, normalized));
    }

    void updateFieldFromPosition(float x, float y)
    {
        constexpr float handleInset = 10.0f;
        const float usableW = fieldRect_.w - handleInset * 2.0f;
        const float usableH = fieldRect_.h - handleInset * 2.0f;
        const float mix = usableW > 0.0f
            ? clampf((x - fieldRect_.x - handleInset) / usableW, 0.0f, 1.0f)
            : values_[kParamMix];
        const float feedbackNormalized = usableH > 0.0f
            ? clampf(1.0f - ((y - fieldRect_.y - handleInset) / usableH), 0.0f, 1.0f)
            : values_[kParamFeedback] / 0.96f;
        const float feedback = feedbackNormalized * 0.96f;

        commitParameter(kParamMix, mix);
        commitParameter(kParamFeedback, feedback);
    }

    void commitParameter(uint32_t index, float value)
    {
        CoreParameters parameters = currentParameters();
        setCoreParameterValue(parameters, index, value);
        parameters = downspout::ambo::clampParameters(parameters);
        const float committed = parameterValue(parameters, index);

        values_[index] = committed;
        editParameter(index, true);
        setParameterValue(index, committed);
        editParameter(index, false);
        storeParameters(parameters);
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AmboUI)
};

UI* createUI()
{
    return new AmboUI();
}

END_NAMESPACE_DISTRHO
