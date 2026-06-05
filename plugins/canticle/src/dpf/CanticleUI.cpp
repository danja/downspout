#include "DistrhoUI.hpp"

#include "canticle_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::canticle::ParamId;
using downspout::canticle::kModelNames;
using downspout::canticle::kParameterCount;
using downspout::canticle::kParameterSpecs;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct Color {
    int r;
    int g;
    int b;
};

struct ControlDef {
    std::uint32_t parameter;
    const char* label;
};

struct SectionDef {
    const char* title;
    Color color;
    std::array<ControlDef, 4> controls;
    std::size_t count;
};

constexpr std::array<SectionDef, 4> kSections = {{
    {"Voice", {93, 158, 180}, {{{0, "Model"}, {1, "Tone"}, {2, "Body"}, {3, "Move"}}}, 4},
    {"Envelope", {199, 145, 86}, {{{4, "Attack"}, {5, "Decay"}, {6, "Sustain"}, {7, "Release"}}}, 4},
    {"Space", {132, 168, 104}, {{{8, "Detune"}, {9, "Width"}, {10, "Drive"}, {11, "Output"}}}, 4},
    {"Role", {178, 128, 188}, {{{0, "Keys"}, {3, "Pluck"}, {2, "Pad"}, {1, "Reed"}}}, 4},
}};

float clampf(const float value, const float minimum, const float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

float normalizedValue(const std::uint32_t parameter, const float value)
{
    const auto& spec = kParameterSpecs[parameter];
    return spec.maximum <= spec.minimum ? 0.0f : clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[48];
    if (parameter == static_cast<std::uint32_t>(ParamId::model))
    {
        const std::size_t index = std::min<std::size_t>(kModelNames.size() - 1,
                                                        static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(value)))));
        std::snprintf(buffer, sizeof(buffer), "%s", kModelNames[index]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::output))
    {
        std::snprintf(buffer, sizeof(buffer), "%.1fx", clampf(value, 0.0f, 1.0f) * 1.65f);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    }
    return buffer;
}

} // namespace

class CanticleUI : public UI {
public:
    CanticleUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < values_.size())
        {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 24.0f;
        controlRectCount_ = 0;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 72.0f);

        const float top = 122.0f;
        const float gap = 16.0f;
        const float sectionW = (width - pad * 2.0f - gap * 3.0f) / 4.0f;
        const float sectionH = height - top - pad;
        for (std::size_t i = 0; i < kSections.size(); ++i)
            drawSection(i, {pad + static_cast<float>(i) * (sectionW + gap), top, sectionW, sectionH});
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press)
        {
            activeParameter_ = -1;
            return false;
        }

        for (const auto& rect : controlRects_)
        {
            if (rect.parameter >= 0 && rect.bounds.contains(ev.pos.getX(), ev.pos.getY()))
            {
                if (rect.modelValue >= 0)
                {
                    activeParameter_ = -1;
                    commitParameter(static_cast<std::uint32_t>(ParamId::model), static_cast<float>(rect.modelValue));
                    return true;
                }
                activeParameter_ = rect.parameter;
                updateParameterFromMouse(rect.bounds, ev.pos.getX());
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeParameter_ < 0)
            return false;

        for (const auto& rect : controlRects_)
        {
            if (rect.parameter == activeParameter_)
            {
                updateParameterFromMouse(rect.bounds, ev.pos.getX());
                return true;
            }
        }
        return false;
    }

