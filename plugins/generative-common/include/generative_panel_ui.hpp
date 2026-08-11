#pragma once

#include "DistrhoUI.hpp"
#include "generative_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

START_NAMESPACE_DISTRHO

class GenerativePanelUI : public UI {
public:
    GenerativePanelUI(const char* title,
                      const char* subtitle,
                      const downspout::generative::ParamSpec* specs,
                      const std::uint32_t count,
                      const int accentR,
                      const int accentG,
                      const int accentB)
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          title_(title),
          subtitle_(subtitle),
          specs_(specs),
          count_(std::min<std::uint32_t>(count, values_.size())),
          accentR_(accentR),
          accentG_(accentG),
          accentB_(accentB)
    {
        for (std::uint32_t i = 0; i < count_; ++i)
            values_[i] = specs_[i].defaultValue;
#ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
#else
        loadSharedResources();
#endif
    }

protected:
    enum class HitKind : std::uint8_t {
        Slider,
        Choice,
        Toggle,
        Action
    };

    struct Box {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        bool contains(const float px, const float py) const noexcept
        {
            return px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index < count_) {
            values_[index] = value;
            repaint();
        }
    }

    float value(const std::uint32_t index) const noexcept
    {
        return index < count_ ? values_[index] : 0.0f;
    }

    const downspout::generative::ParamSpec& spec(const std::uint32_t index) const noexcept
    {
        return specs_[index];
    }

    void beginPanel()
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        hitCount_ = 0;

        beginPath();
        fillColor(17, 21, 26, 255);
        rect(0.0f, 0.0f, width, height);
        fill();

        beginPath();
        fillColor(28, 34, 41, 255);
        rect(0.0f, 0.0f, width, 88.0f);
        fill();

        fillColor(238, 242, 239, 255);
        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(24.0f, 18.0f, title_, nullptr);

