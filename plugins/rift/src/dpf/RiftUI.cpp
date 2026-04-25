#include "DistrhoUI.hpp"

#include "rift_engine.hpp"
#include "rift_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using CoreParameters = downspout::rift::Parameters;
using downspout::rift::ActionType;
using downspout::rift::ActionWeights;
using downspout::rift::kActionCount;
using downspout::rift::kActionNames;
using downspout::rift::kParamDamage;
using downspout::rift::kParamDensity;
using downspout::rift::kParamDrift;
using downspout::rift::kParamGrid;
using downspout::rift::kParamHold;
using downspout::rift::kParamMemoryBars;
using downspout::rift::kParamMix;
using downspout::rift::kParamPitch;
using downspout::rift::kParamRecover;
using downspout::rift::kParamScatter;
using downspout::rift::kParamStatusAction;
using downspout::rift::kParamStatusActivity;
using downspout::rift::kParameterCount;

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
};

struct ButtonDef {
    uint32_t index;
    const char* label;
    int red;
    int green;
    int blue;
};

struct ModePreset {
    const char* label;
    const char* detail;
    int red;
    int green;
    int blue;
    CoreParameters parameters;
};

constexpr std::array<SliderDef, 7> kSliders = {{
    {kParamGrid, "Grid", "Blocks per bar", 1.0f, 16.0f},
    {kParamDensity, "Density", "How often blocks mutate", 0.0f, 100.0f},
    {kParamDamage, "Damage", "Bias toward disruptive actions", 0.0f, 100.0f},
    {kParamMemoryBars, "Memory", "Eligible history window", 1.0f, 8.0f},
    {kParamDrift, "Drift", "Slice distance and instability", 0.0f, 100.0f},
    {kParamPitch, "Pitch", "Slip semitone offset", -12.0f, 12.0f},
    {kParamMix, "Mix", "Wet layer amount", 0.0f, 100.0f},
}};

constexpr std::array<ButtonDef, 3> kButtons = {{
    {kParamHold, "Hold", 198, 161, 78},
    {kParamScatter, "Scatter", 183, 86, 78},
    {kParamRecover, "Recover", 79, 145, 117},
}};

constexpr std::array<ModePreset, 6> kModes = {{
    {"Pocket", "low-risk drift", 88, 125, 154, {8.0f, 18.0f, 18.0f, 2.0f, 10.0f, 0.0f, 58.0f, 0.0f}},
    {"Stutter", "tight repeats", 205, 151, 79, {16.0f, 72.0f, 24.0f, 1.0f, 10.0f, 0.0f, 100.0f, 0.0f}},
    {"Flip", "harder turns", 145, 110, 208, {8.0f, 56.0f, 70.0f, 2.0f, 26.0f, 0.0f, 100.0f, 0.0f}},
    {"Smear", "slow melt", 78, 166, 169, {8.0f, 52.0f, 60.0f, 3.0f, 72.0f, 0.0f, 92.0f, 0.0f}},
    {"Slip", "pitched drag", 93, 149, 224, {8.0f, 58.0f, 58.0f, 2.0f, 48.0f, 7.0f, 92.0f, 0.0f}},
    {"Ruin", "full wreck", 196, 82, 82, {16.0f, 90.0f, 90.0f, 4.0f, 86.0f, -5.0f, 100.0f, 0.0f}},
}};

constexpr std::size_t kPreviewBlockCount = 24;

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

[[nodiscard]] std::string formatValue(const SliderDef& def, const float value)
{
    char buf[64];
    if (def.index == kParamGrid || def.index == kParamMemoryBars) {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    } else if (def.index == kParamPitch) {
        std::snprintf(buf, sizeof(buf), "%+d st", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value)));
    }
    return buf;
}

[[nodiscard]] std::string formatBars(const float bars)
{
    char buf[32];
    if (std::fabs(bars - std::round(bars)) < 0.001f)
        std::snprintf(buf, sizeof(buf), "%.0f bars", bars);
    else
        std::snprintf(buf, sizeof(buf), "%.2f bars", bars);
    return buf;
}

struct ActionColor {
    int r;
    int g;
    int b;
};

