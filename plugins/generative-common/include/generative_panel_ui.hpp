#pragma once

#include "DistrhoUI.hpp"
#include "generative_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

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
    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index < count_) {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        beginPath();
        fillColor(19, 23, 28, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        beginPath();
        fillColor(29, 35, 42, 255);
        rect(0.0f, 0.0f, width, 92.0f);
        fill();

        fillColor(235, 239, 236, 255);
        fontSize(29.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(25.0f, 20.0f, title_, nullptr);
        fillColor(accentR_, accentG_, accentB_, 255);
        fontSize(13.0f);
        text(26.0f, 58.0f, subtitle_, nullptr);

        rectCount_ = 0;
        const std::uint32_t columns = count_ > 16 ? 3u : 2u;
        const std::uint32_t rows = (count_ + columns - 1u) / columns;
        const float gap = 14.0f;
        const float pad = 24.0f;
        const float top = 112.0f;
        const float columnWidth = (width - pad * 2.0f - gap * (columns - 1u)) / columns;
        const float rowHeight = std::max(38.0f, (height - top - pad) / std::max(1u, rows));
        for (std::uint32_t i = 0; i < count_; ++i) {
            const std::uint32_t column = i / rows;
            const std::uint32_t row = i % rows;
            drawControl(i,
                        pad + column * (columnWidth + gap),
                        top + row * rowHeight,
                        columnWidth,
                        rowHeight - 7.0f);
        }
    }

    bool onMouse(const MouseEvent& event) override
    {
        if (event.button != 1)
            return false;
        if (!event.press) {
            active_ = -1;
            return true;
        }
        for (std::uint32_t i = 0; i < rectCount_; ++i) {
            if (rects_[i].contains(event.pos.getX(), event.pos.getY())) {
                active_ = static_cast<int>(rects_[i].parameter);
                update(event.pos.getX());
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& event) override
    {
        if (active_ < 0)
            return false;
        update(event.pos.getX());
        return true;
    }

private:
    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;
        std::uint32_t parameter = 0;
        bool contains(const float px, const float py) const noexcept
        {
            return px >= x && px <= x + w && py >= y && py <= y + h;
        }
    };

    void drawControl(const std::uint32_t index,
                     const float x,
                     const float y,
                     const float width,
                     const float height)
    {
        const auto& spec = specs_[index];
        const float normalized = spec.maximum > spec.minimum
            ? std::clamp((values_[index] - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f)
            : 0.0f;
        char value[32] {};
        if (spec.integer)
            std::snprintf(value, sizeof(value), "%d", static_cast<int>(std::lround(values_[index])));
        else
            std::snprintf(value, sizeof(value), "%.2f", values_[index]);

        beginPath();
        fillColor(32, 38, 44, 255);
        roundedRect(x, y, width, height, 5.0f);
        fill();
        fillColor(spec.output ? 126 : 205, spec.output ? 137 : 214, spec.output ? 143 : 210, 255);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 9.0f, y + 7.0f, spec.name, nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(x + width - 9.0f, y + 7.0f, value, nullptr);

        const float trackY = y + height - 10.0f;
        beginPath();
        fillColor(15, 18, 22, 255);
        roundedRect(x + 9.0f, trackY, width - 18.0f, 5.0f, 2.0f);
        fill();
        beginPath();
        fillColor(accentR_, accentG_, accentB_, spec.output ? 110 : 245);
        roundedRect(x + 9.0f, trackY, (width - 18.0f) * normalized, 5.0f, 2.0f);
        fill();
        if (!spec.output && rectCount_ < rects_.size())
            rects_[rectCount_++] = {x + 9.0f, y, width - 18.0f, height, index};
    }

    void update(const float mouseX)
    {
        if (active_ < 0)
            return;
        for (std::uint32_t i = 0; i < rectCount_; ++i) {
            if (rects_[i].parameter != static_cast<std::uint32_t>(active_))
                continue;
            const auto& spec = specs_[static_cast<std::uint32_t>(active_)];
            const float normalized = std::clamp(
                (mouseX - rects_[i].x) / std::max(1.0f, rects_[i].w), 0.0f, 1.0f);
            float value = spec.minimum + normalized * (spec.maximum - spec.minimum);
            if (spec.integer)
                value = std::round(value);
            values_[static_cast<std::uint32_t>(active_)] = value;
            setParameterValue(static_cast<std::uint32_t>(active_), value);
            repaint();
            return;
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
    std::array<Rect, 64> rects_ {};
    std::uint32_t rectCount_ = 0;
    int active_ = -1;
};

END_NAMESPACE_DISTRHO