        fillColor(accentR_, accentG_, accentB_, 255);
        fontSize(12.5f);
        text(25.0f, 55.0f, subtitle_, nullptr);
    }

    void endPanel()
    {
        const float height = static_cast<float>(getHeight());
        beginPath();
        fillColor(22, 27, 33, 255);
        rect(0.0f, height - 25.0f, static_cast<float>(getWidth()), 25.0f);
        fill();

        fillColor(144, 154, 163, 255);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        const char* message = "Drag to adjust · right-click restores the default";
        if (hovered_ >= 0 && static_cast<std::uint32_t>(hovered_) < hitCount_
            && hits_[static_cast<std::uint32_t>(hovered_)].help != nullptr)
            message = hits_[static_cast<std::uint32_t>(hovered_)].help;
        text(24.0f, height - 12.5f, message, nullptr);
    }

    void drawSection(const float x,
                     const float y,
                     const float w,
                     const float h,
                     const char* title,
                     const char* hint = nullptr)
    {
        beginPath();
        fillColor(30, 36, 43, 255);
        roundedRect(x, y, w, h, 7.0f);
        fill();

        fillColor(224, 230, 226, 255);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 12.0f, y + 10.0f, title, nullptr);
        if (hint != nullptr) {
            fillColor(118, 131, 141, 255);
            fontSize(10.5f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            text(x + w - 12.0f, y + 11.0f, hint, nullptr);
        }
    }

    void drawSlider(const std::uint32_t index,
                    const float x,
                    const float y,
                    const float w,
                    const char* label,
                    const char* unit,
                    const char* help,
                    const int decimals = 2,
                    const bool enabled = true)
    {
        const auto& s = specs_[index];
        const float normalized = normalizedValue(index);
        char formatted[48] {};
        if (s.integer)
            std::snprintf(formatted, sizeof(formatted), "%d%s",
                          static_cast<int>(std::lround(values_[index])), unit != nullptr ? unit : "");
        else
            std::snprintf(formatted, sizeof(formatted), "%.*f%s",
                          decimals, values_[index], unit != nullptr ? unit : "");

        drawControlShell(x, y, w, 48.0f, enabled);
        drawLabelAndValue(x, y, w, label, formatted, enabled);
        const float tx = x + 10.0f;
        const float ty = y + 35.0f;
        const float tw = w - 20.0f;
        beginPath();
        fillColor(13, 17, 21, 255);
        roundedRect(tx, ty, tw, 5.0f, 2.0f);
        fill();
        beginPath();
        fillColor(accentR_, accentG_, accentB_, enabled ? 245 : 65);
        roundedRect(tx, ty, std::max(2.0f, tw * normalized), 5.0f, 2.0f);
        fill();
        if (enabled)
            addHit({tx, y, tw, 48.0f}, index, HitKind::Slider, 0, help);
    }

    void drawNoteSlider(const std::uint32_t index,
                        const float x,
                        const float y,
                        const float w,
                        const char* label,
                        const char* help,
                        const bool enabled = true)
    {
        char note[24] {};
        formatMidiNote(static_cast<int>(std::lround(values_[index])), note, sizeof(note));
        drawFormattedSlider(index, x, y, w, label, note, help, enabled);
    }

    void drawPercentSlider(const std::uint32_t index,
                           const float x,
                           const float y,
                           const float w,
                           const char* label,
                           const char* help,
                           const bool enabled = true)
    {
        char valueText[24] {};
        std::snprintf(valueText, sizeof(valueText), "%d%%",
                      static_cast<int>(std::lround(values_[index] * 100.0f)));
        drawFormattedSlider(index, x, y, w, label, valueText, help, enabled);
    }

    void drawBeatSlider(const std::uint32_t index,
                        const float x,
                        const float y,
                        const float w,
                        const char* label,
                        const char* help,
                        const bool enabled = true)
    {
        char valueText[32] {};
        const float beats = values_[index];
        if (std::abs(beats - 1.0f) < 0.001f)
            std::snprintf(valueText, sizeof(valueText), "1 beat");
        else
            std::snprintf(valueText, sizeof(valueText), "%.3g beats", beats);
        drawFormattedSlider(index, x, y, w, label, valueText, help, enabled);
    }

    void drawPitchClassSlider(const std::uint32_t index,
                              const float x,
                              const float y,
                              const float w,
                              const char* label,
                              const char* help,
                              const bool enabled = true)
    {
        drawFormattedSlider(index, x, y, w, label,
                            pitchClassName(static_cast<int>(std::lround(values_[index]))),
                            help, enabled);
    }

    void drawCcSlider(const std::uint32_t index,
                      const float x,
                      const float y,
                      const float w,
                      const char* label,
                      const char* help,
                      const bool enabled = true)
    {
        char valueText[40] {};
        const int cc = static_cast<int>(std::lround(values_[index]));
        const char* common = ccName(cc);
        if (common != nullptr)
            std::snprintf(valueText, sizeof(valueText), "CC %d · %s", cc, common);
        else
            std::snprintf(valueText, sizeof(valueText), "CC %d", cc);
        drawFormattedSlider(index, x, y, w, label, valueText, help, enabled);
    }

    void drawChannelSlider(const std::uint32_t index,
                           const float x,
                           const float y,
                           const float w,
                           const char* label,
                           const char* help,
                           const bool enabled = true)
    {
        char valueText[20] {};
        std::snprintf(valueText, sizeof(valueText), "Ch %d",
                      static_cast<int>(std::lround(values_[index])));
        drawFormattedSlider(index, x, y, w, label, valueText, help, enabled);
    }

    void drawChoice(const std::uint32_t index,
                    const float x,
                    const float y,
                    const float w,
                    const char* label,
                    const char* const* choices,
                    const std::uint32_t choiceCount,
                    const char* help,
                    const bool enabled = true)
    {
        drawControlShell(x, y, w, 51.0f, enabled);
        fillColor(enabled ? 202 : 99, enabled ? 211 : 108, enabled ? 207 : 113, 255);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10.0f, y + 7.0f, label, nullptr);

        const std::uint32_t selected = choiceCount > 0
            ? std::min<std::uint32_t>(choiceCount - 1u,
                static_cast<std::uint32_t>(std::max(0, static_cast<int>(std::lround(values_[index])))))
            : 0u;
        const float sx = x + 9.0f;
        const float sy = y + 24.0f;
        const float sw = w - 18.0f;
        const float segment = sw / std::max(1u, choiceCount);
        for (std::uint32_t i = 0; i < choiceCount; ++i) {
            beginPath();
            fillColor(i == selected ? accentR_ : 20,
                      i == selected ? accentG_ : 25,
                      i == selected ? accentB_ : 30,
                      enabled ? 255 : 90);
            roundedRect(sx + i * segment + 1.0f, sy, segment - 2.0f, 20.0f, 3.0f);
            fill();
            fillColor(i == selected ? 12 : 168, i == selected ? 17 : 177,
                      i == selected ? 20 : 182, enabled ? 255 : 90);
            fontSize(choiceCount > 4 ? 9.0f : 10.5f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(sx + (i + 0.5f) * segment, sy + 10.0f, choices[i], nullptr);
        }
        if (enabled)
            addHit({sx, sy, sw, 20.0f}, index, HitKind::Choice,
                   static_cast<int>(choiceCount), help);
    }

    void drawToggle(const std::uint32_t index,
                    const float x,
                    const float y,
                    const float w,
                    const char* label,
                    const char* help,
                    const bool enabled = true)
    {
        drawControlShell(x, y, w, 42.0f, enabled);
        fillColor(enabled ? 205 : 100, enabled ? 214 : 109, enabled ? 210 : 114, 255);
        fontSize(11.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 11.0f, y + 21.0f, label, nullptr);
        const bool on = values_[index] >= 0.5f;
        const float sx = x + w - 49.0f;
        const float sy = y + 11.0f;
        beginPath();
        fillColor(on ? accentR_ : 14, on ? accentG_ : 18, on ? accentB_ : 23,
                  enabled ? 255 : 80);
        roundedRect(sx, sy, 38.0f, 20.0f, 10.0f);
        fill();
        beginPath();
        fillColor(236, 240, 237, enabled ? 255 : 100);
        circle(sx + (on ? 28.0f : 10.0f), sy + 10.0f, 7.0f);
        fill();
        if (enabled)
            addHit({x, y, w, 42.0f}, index, HitKind::Toggle, 0, help);
    }

    void drawAction(const int action,
                    const float x,
                    const float y,
                    const float w,
                    const float h,
                    const char* label,
                    const char* help,
                    const bool destructive = false)
    {
        beginPath();
        fillColor(destructive ? 83 : accentR_,
                  destructive ? 43 : accentG_,
                  destructive ? 40 : accentB_, 225);
        roundedRect(x, y, w, h, 5.0f);
        fill();
        fillColor(245, 247, 245, 255);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x + w * 0.5f, y + h * 0.5f, label, nullptr);
        addHit({x, y, w, h}, static_cast<std::uint32_t>(std::max(0, action)),
               HitKind::Action, action, help);
    }

    void drawMeter(const std::uint32_t index,
                   const float x,
                   const float y,
                   const float w,
                   const char* label,
                   const char* unit = nullptr,
                   const int decimals = 2)
    {
        char formatted[48] {};
        if (specs_[index].integer)
            std::snprintf(formatted, sizeof(formatted), "%d%s",
                          static_cast<int>(std::lround(values_[index])), unit != nullptr ? unit : "");
        else
            std::snprintf(formatted, sizeof(formatted), "%.*f%s",
                          decimals, values_[index], unit != nullptr ? unit : "");
        drawControlShell(x, y, w, 42.0f, true, true);
        drawLabelAndValue(x, y, w, label, formatted, true, true);
        const float normalized = normalizedValue(index);
        beginPath();
        fillColor(13, 17, 21, 255);
        roundedRect(x + 10.0f, y + 30.0f, w - 20.0f, 4.0f, 2.0f);
        fill();
        beginPath();
        fillColor(accentR_, accentG_, accentB_, 165);
        roundedRect(x + 10.0f, y + 30.0f, std::max(2.0f, (w - 20.0f) * normalized), 4.0f, 2.0f);
        fill();
    }

    void drawReadout(const float x,
                     const float y,
                     const float w,
                     const float h,
                     const char* label,
                     const char* display)
    {
        drawControlShell(x, y, w, h, true, true);
        fillColor(126, 139, 148, 255);
        fontSize(10.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10.0f, y + 7.0f, label, nullptr);
        fillColor(235, 239, 236, 255);
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(x + 10.0f, y + h - 8.0f, display, nullptr);
    }

    void drawLamp(const float x,
                  const float y,
                  const float w,
                  const char* label,
                  const bool on,
                  const bool warning = false)
    {
        drawControlShell(x, y, w, 37.0f, true, true);
        beginPath();
        fillColor(on ? (warning ? 232 : accentR_) : 50,
                  on ? (warning ? 106 : accentG_) : 57,
                  on ? (warning ? 82 : accentB_) : 63, 255);
        circle(x + 16.0f, y + 18.5f, 6.0f);
        fill();
        fillColor(on ? 225 : 130, on ? 231 : 140, on ? 227 : 148, 255);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 29.0f, y + 18.5f, label, nullptr);
    }

    void drawBar(const float x,
                 const float y,
                 const float w,
                 const float normalized,
                 const bool muted = false)
    {
        beginPath();
        fillColor(13, 17, 21, 255);
        roundedRect(x, y, w, 6.0f, 3.0f);
        fill();
        beginPath();
        fillColor(accentR_, accentG_, accentB_, muted ? 80 : 235);
        roundedRect(x, y, std::max(2.0f, w * std::clamp(normalized, 0.0f, 1.0f)), 6.0f, 3.0f);
        fill();
    }

    virtual void actionTriggered(const int) {}

    bool handleMouse(const MouseEvent& event)
    {
        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        if (!event.press) {
            active_ = -1;
            return true;
        }
        for (std::uint32_t i = 0; i < hitCount_; ++i) {
            if (!hits_[i].box.contains(x, y))
                continue;
            hovered_ = static_cast<int>(i);
            const Hit hit = hits_[i];
            if (event.button == 2 && hit.kind != HitKind::Action) {
                commit(hit.parameter, specs_[hit.parameter].defaultValue);
                return true;
            }
            if (event.button != 1)
                return false;
            switch (hit.kind) {
            case HitKind::Slider:
                active_ = static_cast<int>(i);
                updateSlider(hit, x);
                break;
            case HitKind::Choice: {
                const float normalized = std::clamp((x - hit.box.x) / std::max(1.0f, hit.box.w), 0.0f, 0.9999f);
                commit(hit.parameter, static_cast<float>(static_cast<int>(normalized * std::max(1, hit.detail))));
                break;
            }
            case HitKind::Toggle:
                commit(hit.parameter, values_[hit.parameter] >= 0.5f ? 0.0f : 1.0f);
                break;
            case HitKind::Action:
                actionTriggered(hit.detail);
                break;
            }
            return true;
        }
        return false;
    }

    static const char* pitchClassName(const int pitch) noexcept
    {
        static constexpr const char* names[] {
            "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"
        };
        return names[(pitch % 12 + 12) % 12];
    }

    static void formatMidiNote(const int note, char* target, const std::size_t size) noexcept
    {
        std::snprintf(target, size, "%s%d", pitchClassName(note), note / 12 - 1);
    }

    int accentR() const noexcept { return accentR_; }
    int accentG() const noexcept { return accentG_; }
    int accentB() const noexcept { return accentB_; }
    void commitParameter(const std::uint32_t index, const float next) { commit(index, next); }