[[nodiscard]] ActionColor actionColor(const ActionType action)
{
    switch (action) {
    case ActionType::Pass: return {77, 97, 111};
    case ActionType::Repeat: return {207, 151, 79};
    case ActionType::Reverse: return {144, 110, 208};
    case ActionType::Skip: return {196, 82, 82};
    case ActionType::Smear: return {78, 166, 169};
    case ActionType::Slip: return {93, 149, 224};
    }
    return {77, 97, 111};
}

[[nodiscard]] bool parameterMatches(const float lhs, const float rhs)
{
    return std::fabs(lhs - rhs) < 0.01f;
}

}  // namespace

class RiftUI : public UI
{
public:
    RiftUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        buttonPulse_.fill(0);
        modePulse_.fill(0);

        const CoreParameters defaults = downspout::rift::clampParameters(CoreParameters {});
        values_[kParamGrid] = defaults.grid;
        values_[kParamDensity] = defaults.density;
        values_[kParamDamage] = defaults.damage;
        values_[kParamMemoryBars] = defaults.memoryBars;
        values_[kParamDrift] = defaults.drift;
        values_[kParamPitch] = defaults.pitch;
        values_[kParamMix] = defaults.mix;
        values_[kParamHold] = defaults.hold;
        values_[kParamStatusAction] = 0.0f;
        values_[kParamStatusActivity] = 0.0f;

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
        bool needsRepaint = false;
        for (int& pulse : buttonPulse_) {
            if (pulse > 0) {
                --pulse;
                needsRepaint = true;
            }
        }
        for (int& pulse : modePulse_) {
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
        const float pad = 24.0f;
        const float headerH = 96.0f;
        const float modeStripH = 80.0f;
        const float sectionGap = 18.0f;
        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float mainH = contentH - modeStripH - sectionGap;
        const float leftW = 370.0f;
        const float rightX = pad + leftW + 28.0f;
        const float rightW = width - rightX - pad;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);
        drawControls(pad, contentY, leftW, mainH);
        drawVisuals(rightX, contentY, rightW, mainH);
        drawModeStrip(pad, contentY + mainH + sectionGap, width - pad * 2.0f, modeStripH);
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

        for (std::size_t i = 0; i < kButtons.size(); ++i) {
            if (buttonRects_[i].contains(x, y)) {
                handleButton(kButtons[i].index, static_cast<int>(i));
                return true;
            }
        }

