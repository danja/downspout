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
using downspout::canticle::kArticulationNames;
using downspout::canticle::kEnsembleNames;
using downspout::canticle::kModelNames;
using downspout::canticle::kParameterCount;
using downspout::canticle::kParameterSpecs;
using downspout::canticle::kRangeNames;

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

struct DropdownDef {
    std::uint32_t parameter;
    const char* label;
    const char* const* names;
    std::size_t count;
};

constexpr std::array<SectionDef, 3> kSections = {{
    {"Voice", {93, 158, 180}, {{{1, "Tone"}, {2, "Body"}, {12, "Metal"}, {3, "Move"}}}, 4},
    {"Envelope", {199, 145, 86}, {{{4, "Attack"}, {5, "Decay"}, {6, "Sustain"}, {7, "Release"}}}, 4},
    {"Space", {132, 168, 104}, {{{8, "Detune"}, {9, "Width"}, {10, "Drive"}, {11, "Output"}}}, 4},
}};

constexpr std::array<DropdownDef, 4> kDropdowns = {{
    {static_cast<std::uint32_t>(ParamId::model), "Model", kModelNames.data(), kModelNames.size()},
    {static_cast<std::uint32_t>(ParamId::articulation), "Articulation", kArticulationNames.data(), kArticulationNames.size()},
    {static_cast<std::uint32_t>(ParamId::range), "Register", kRangeNames.data(), kRangeNames.size()},
    {static_cast<std::uint32_t>(ParamId::ensemble), "Ensemble", kEnsembleNames.data(), kEnsembleNames.size()},
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
    else if (parameter == static_cast<std::uint32_t>(ParamId::articulation))
    {
        const std::size_t index = std::min<std::size_t>(kArticulationNames.size() - 1,
                                                        static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(value)))));
        std::snprintf(buffer, sizeof(buffer), "%s", kArticulationNames[index]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::range))
    {
        const std::size_t index = std::min<std::size_t>(kRangeNames.size() - 1,
                                                        static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(value)))));
        std::snprintf(buffer, sizeof(buffer), "%s", kRangeNames[index]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::ensemble))
    {
        const std::size_t index = std::min<std::size_t>(kEnsembleNames.size() - 1,
                                                        static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(value)))));
        std::snprintf(buffer, sizeof(buffer), "%s", kEnsembleNames[index]);
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
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
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
        drawDropdownColumn({pad + 3.0f * (sectionW + gap), top, sectionW, sectionH});
        drawOpenDropdown();
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

        if (openDropdown_ >= 0)
        {
            if (handleOpenDropdownClick(ev.pos.getX(), ev.pos.getY()))
                return true;
            openDropdown_ = -1;
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i)
        {
            if (selectorRects_[i].contains(ev.pos.getX(), ev.pos.getY()))
            {
                activeParameter_ = -1;
                openDropdown_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }

        for (const auto& rect : controlRects_)
        {
            if (rect.parameter >= 0 && rect.bounds.contains(ev.pos.getX(), ev.pos.getY()))
            {
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
            drawSlider(parameter, section.controls[i].label, control, section.color);
        }
    }

    void drawDropdownColumn(const Rect rect)
    {
        beginPath();
        fillColor(35, 40, 42, 255);
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fill();

        beginPath();
        fillColor(178, 128, 188, 255);
        roundedRect(rect.x, rect.y, rect.w, 42.0f, 7.0f);
        fill();

        fillColor(20, 24, 25, 255);
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(rect.x + 14.0f, rect.y + 21.0f, "Role", nullptr);

        const float slotH = (rect.h - 64.0f) / static_cast<float>(kDropdowns.size());
        for (std::size_t i = 0; i < kDropdowns.size(); ++i)
        {
            const Rect control {rect.x + 14.0f, rect.y + 54.0f + static_cast<float>(i) * slotH, rect.w - 28.0f, slotH - 12.0f};
            selectorRects_[i] = control;
            drawDropdown(kDropdowns[i], control, openDropdown_ == static_cast<int>(i));
        }
    }

    void drawDropdown(const DropdownDef& dropdown, const Rect rect, const bool open)
    {
        fillColor(190, 198, 193, 255);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(rect.x, rect.y, dropdown.label, nullptr);

        const Rect box {rect.x, rect.y + 24.0f, rect.w, 30.0f};
        beginPath();
        fillColor(open ? 58 : 24, open ? 45 : 28, open ? 62 : 30, 255);
        roundedRect(box.x, box.y, box.w, box.h, 5.0f);
        fill();

        const std::string value = formatValue(dropdown.parameter, values_[dropdown.parameter]);
        fillColor(224, 226, 222, 255);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 10.0f, box.y + box.h * 0.5f + 1.0f, value.c_str(), nullptr);

        fillColor(154, 164, 159, 255);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 10.0f, box.y + box.h * 0.5f, open ? "^" : "v", nullptr);
    }

    void drawOpenDropdown()
    {
        if (openDropdown_ < 0)
            return;

        const DropdownDef& dropdown = kDropdowns[static_cast<std::size_t>(openDropdown_)];
        const Rect base = selectorRects_[static_cast<std::size_t>(openDropdown_)];
        const Rect menu {base.x, base.y + 56.0f, base.w, 28.0f * static_cast<float>(dropdown.count)};
        const int selected = static_cast<int>(std::lround(values_[dropdown.parameter]));

        beginPath();
        fillColor(23, 27, 29, 250);
        roundedRect(menu.x, menu.y, menu.w, menu.h, 6.0f);
        fill();

        beginPath();
        strokeColor(178, 128, 188, 220);
        strokeWidth(1.0f);
        roundedRect(menu.x, menu.y, menu.w, menu.h, 6.0f);
        stroke();

        for (std::size_t i = 0; i < dropdown.count; ++i)
        {
            const float y = menu.y + static_cast<float>(i) * 28.0f;
            if (selected == static_cast<int>(i))
            {
                beginPath();
                fillColor(74, 57, 80, 255);
                roundedRect(menu.x + 4.0f, y + 3.0f, menu.w - 8.0f, 22.0f, 5.0f);
                fill();
            }

            fillColor(225, 228, 224, 255);
            fontSize(13.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(menu.x + 10.0f, y + 14.0f, dropdown.names[i], nullptr);
        }
    }

    bool handleOpenDropdownClick(const float x, const float y)
    {
        const DropdownDef& dropdown = kDropdowns[static_cast<std::size_t>(openDropdown_)];
        const Rect base = selectorRects_[static_cast<std::size_t>(openDropdown_)];
        if (base.contains(x, y))
            return false;

        const Rect menu {base.x, base.y + 56.0f, base.w, 28.0f * static_cast<float>(dropdown.count)};
        if (!menu.contains(x, y))
            return false;

        const int item = std::clamp(static_cast<int>((y - menu.y) / 28.0f), 0, static_cast<int>(dropdown.count) - 1);
        commitParameter(dropdown.parameter, static_cast<float>(item));
        openDropdown_ = -1;
        repaint();
        return true;
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

    void rememberControl(const int parameter, const Rect bounds)
    {
        if (controlRectCount_ < controlRects_.size())
            controlRects_[controlRectCount_++] = {parameter, bounds};
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
    std::array<Rect, kDropdowns.size()> selectorRects_ {};
    std::size_t controlRectCount_ = 0;
    int activeParameter_ = -1;
    int openDropdown_ = -1;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CanticleUI)
};

UI* createUI()
{
    return new CanticleUI();
}

END_NAMESPACE_DISTRHO