private:
    struct Hit {
        Box box {};
        std::uint32_t parameter = 0;
        HitKind kind = HitKind::Slider;
        int detail = 0;
        const char* help = nullptr;
    };

    void onNanoDisplay() override
    {
        beginPanel();
        endPanel();
    }

    bool onMouse(const MouseEvent& event) override
    {
        return handleMouse(event);
    }

    bool onMotion(const MotionEvent& event) override
    {
        const float x = static_cast<float>(event.pos.getX());
        const float y = static_cast<float>(event.pos.getY());
        if (active_ >= 0 && static_cast<std::uint32_t>(active_) < hitCount_) {
            updateSlider(hits_[static_cast<std::uint32_t>(active_)], x);
            return true;
        }
        int nextHover = -1;
        for (std::uint32_t i = 0; i < hitCount_; ++i) {
            if (hits_[i].box.contains(x, y)) {
                nextHover = static_cast<int>(i);
                break;
            }
        }
        if (nextHover != hovered_) {
            hovered_ = nextHover;
            repaint();
        }
        return nextHover >= 0;
    }

    void drawFormattedSlider(const std::uint32_t index,
                             const float x,
                             const float y,
                             const float w,
                             const char* label,
                             const char* formatted,
                             const char* help,
                             const bool enabled)
    {
        drawControlShell(x, y, w, 48.0f, enabled);
        drawLabelAndValue(x, y, w, label, formatted, enabled);
        const float tx = x + 10.0f;
        const float ty = y + 35.0f;
        const float tw = w - 20.0f;
        beginPath();
        fillColor(13, 17, 21, 255);
        roundedRect(tx, ty, tw, 5.0f, 2.0f);
        fill();
        beginPath();
        fillColor(accentR_, accentG_, accentB_, enabled ? 245 : 65);
        roundedRect(tx, ty, std::max(2.0f, tw * normalizedValue(index)), 5.0f, 2.0f);
        fill();
        if (enabled)
            addHit({tx, y, tw, 48.0f}, index, HitKind::Slider, 0, help);
    }

    void drawControlShell(const float x,
                          const float y,
                          const float w,
                          const float h,
                          const bool enabled,
                          const bool output = false)
    {
        beginPath();
        if (output)
            fillColor(25, 31, 37, 255);
        else if (enabled)
            fillColor(38, 45, 52, 255);
        else
            fillColor(29, 34, 40, 255);
        roundedRect(x, y, w, h, 5.0f);
        fill();
    }

    void drawLabelAndValue(const float x,
                           const float y,
                           const float w,
                           const char* label,
                           const char* formatted,
                           const bool enabled,
                           const bool output = false)
    {
        fillColor(enabled ? (output ? 132 : 205) : 98,
                  enabled ? (output ? 145 : 214) : 107,
                  enabled ? (output ? 153 : 210) : 113, 255);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10.0f, y + 7.0f, label, nullptr);
        fillColor(enabled ? 220 : 105, enabled ? 226 : 114, enabled ? 222 : 120, 255);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(x + w - 10.0f, y + 7.0f, formatted, nullptr);
    }

    void addHit(const Box box,
                const std::uint32_t parameter,
                const HitKind kind,
                const int detail,
                const char* help)
    {
        if (hitCount_ < hits_.size())
            hits_[hitCount_++] = {box, parameter, kind, detail, help};
    }

    void commit(const std::uint32_t index, float next)
    {
        if (index >= count_ || specs_[index].output)
            return;
        next = downspout::generative::clampParam(next, specs_[index]);
        values_[index] = next;
        setParameterValue(index, next);
        repaint();
    }

    void updateSlider(const Hit& hit, const float mouseX)
    {
        const auto& s = specs_[hit.parameter];
        const float normalized = std::clamp(
            (mouseX - hit.box.x) / std::max(1.0f, hit.box.w), 0.0f, 1.0f);
        float next = s.minimum + normalized * (s.maximum - s.minimum);
        if (s.integer)
            next = std::round(next);
        commit(hit.parameter, next);
    }

    float normalizedValue(const std::uint32_t index) const noexcept
    {
        const auto& s = specs_[index];
        return s.maximum > s.minimum
            ? std::clamp((values_[index] - s.minimum) / (s.maximum - s.minimum), 0.0f, 1.0f)
            : 0.0f;
    }

    static const char* ccName(const int cc) noexcept
    {
        switch (cc) {
        case 1: return "Mod";
        case 2: return "Breath";
        case 7: return "Volume";
        case 10: return "Pan";
        case 11: return "Expression";
        case 64: return "Sustain";
        case 71: return "Resonance";
        case 74: return "Brightness";
        default: return nullptr;
        }
    }

    const char* title_;
    const char* subtitle_;
    const downspout::generative::ParamSpec* specs_;
    std::uint32_t count_;
    int accentR_;
    int accentG_;
    int accentB_;
    std::array<float, 64> values_ {};
    std::array<Hit, 96> hits_ {};
    std::uint32_t hitCount_ = 0;
    int active_ = -1;
    int hovered_ = -1;
};

END_NAMESPACE_DISTRHO