        for (std::size_t i = 0; i < kModes.size(); ++i) {
            if (modeRects_[i].contains(x, y)) {
                applyModePreset(static_cast<int>(i));
                return true;
            }
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
    std::array<Rect, kButtons.size()> buttonRects_ {};
    std::array<int, kButtons.size()> buttonPulse_ {};
    std::array<Rect, kModes.size()> modeRects_ {};
    std::array<int, kModes.size()> modePulse_ {};
    int draggingSlider_ = -1;

    [[nodiscard]] CoreParameters currentParameters() const
    {
        CoreParameters parameters;
        parameters.grid = values_[kParamGrid];
        parameters.density = values_[kParamDensity];
        parameters.damage = values_[kParamDamage];
        parameters.memoryBars = values_[kParamMemoryBars];
        parameters.drift = values_[kParamDrift];
        parameters.pitch = values_[kParamPitch];
        parameters.mix = values_[kParamMix];
        parameters.hold = values_[kParamHold];
        return downspout::rift::clampParameters(parameters);
    }

    [[nodiscard]] ActionType currentAction() const
    {
        const int raw = static_cast<int>(std::lround(values_[kParamStatusAction]));
        const int clamped = std::max(0, std::min(raw, static_cast<int>(kActionCount - 1)));
        return static_cast<ActionType>(clamped);
    }

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(11, 16, 24, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(20, 29, 38, 255);
        rect(0.0f, 0.0f, width, height * 0.36f);
        fill();
        closePath();

        beginPath();
        fillColor(193, 92, 79, 18);
        rect(width - 300.0f, 0.0f, 300.0f, height);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(16, 23, 31, 232);
        fill();
        closePath();

        fontSize(34.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(240, 242, 246, 255);
        text(x + 24.0f, y + 18.0f, "Rift", nullptr);

        fontSize(14.0f);
        fillColor(156, 171, 182, 255);
        text(x + 26.0f, y + 58.0f, "Transport-locked buffer damage for rhythmic accidents that still feel intentional", nullptr);

        const ActionType liveAction = currentAction();
        const ActionColor color = actionColor(liveAction);
        drawPill(x + w - 214.0f, y + 18.0f, 188.0f, 30.0f, kActionNames[static_cast<std::size_t>(liveAction)], color);
    }

    void drawPill(const float x, const float y, const float w, const float h, const char* label, const ActionColor color)
    {
        beginPath();
        roundedRect(x, y, w, h, 8.0f);
        fillColor(color.r, color.g, color.b, 34);
        fill();
        strokeColor(color.r, color.g, color.b, 196);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(color.r, color.g, color.b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawControls(const float x, const float y, const float w, const float h)
    {
        const CoreParameters parameters = currentParameters();

        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 233, 238, 255);
        text(x, y, "Performance Controls", nullptr);

        const float buttonY = y + 30.0f;
        const float buttonGap = 12.0f;
        const float buttonW = (w - buttonGap * 2.0f) / 3.0f;
        for (std::size_t i = 0; i < kButtons.size(); ++i) {
            const float bx = x + static_cast<float>(i) * (buttonW + buttonGap);
            buttonRects_[i] = {bx, buttonY, buttonW, 40.0f};
            drawButton(kButtons[i], buttonRects_[i], static_cast<int>(i), parameters);
        }

        const float sliderStartY = buttonY + 58.0f;
        const float rowGap = 10.0f;
        const float rowH = 46.0f;
        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            const float rowY = sliderStartY + static_cast<float>(i) * (rowH + rowGap);
            if (rowY + rowH > y + h) {
                sliderRects_[i] = {-1000.0f, -1000.0f, 0.0f, 0.0f};
                continue;
            }
            sliderRects_[i] = {x, rowY + 26.0f, w, 14.0f};
            drawSlider(kSliders[i], sliderRects_[i], parameters, draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawButton(const ButtonDef& def, const Rect& rect, const int buttonIndex, const CoreParameters& parameters)
    {
        const bool active = (def.index == kParamHold) ? (parameters.hold >= 0.5f) : (buttonPulse_[buttonIndex] > 0);
        const int lift = (def.index == kParamHold && active) || buttonPulse_[buttonIndex] > 0 ? 26 : 0;

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(def.red + lift, def.green + lift, def.blue + lift, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        strokeColor(240, 242, 246, active ? 220 : 90);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(246, 247, 249, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const CoreParameters& parameters, const bool active)
    {
        const float value = valueForIndex(def.index, parameters);

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(227, 232, 236, 255);
        text(rect.x, rect.y - 24.0f, def.label, nullptr);

        fontSize(11.0f);
        fillColor(118, 134, 145, 255);
        text(rect.x, rect.y - 10.0f, def.hint, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(240, 205, 170, 255);
        const std::string valueText = formatValue(def, value);
        text(rect.x + rect.w, rect.y - 24.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(37, 48, 58, 255);
        fill();
        closePath();

        const float t = clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
        beginPath();
        roundedRect(rect.x, rect.y, std::max(14.0f, rect.w * t), rect.h, 8.0f);
        fillColor(active ? 230 : 201, active ? 136 : 118, 73, 255);
        fill();
        closePath();

        beginPath();
        circle(rect.x + rect.w * t, rect.y + rect.h * 0.5f, 7.0f);
        fillColor(248, 239, 229, 255);
        fill();
        closePath();
    }

    void drawVisuals(const float x, const float y, const float w, const float h)
    {
        const CoreParameters parameters = currentParameters();
        const float gap = 18.0f;
        const float summaryH = 108.0f;
        const float previewH = 166.0f;
        drawSummary(x, y, w, summaryH, parameters);
        drawPreview(x, y + summaryH + gap, w, previewH, parameters);
        drawWeights(x, y + summaryH + previewH + gap * 2.0f, w, h - summaryH - previewH - gap * 2.0f, parameters);
    }

    void drawSummary(const float x, const float y, const float w, const float h, const CoreParameters& parameters)
    {
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 233, 238, 255);
        text(x, y, "Live Summary", nullptr);

        const float top = y + 30.0f;
        beginPath();
        roundedRect(x, top, w, h - 30.0f, 8.0f);
        fillColor(15, 23, 31, 212);
        fill();
        closePath();

        const ActionType action = currentAction();
        const char* label = kActionNames[static_cast<std::size_t>(action)];
        const float activity = clampf(values_[kParamStatusActivity], 0.0f, 1.0f);
        const float blockBars = 1.0f / std::max(1.0f, parameters.grid);

        drawMetric(x + 18.0f, top + 16.0f, "Action", label);
        drawMetric(x + 18.0f + w * 0.27f, top + 16.0f, "Block", formatBars(blockBars).c_str());
        drawMetric(x + 18.0f + w * 0.52f, top + 16.0f, "Memory", formatBars(parameters.memoryBars).c_str());

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", activity * 100.0f);
        drawMetric(x + 18.0f, top + 58.0f, "Activity", buf);
        std::snprintf(buf, sizeof(buf), "%.0f%%", parameters.mix);
        drawMetric(x + 18.0f + w * 0.27f, top + 58.0f, "Wet", buf);
        std::snprintf(buf, sizeof(buf), "%.0f%%", parameters.density);
        drawMetric(x + 18.0f + w * 0.52f, top + 58.0f, "Density", buf);
    }

    void drawMetric(const float x, const float y, const char* label, const char* value)
    {
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(108, 125, 137, 255);
        text(x, y, label, nullptr);

        fontSize(16.0f);
        fillColor(239, 242, 245, 255);
        text(x, y + 14.0f, value, nullptr);
    }

    void drawPreview(const float x, const float y, const float w, const float h, const CoreParameters& parameters)
    {
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 233, 238, 255);
        text(x, y, "Preview Lane", nullptr);

        const float top = y + 30.0f;
        beginPath();
        roundedRect(x, top, w, h - 30.0f, 8.0f);
        fillColor(15, 23, 31, 212);
        fill();
        closePath();

        fontSize(12.0f);
        fillColor(140, 156, 168, 255);
        text(x + 18.0f, top + 14.0f, "Conceptual next blocks from the current macro state", nullptr);

        const float stripX = x + 18.0f;
        const float stripY = top + 44.0f;
        const float stripW = w - 36.0f;
        const float blockGap = 3.0f;
        const float blockW = (stripW - blockGap * static_cast<float>(kPreviewBlockCount - 1)) / static_cast<float>(kPreviewBlockCount);
        const float blockH = 50.0f;

        for (std::size_t i = 0; i < kPreviewBlockCount; ++i) {
            ActionType action = downspout::rift::previewActionForBlock(parameters, i);
            if (parameters.hold >= 0.5f && values_[kParamStatusAction] > 0.0f) {
                action = currentAction();
            }
            const ActionColor color = actionColor(action);
            const float bx = stripX + static_cast<float>(i) * (blockW + blockGap);

            beginPath();
            roundedRect(bx, stripY, blockW, blockH, blockW > 12.0f ? 5.0f : 2.0f);
            fillColor(color.r, color.g, color.b, 255);
            fill();
            closePath();
        }

        beginPath();
        rect(stripX, stripY - 8.0f, 2.0f, blockH + 16.0f);
        fillColor(244, 245, 246, 255);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(116, 132, 144, 255);
        text(stripX, stripY + blockH + 12.0f, "Now", nullptr);

        drawPreviewLegend(x + 18.0f, top + 118.0f);
    }

    void drawPreviewLegend(const float x, const float y)
    {
        const std::array<ActionType, kActionCount> actions = {{
            ActionType::Pass, ActionType::Repeat, ActionType::Reverse,
            ActionType::Skip, ActionType::Smear, ActionType::Slip,
        }};

        float cursor = x;
        for (const ActionType action : actions) {
            const ActionColor color = actionColor(action);

            beginPath();
            roundedRect(cursor, y, 14.0f, 14.0f, 3.0f);
            fillColor(color.r, color.g, color.b, 255);
            fill();
            closePath();

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(140, 156, 168, 255);
            const char* label = kActionNames[static_cast<std::size_t>(action)];
            text(cursor + 20.0f, y + 1.0f, label, nullptr);
            cursor += 74.0f;
        }
    }

    void drawWeights(const float x, const float y, const float w, const float h, const CoreParameters& parameters)
    {
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 233, 238, 255);
        text(x, y, "Action Bias", nullptr);

        const float top = y + 30.0f;
        beginPath();
        roundedRect(x, top, w, h - 30.0f, 8.0f);
        fillColor(15, 23, 31, 212);
        fill();
        closePath();

        ActionWeights weights = downspout::rift::calculateActionWeights(parameters);
        const float total = weights.repeat + weights.reverse + weights.skip + weights.smear + weights.slip;
        if (total > 0.0f) {
            weights.repeat /= total;
            weights.reverse /= total;
            weights.skip /= total;
            weights.smear /= total;
            weights.slip /= total;
        }

        const std::array<std::pair<const char*, float>, 5> rows = {{
            {"Repeat", weights.repeat},
            {"Reverse", weights.reverse},
            {"Skip", weights.skip},
            {"Smear", weights.smear},
            {"Slip", weights.slip},
        }};

        const std::array<ActionType, 5> rowActions = {{
            ActionType::Repeat, ActionType::Reverse, ActionType::Skip, ActionType::Smear, ActionType::Slip
        }};

        const float rowY0 = top + 18.0f;
        const float rowGap = 18.0f;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const float rowY = rowY0 + static_cast<float>(i) * rowGap;
            const ActionColor color = actionColor(rowActions[i]);
            drawWeightBar(x + 18.0f, rowY, w - 36.0f, rows[i].first, rows[i].second, color);
        }

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(119, 135, 146, 255);
        text(x + 18.0f, top + h - 62.0f, "Density decides if a block mutates at all. These bars show the split once it does.", nullptr);
    }

    void drawModeStrip(const float x, const float y, const float w, const float h)
    {
        const CoreParameters parameters = currentParameters();
        const int activeMode = matchingModeIndex(parameters);

        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(228, 233, 238, 255);
        text(x, y, "Modes", nullptr);

        fontSize(11.0f);
        fillColor(119, 135, 146, 255);
        text(x + 62.0f, y + 3.0f, "quick parameter recipes for useful failure states", nullptr);

        const float top = y + 26.0f;
        beginPath();
        roundedRect(x, top, w, h - 26.0f, 8.0f);
        fillColor(15, 23, 31, 212);
        fill();
        closePath();

        const float innerX = x + 14.0f;
        const float innerY = top + 12.0f;
        const float innerW = w - 28.0f;
        const float buttonGap = 10.0f;
        const float buttonW = (innerW - buttonGap * static_cast<float>(kModes.size() - 1)) / static_cast<float>(kModes.size());

        for (std::size_t i = 0; i < kModes.size(); ++i) {
            const float bx = innerX + static_cast<float>(i) * (buttonW + buttonGap);
            modeRects_[i] = {bx, innerY, buttonW, 32.0f};
            drawModeButton(kModes[i], modeRects_[i], static_cast<int>(i), activeMode == static_cast<int>(i));
        }
    }

    void drawModeButton(const ModePreset& preset, const Rect& rect, const int modeIndex, const bool active)
    {
        const bool pulsing = modePulse_[modeIndex] > 0;
        const int fillBoost = active ? 18 : (pulsing ? 28 : 0);
        const int borderAlpha = active ? 220 : (pulsing ? 180 : 90);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        fillColor(preset.red + fillBoost, preset.green + fillBoost, preset.blue + fillBoost, active ? 80 : 42);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 8.0f);
        strokeColor(preset.red + fillBoost, preset.green + fillBoost, preset.blue + fillBoost, borderAlpha);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(241, 243, 246, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 6.0f, preset.label, nullptr);

        fontSize(10.0f);
        fillColor(166, 178, 188, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 18.0f, preset.detail, nullptr);
    }

    void drawWeightBar(const float x, const float y, const float w, const char* label, const float weight, const ActionColor color)
    {
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(219, 224, 229, 255);
        text(x, y, label, nullptr);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.0f%%", weight * 100.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(154, 170, 181, 255);
        text(x + w, y, buf, nullptr);

        beginPath();
        roundedRect(x, y + 16.0f, w, 12.0f, 6.0f);
        fillColor(36, 47, 58, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(x, y + 16.0f, std::max(8.0f, w * weight), 12.0f, 6.0f);
        fillColor(color.r, color.g, color.b, 255);
        fill();
        closePath();
    }

    void handleButton(const uint32_t index, const int buttonIndex)
    {
        if (index == kParamHold) {
            const float next = values_[kParamHold] >= 0.5f ? 0.0f : 1.0f;
            commitParameter(index, next);
            return;
        }

        editParameter(index, true);
        setParameterValue(index, 1.0f);
        setParameterValue(index, 0.0f);
        editParameter(index, false);
        buttonPulse_[buttonIndex] = 18;
        repaint();
    }

    [[nodiscard]] bool parametersMatchMode(const CoreParameters& parameters, const ModePreset& preset) const
    {
        return parameterMatches(parameters.grid, preset.parameters.grid) &&
               parameterMatches(parameters.density, preset.parameters.density) &&
               parameterMatches(parameters.damage, preset.parameters.damage) &&
               parameterMatches(parameters.memoryBars, preset.parameters.memoryBars) &&
               parameterMatches(parameters.drift, preset.parameters.drift) &&
               parameterMatches(parameters.pitch, preset.parameters.pitch) &&
               parameterMatches(parameters.mix, preset.parameters.mix);
    }

    [[nodiscard]] int matchingModeIndex(const CoreParameters& parameters) const
    {
        for (std::size_t i = 0; i < kModes.size(); ++i) {
            if (parametersMatchMode(parameters, kModes[i])) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void applyModePreset(const int modeIndex)
    {
        if (modeIndex < 0 || modeIndex >= static_cast<int>(kModes.size())) {
            return;
        }

        const ModePreset& preset = kModes[static_cast<std::size_t>(modeIndex)];
        commitParameter(kParamGrid, preset.parameters.grid, false);
        commitParameter(kParamDensity, preset.parameters.density, false);
        commitParameter(kParamDamage, preset.parameters.damage, false);
        commitParameter(kParamMemoryBars, preset.parameters.memoryBars, false);
        commitParameter(kParamDrift, preset.parameters.drift, false);
        commitParameter(kParamPitch, preset.parameters.pitch, false);
        commitParameter(kParamMix, preset.parameters.mix, false);
        commitParameter(kParamHold, 0.0f, false);
        modePulse_[static_cast<std::size_t>(modeIndex)] = 18;
        repaint();
    }

    [[nodiscard]] float valueForIndex(const uint32_t index, const CoreParameters& parameters) const
    {
        switch (index) {
        case kParamGrid: return parameters.grid;
        case kParamDensity: return parameters.density;
        case kParamDamage: return parameters.damage;
        case kParamMemoryBars: return parameters.memoryBars;
        case kParamDrift: return parameters.drift;
        case kParamPitch: return parameters.pitch;
        case kParamMix: return parameters.mix;
        default: return 0.0f;
        }
    }

    void updateSliderFromPosition(const int sliderIndex, const float x)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kSliders.size())) {
            return;
        }

        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const Rect rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const float normalized = clampf((x - rect.x) / rect.w, 0.0f, 1.0f);
        float value = def.min + normalized * (def.max - def.min);
        value = std::round(value);
        commitParameter(def.index, value);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kSliders.size())) {
            return;
        }

        const SliderDef& def = kSliders[static_cast<std::size_t>(sliderIndex)];
        const CoreParameters parameters = currentParameters();
        const float current = valueForIndex(def.index, parameters);
        const float value = clampf(current + direction, def.min, def.max);
        commitParameter(def.index, value);
    }

    void commitParameter(const uint32_t index, const float value, const bool shouldRepaint = true)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        if (shouldRepaint) {
            repaint();
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RiftUI)
};

UI* createUI()
{
    return new RiftUI();
}

END_NAMESPACE_DISTRHO
