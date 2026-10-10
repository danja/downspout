#pragma once

// Magneto-style panel toolkit for a NanoVG DPF UI: flat surface panels with
// accent-coloured headers, horizontal bar sliders, stepper and segmented
// controls, and a dark/light theme toggle, all drawn from downspout/look_and_feel.hpp.
// Shared by retune, sprout, markov and damiano. This header depends on DPF, which is why
// it lives under downspout/dpf/ rather than beside the portable headers.
//
// A plugin derives from MagnetoUI, calls beginFrame()/drawHeader()/drawPanel() and the
// draw* controls from onNanoDisplay(), and handles buttons in onAction(). By default a
// control change goes to the host with setParameterValue(); override sendValue() when a
// plugin carries its values some other way (damiano sends state keys).

#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace kit {

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

struct Accent {
    int r;
    int g;
    int b;
};

struct Range {
    float minimum;
    float maximum;
    bool integer;
};

}  // namespace kit

class MagnetoUI : public UI {
public:
    MagnetoUI(const kit::Range* ranges, const std::uint32_t count, const float* defaults)
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT), ranges_(ranges), count_(std::min<std::uint32_t>(count, 32))
    {
        for (std::uint32_t i = 0; i < count_; ++i)
            values_[i] = defaults[i];
       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    using Rect = kit::Rect;
    using Accent = kit::Accent;

    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < count_) {
            values_[index] = value;
            repaint();
        }
    }

    // A button, stepper half or segment was pressed. `id` is the caller's own number.
    virtual void onAction(int id) = 0;

    // Delivers a value the user just set. Default: a host parameter change.
    virtual void sendValue(const std::uint32_t parameter, const float v) { setParameterValue(parameter, v); }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;
        if (!ev.press) {
            activeSlider_ = -1;
            return false;
        }
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (themeRect_.contains(mx, my)) {
            darkTheme_ = !darkTheme_;
            repaint();
            return true;
        }
        for (std::size_t i = 0; i < actionCount_; ++i) {
            if (actions_[i].bounds.contains(mx, my)) {
                onAction(actions_[i].id);
                return true;
            }
        }
        for (std::size_t i = 0; i < stepperCount_; ++i) {
            if (steppers_[i].bounds.contains(mx, my)) {
                step(steppers_[i].parameter, mx < steppers_[i].bounds.x + steppers_[i].bounds.w * 0.5f ? -1 : 1);
                return true;
            }
        }
        for (std::size_t i = 0; i < segmentCount_; ++i) {
            if (segments_[i].bounds.contains(mx, my)) {
                commit(segments_[i].parameter, static_cast<float>(segments_[i].value));
                return true;
            }
        }
        for (std::size_t i = 0; i < sliderCount_; ++i) {
            if (sliders_[i].bounds.contains(mx, my)) {
                activeSlider_ = static_cast<int>(i);
                fromMouse(sliders_[i], mx);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0 || activeSlider_ >= static_cast<int>(sliderCount_))
            return false;
        fromMouse(sliders_[static_cast<std::size_t>(activeSlider_)], static_cast<float>(ev.pos.getX()));
        return true;
    }

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }
    [[nodiscard]] float value(const std::uint32_t i) const { return i < count_ ? values_[i] : 0.0f; }
    [[nodiscard]] int intValue(const std::uint32_t i) const { return static_cast<int>(std::lround(value(i))); }

    void commit(const std::uint32_t parameter, const float raw)
    {
        const auto& r = ranges_[parameter];
        float v = std::clamp(raw, r.minimum, r.maximum);
        if (r.integer)
            v = std::round(v);
        values_[parameter] = v;
        sendValue(parameter, v);
        repaint();
    }

    // Updates the displayed value without sending it anywhere (for state or host echoes).
    void setValue(const std::uint32_t parameter, const float v)
    {
        if (parameter < count_) {
            values_[parameter] = v;
            repaint();
        }
    }

    // Fades everything drawn until the next call, to show a control or panel is inactive.
    // Controls drawn while dimmed still respond to the mouse.
    void setDim(const bool dim) { globalAlpha(dim ? 0.38f : 1.0f); }

    // ── Frame ───────────────────────────────────────────────────────────────

    void beginFrame()
    {
        sliderCount_ = actionCount_ = stepperCount_ = segmentCount_ = 0;
        const auto& t = theme();
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        beginPath();
        fc(t.background);
        rect(0.0f, 0.0f, width, height);
        fill();
        beginPath();
        fc(t.panel);
        rect(0.0f, 0.0f, width, 96.0f);
        fill();
    }

    void drawHeader(const char* title, const char* subtitle, const char* explainer)
    {
        const auto& t = theme();
        const float width = static_cast<float>(getWidth());
        fc(t.textPrimary);
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(24.0f, 20.0f, title, nullptr);
        fc(t.textDim);
        fontSize(15.0f);
        fontSize(30.0f);
        Rectangle<float> bounds;
        textBounds(0.0f, 0.0f, title, nullptr, bounds);
        fontSize(15.0f);
        text(24.0f + bounds.getWidth() + 18.0f, 30.0f, subtitle, nullptr);
        fontSize(12.0f);
        text(24.0f, 64.0f, explainer, nullptr);

        themeRect_ = {width - 24.0f - 74.0f, 24.0f, 74.0f, 26.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall);
        fill();
        fc(t.textDim);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f, darkTheme_ ? "DARK" : "LIGHT", nullptr);
    }

    void drawPanel(const Rect b, const char* title, const Accent accent, const char* right = nullptr)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusPanel);
        fill();
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        roundedRect(b.x, b.y, b.w, 32.0f, laf::kRadiusPanel);
        fill();
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        rect(b.x, b.y + 20.0f, b.w, 12.0f);
        fill();
        fillColor(250, 248, 242, 255);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(b.x + 12.0f, b.y + 16.0f, title, nullptr);
        if (right != nullptr) {
            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(b.x + b.w - 12.0f, b.y + 16.0f, right, nullptr);
        }
    }

    void label(const float x, const float y, const char* s, const bool dim = true, const float size = 12.0f,
               const int align = ALIGN_LEFT | ALIGN_TOP)
    {
        const auto& t = theme();
        fc(dim ? t.textDim : t.textPrimary);
        fontSize(size);
        textAlign(align);
        text(x, y, s, nullptr);
    }

    // ── Controls ────────────────────────────────────────────────────────────

    void drawSlider(const std::uint32_t parameter, const char* name, const char* shown, const Rect b, const Accent accent)
    {
        const auto& t = theme();
        const auto& r = ranges_[parameter];
        const float normalized = r.maximum > r.minimum ? std::clamp((values_[parameter] - r.minimum) / (r.maximum - r.minimum), 0.0f, 1.0f) : 0.0f;
        label(b.x, b.y, name);
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(b.x + b.w, b.y, shown, nullptr);

        const float trackY = b.y + 17.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(b.x, trackY, b.w, 10.0f, 3.0f);
        fill();
        if (normalized > 0.002f) {
            beginPath();
            fillColor(accent.r, accent.g, accent.b, 255);
            roundedRect(b.x, trackY, b.w * normalized, 10.0f, 3.0f);
            fill();
        }
        if (sliderCount_ < sliders_.size())
            sliders_[sliderCount_++] = {parameter, {b.x, trackY - 9.0f, b.w, 28.0f}};
    }

    void drawStepper(const std::uint32_t parameter, const char* name, const char* shown, const Rect b)
    {
        const auto& t = theme();
        label(b.x, b.y, name);
        const Rect box {b.x, b.y + 15.0f, b.w, 22.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        stroke();
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, shown, nullptr);
        fc(t.textDim);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);
        if (stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {parameter, box};
    }

    // A row of buttons, one per name; the selected one is filled with the accent.
    void drawSegments(const std::uint32_t parameter, const char* const* names, const int count, const Rect b, const Accent accent)
    {
        const auto& t = theme();
        const int selected = intValue(parameter);
        const float gap = 6.0f;
        const float w = (b.w - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        for (int i = 0; i < count; ++i) {
            const Rect box {b.x + static_cast<float>(i) * (w + gap), b.y, w, b.h};
            beginPath();
            if (i == selected)
                fillColor(accent.r, accent.g, accent.b, 255);
            else
                fc(t.buttonFace);
            roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
            fill();
            if (i != selected) {
                beginPath();
                sc(t.border);
                strokeWidth(1.0f);
                roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
                stroke();
            }
            if (i == selected)
                fillColor(250, 248, 242, 255);
            else
                fc(t.textPrimary);
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, names[i], nullptr);
            if (segmentCount_ < segments_.size())
                segments_[segmentCount_++] = {parameter, i, box};
        }
    }

    void drawButton(const int id, const char* name, const Rect b, const Accent accent, const bool filled = true)
    {
        const auto& t = theme();
        beginPath();
        if (filled)
            fillColor(accent.r, accent.g, accent.b, 255);
        else
            fc(t.buttonFace);
        roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusSmall);
        fill();
        if (!filled) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusSmall);
            stroke();
        }
        if (filled)
            fillColor(250, 248, 242, 255);
        else
            fc(t.textPrimary);
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(b.x + b.w * 0.5f, b.y + b.h * 0.5f, name, nullptr);
        if (actionCount_ < actions_.size())
            actions_[actionCount_++] = {id, b};
    }

    void drawPlotFrame(const Rect plot)
    {
        const auto& t = theme();
        beginPath();
        fc(t.panel);
        roundedRect(plot.x, plot.y, plot.w, plot.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(plot.x, plot.y, plot.w, plot.h, laf::kRadiusSmall);
        stroke();
    }

private:
    struct SliderHit {
        std::uint32_t parameter = 0;
        Rect bounds {};
    };
    struct SegmentHit {
        std::uint32_t parameter = 0;
        int value = 0;
        Rect bounds {};
    };
    struct ActionHit {
        int id = 0;
        Rect bounds {};
    };

    void step(const std::uint32_t parameter, const int direction)
    {
        const auto& r = ranges_[parameter];
        float next = values_[parameter] + static_cast<float>(direction);
        const float span = r.maximum - r.minimum + 1.0f;
        if (next < r.minimum) next += span;
        if (next > r.maximum) next -= span;
        commit(parameter, next);
    }

    void fromMouse(const SliderHit& s, const float mouseX)
    {
        const auto& r = ranges_[s.parameter];
        const float n = std::clamp((mouseX - s.bounds.x) / std::max(1.0f, s.bounds.w), 0.0f, 1.0f);
        commit(s.parameter, r.minimum + n * (r.maximum - r.minimum));
    }

    const kit::Range* ranges_;
    std::uint32_t count_;
    std::array<float, 32> values_ {};
    std::array<SliderHit, 32> sliders_ {};
    std::array<ActionHit, 16> actions_ {};
    std::array<SliderHit, 8> steppers_ {};
    std::array<SegmentHit, 24> segments_ {};
    std::size_t sliderCount_ = 0, actionCount_ = 0, stepperCount_ = 0, segmentCount_ = 0;
    int activeSlider_ = -1;
    bool darkTheme_ = true;
    Rect themeRect_ {};
};

END_NAMESPACE_DISTRHO