private:
    struct ControlRect {
        int parameter = -1;
        int modelValue = -1;
        Rect bounds {};
    };

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(27, 31, 34, 255);
        rect(0.0f, 0.0f, width, height);
        fill();

        beginPath();
        fillColor(38, 44, 47, 255);
        rect(0.0f, 0.0f, width, 104.0f);
        fill();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        fillColor(225, 230, 222, 255);
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y + 3.0f, "Canticle", nullptr);

        fillColor(165, 176, 170, 255);
        fontSize(15.0f);
        text(x + 160.0f, y + 13.0f, "polyphonic keys, reed, pad, pluck, and glass", nullptr);

        fillColor(116, 128, 124, 255);
        fontSize(12.0f);
        text(x, y + h - 18.0f, "clear middle voice for Cadence chords, MelGen lines, and Counterpointer answers", nullptr);
    }

    void drawSection(const std::size_t index, const Rect rect)
    {
        const auto& section = kSections[index];
        beginPath();
        fillColor(35, 40, 42, 255);
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fill();

        beginPath();
        fillColor(section.color.r, section.color.g, section.color.b, 255);
        roundedRect(rect.x, rect.y, rect.w, 42.0f, 7.0f);
        fill();

        fillColor(20, 24, 25, 255);
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(rect.x + 14.0f, rect.y + 21.0f, section.title, nullptr);

        const float slotH = (rect.h - 64.0f) / static_cast<float>(section.count);
        for (std::size_t i = 0; i < section.count; ++i)
        {
            const Rect control {rect.x + 14.0f, rect.y + 54.0f + static_cast<float>(i) * slotH, rect.w - 28.0f, slotH - 12.0f};
            const std::uint32_t parameter = section.controls[i].parameter;
            if (index == 3)
                drawModelButton(section.controls[i], control);
            else
                drawSlider(parameter, section.controls[i].label, control, section.color);
        }
    }

    void drawModelButton(const ControlDef control, const Rect rect)
    {
        const int model = static_cast<int>(std::lround(values_[static_cast<std::size_t>(ParamId::model)]));
        const bool selected = model == static_cast<int>(control.parameter);
        beginPath();
        fillColor(selected ? 90 : 48, selected ? 69 : 54, selected ? 96 : 58, 255);
        roundedRect(rect.x, rect.y, rect.w, rect.h, 5.0f);
        fill();

        fillColor(selected ? 238 : 172, selected ? 230 : 174, selected ? 241 : 178, 255);
        fontSize(15.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, control.label, nullptr);

        rememberControl(static_cast<int>(ParamId::model), rect, static_cast<int>(control.parameter));
    }

    void drawSlider(const std::uint32_t parameter, const char* label, const Rect rect, const Color color)
    {
        const float normalized = normalizedValue(parameter, values_[parameter]);
        fillColor(190, 198, 193, 255);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(rect.x, rect.y, label, nullptr);

        const std::string value = formatValue(parameter, values_[parameter]);
        fillColor(128, 138, 134, 255);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(rect.x + rect.w, rect.y, value.c_str(), nullptr);

        const float trackY = rect.y + 27.0f;
        beginPath();
        fillColor(24, 28, 30, 255);
        roundedRect(rect.x, trackY, rect.w, 13.0f, 4.0f);
        fill();

        beginPath();
        fillColor(color.r, color.g, color.b, 255);
        roundedRect(rect.x, trackY, rect.w * normalized, 13.0f, 4.0f);
        fill();

        rememberControl(static_cast<int>(parameter), {rect.x, trackY - 8.0f, rect.w, 29.0f});
    }

    void rememberControl(const int parameter, const Rect bounds, const int modelValue = -1)
    {
        if (controlRectCount_ < controlRects_.size())
            controlRects_[controlRectCount_++] = {parameter, modelValue, bounds};
    }

    void updateParameterFromMouse(const Rect bounds, const float mouseX)
    {
        if (activeParameter_ < 0)
            return;

        const auto& spec = kParameterSpecs[static_cast<std::size_t>(activeParameter_)];
        float normalized = clampf((mouseX - bounds.x) / std::max(1.0f, bounds.w), 0.0f, 1.0f);
        float value = spec.minimum + normalized * (spec.maximum - spec.minimum);
        if (spec.integer)
            value = std::round(value);

        commitParameter(static_cast<std::uint32_t>(activeParameter_), value);
    }

    void commitParameter(const std::uint32_t parameter, const float value)
    {
        values_[parameter] = value;
        setParameterValue(parameter, value);
        repaint();
    }

    std::array<float, kParameterCount> values_ {};
    std::array<ControlRect, 32> controlRects_ {};
    std::size_t controlRectCount_ = 0;
    int activeParameter_ = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CanticleUI)
};

UI* createUI()
{
    return new CanticleUI();
}

END_NAMESPACE_DISTRHO
