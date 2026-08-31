#include "DistrhoUI.hpp"

#include "floozy_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::floozy::ParamId;
using downspout::floozy::kBodyTypeNames;
using downspout::floozy::kInterfaceTypeNames;
using downspout::floozy::kParameterCount;
using downspout::floozy::kParameterSpecs;
using downspout::floozy::kSourceAlgorithmParamNames;
using downspout::floozy::kSourceAlgorithmNames;

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
    const Color color;
    const std::array<ControlDef, 7> controls;
    std::size_t count;
};

constexpr std::array<SectionDef, 5> kSections = {{
    {"Source", {219, 132, 84}, {{{24, "External"}, {0, "Algorithm"}, {1, "Param 1"}, {2, "Param 2"}, {3, "Tone"}, {4, "Noise"}, {5, "DC"}}}, 7},
    {"Shape", {92, 178, 164}, {{{6, "Attack"}, {7, "Release"}, {8, "Interface"}, {9, "Intensity"}, {22, "Gain"}, {0, ""}, {0, ""}}}, 5},
    {"Body", {126, 153, 221}, {{{25, "Body type"}, {10, "Tuning"}, {11, "Ratio"}, {12, "D1 FB"}, {13, "D2 FB"}, {14, "Cross FB"}, {0, ""}}}, 6},
    {"Filter/Mod", {181, 137, 216}, {{{15, "Cutoff"}, {16, "Q"}, {17, "Shape"}, {18, "LFO"}, {19, "Mod Mix"}, {0, ""}, {0, ""}}}, 5},
    {"Space", {211, 165, 82}, {{{20, "Size"}, {21, "Level"}, {23, "Voices"}, {0, ""}, {0, ""}, {0, ""}, {0, ""}}}, 3},
}};

float clampf(const float value, const float minimum, const float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

float normalizedValue(const std::uint32_t parameter, const float value)
{
    const auto& spec = kParameterSpecs[parameter];
    if (spec.maximum <= spec.minimum)
        return 0.0f;
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[64];
    if (parameter == static_cast<std::uint32_t>(ParamId::sourceAlgorithm))
    {
        const int index = std::clamp(static_cast<int>(std::lround(value)), 0, 7);
        std::snprintf(buffer, sizeof(buffer), "%s", kSourceAlgorithmNames[static_cast<std::size_t>(index)]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::bodyType))
    {
        const int index = std::clamp(static_cast<int>(std::lround(value)), 0, 6);
        std::snprintf(buffer, sizeof(buffer), "%s", kBodyTypeNames[static_cast<std::size_t>(index)]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::interfaceType))
    {
        const int index = std::clamp(static_cast<int>(std::lround(value)), 0, 12);
        std::snprintf(buffer, sizeof(buffer), "%s", kInterfaceTypeNames[static_cast<std::size_t>(index)]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::tuning))
    {
        const int semitone = static_cast<int>(std::lround((clampf(value, 0.0f, 1.0f) - 0.5f) * 48.0f));
        std::snprintf(buffer, sizeof(buffer), "%+d st", semitone);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::numVoices))
    {
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    }
    return buffer;
}

bool isDropdownParameter(const std::uint32_t parameter)
{
    return parameter == static_cast<std::uint32_t>(ParamId::sourceAlgorithm) ||
           parameter == static_cast<std::uint32_t>(ParamId::interfaceType) ||
           parameter == static_cast<std::uint32_t>(ParamId::bodyType);
}

std::size_t dropdownItemCount(const std::uint32_t parameter)
{
    if (parameter == static_cast<std::uint32_t>(ParamId::sourceAlgorithm))
        return kSourceAlgorithmNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::interfaceType))
        return kInterfaceTypeNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::bodyType))
        return kBodyTypeNames.size();
    return 0;
}

const char* dropdownItemName(const std::uint32_t parameter, const std::size_t index)
{
    if (parameter == static_cast<std::uint32_t>(ParamId::sourceAlgorithm))
        return kSourceAlgorithmNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::interfaceType))
        return kInterfaceTypeNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::bodyType))
        return kBodyTypeNames[index];
    return "";
}

} // namespace

class FloozyUI : public UI
{
public:
    FloozyUI()
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
        const float pad = 22.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 78.0f);

        const float top = 126.0f;
        const float gap = 14.0f;
        const float sectionW = (width - pad * 2.0f - gap * 4.0f) / 5.0f;
        const float sectionH = height - top - pad;
        for (std::size_t i = 0; i < kSections.size(); ++i)
        {
            const Rect rect {pad + static_cast<float>(i) * (sectionW + gap), top, sectionW, sectionH};
            drawSection(i, rect);
        }

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

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (dropdownParameter_ >= 0)
        {
            const std::uint32_t parameter = static_cast<std::uint32_t>(dropdownParameter_);
            const std::size_t itemCount = dropdownItemCount(parameter);
            const Rect menu {
                dropdownRect_.x,
                dropdownRect_.y + dropdownRect_.h + 4.0f,
                dropdownRect_.w,
                26.0f * static_cast<float>(itemCount),
            };

            if (menu.contains(x, y))
            {
                const std::size_t item = std::min<std::size_t>(
                    itemCount - 1,
                    static_cast<std::size_t>((y - menu.y) / 26.0f));
                commitParameter(parameter, static_cast<float>(item));
                dropdownParameter_ = -1;
                return true;
            }

            if (!dropdownRect_.contains(x, y))
            {
                dropdownParameter_ = -1;
                repaint();
            }
        }

        for (std::size_t i = 0; i < controlRects_.size(); ++i)
        {
            if (controlRects_[i].contains(x, y))
            {
                if (isDropdownParameter(static_cast<std::uint32_t>(i)))
                {
                    dropdownParameter_ = dropdownParameter_ == static_cast<int>(i) ? -1 : static_cast<int>(i);
                    dropdownRect_ = controlRects_[i];
                    repaint();
                    return true;
                }

                dropdownParameter_ = -1;
                activeParameter_ = static_cast<int>(i);
                updateParameterFromPosition(static_cast<std::uint32_t>(i), x, controlRects_[i]);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeParameter_ < 0)
            return false;

        const std::uint32_t parameter = static_cast<std::uint32_t>(activeParameter_);
        if (isDropdownParameter(parameter))
            return false;
        updateParameterFromPosition(parameter, static_cast<float>(ev.pos.getX()), controlRects_[parameter]);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        const float direction = ev.delta.getY() > 0.0f ? 1.0f : -1.0f;
        for (std::size_t i = 0; i < controlRects_.size(); ++i)
        {
            if (controlRects_[i].contains(x, y))
            {
                const auto& spec = kParameterSpecs[i];
                nudgeParameter(static_cast<std::uint32_t>(i), direction * (spec.integer ? 1.0f : 0.02f));
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kParameterCount> controlRects_ {};
    int activeParameter_ = -1;
    int dropdownParameter_ = -1;
    Rect dropdownRect_ {};

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(12, 14, 15, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(29, 33, 33, 255);
        rect(0.0f, 0.0f, width, 116.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fillColor(18, 22, 22, 238);
        fill();
        strokeColor(52, 60, 60, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(32.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(238, 241, 237, 255);
        text(x + 22.0f, y + 14.0f, "Floozy", nullptr);

        const int numVoices = static_cast<int>(
            std::lround(values_[static_cast<std::size_t>(ParamId::numVoices)]));
        char subtitle[64];
        std::snprintf(subtitle, sizeof(subtitle),
                      "%d voice hybrid physical/modulation synth", numVoices);
        fontSize(13.0f);
        fillColor(155, 166, 164, 255);
        text(x + 170.0f, y + 27.0f, subtitle, nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(204, 214, 210, 255);
        text(x + w - 22.0f, y + 29.0f, "MIDI in | stereo I/O", nullptr);
    }

    void drawSection(const std::size_t index, const Rect& rect)
    {
        const SectionDef& section = kSections[index];
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(18, 22, 23, 238);
        fill();
        strokeColor(46, 54, 55, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        beginPath();
        roundedRect(rect.x + 12.0f, rect.y + 12.0f, rect.w - 24.0f, 7.0f, 3.5f);
        fillColor(section.color.r, section.color.g, section.color.b, 230);
        fill();
        closePath();

        fontSize(17.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(232, 237, 233, 255);
        text(rect.x + 14.0f, rect.y + 33.0f, section.title, nullptr);

        const float startY = rect.y + 78.0f;
        const float rowH = 46.0f;
        const float rowGap = 17.0f;
        for (std::size_t i = 0; i < section.count; ++i)
        {
            const Rect control {rect.x + 14.0f, startY + static_cast<float>(i) * (rowH + rowGap), rect.w - 28.0f, rowH};
            controlRects_[section.controls[i].parameter] = control;
            drawControl(section.controls[i], control, section.color);
        }
    }

    void drawControl(const ControlDef& control, const Rect& rect, const Color& color)
    {
        const float value = values_[control.parameter];
        const float norm = normalizedValue(control.parameter, value);
        const std::string textValue = formatValue(control.parameter, value);
        const std::string label = labelForControl(control);

        if (isDropdownParameter(control.parameter))
        {
            drawDropdownControl(control.parameter, label.c_str(), rect, color);
            return;
        }

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(213, 220, 217, 255);
        text(rect.x, rect.y, label.c_str(), nullptr);

        fontSize(10.5f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(color.r, color.g, color.b, 255);
        text(rect.x + rect.w, rect.y + 1.0f, textValue.c_str(), nullptr);

        const float barY = rect.y + 23.0f;
        beginPath();
        roundedRect(rect.x, barY, rect.w, 12.0f, 6.0f);
        fillColor(38, 44, 45, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, barY, std::max(8.0f, rect.w * norm), 12.0f, 6.0f);
        fillColor(color.r, color.g, color.b, 220);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + rect.w * norm - 5.0f, barY - 4.0f, 10.0f, 20.0f, 5.0f);
        fillColor(237, 242, 238, 255);
        fill();
        closePath();
    }

    std::string labelForControl(const ControlDef& control) const
    {
        const int algorithm = std::clamp(
            static_cast<int>(std::lround(values_[static_cast<std::size_t>(ParamId::sourceAlgorithm)])), 0, 7);

        if (control.parameter == static_cast<std::uint32_t>(ParamId::sourceParam1))
            return kSourceAlgorithmParamNames[static_cast<std::size_t>(algorithm)][0];
        if (control.parameter == static_cast<std::uint32_t>(ParamId::sourceParam2))
            return kSourceAlgorithmParamNames[static_cast<std::size_t>(algorithm)][1];
        if (control.parameter == static_cast<std::uint32_t>(ParamId::tuning))
            return "Tune";
        return control.label;
    }

    void drawDropdownControl(const std::uint32_t parameter, const char* const label, const Rect& rect, const Color& color)
    {
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(213, 220, 217, 255);
        text(rect.x, rect.y, label, nullptr);

        const Rect box {rect.x, rect.y + 21.0f, rect.w, 25.0f};
        const bool open = dropdownParameter_ == static_cast<int>(parameter);
        beginPath();
        roundedRect(box.x, box.y, box.w, box.h, 6.0f);
        fillColor(open ? 45 : 33, open ? 53 : 40, open ? 54 : 42, 255);
        fill();
        strokeColor(color.r, color.g, color.b, open ? 230 : 145);
        strokeWidth(open ? 1.5f : 1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(234, 239, 235, 255);
        text(box.x + 10.0f, box.y + box.h * 0.5f + 1.0f, formatValue(parameter, values_[parameter]).c_str(), nullptr);

        beginPath();
        moveTo(box.x + box.w - 18.0f, box.y + 10.0f);
        lineTo(box.x + box.w - 10.0f, box.y + 10.0f);
        lineTo(box.x + box.w - 14.0f, box.y + 16.0f);
        fillColor(color.r, color.g, color.b, 255);
        fill();
        closePath();
    }

    void drawOpenDropdown()
    {
        if (dropdownParameter_ < 0)
            return;

        const std::uint32_t parameter = static_cast<std::uint32_t>(dropdownParameter_);
        const std::size_t itemCount = dropdownItemCount(parameter);
        if (itemCount == 0)
            return;

        const Rect menu {
            dropdownRect_.x,
            dropdownRect_.y + dropdownRect_.h + 4.0f,
            dropdownRect_.w,
            26.0f * static_cast<float>(itemCount),
        };
        const int selected = static_cast<int>(std::lround(values_[parameter]));

        beginPath();
        roundedRect(menu.x, menu.y, menu.w, menu.h, 7.0f);
        fillColor(13, 17, 18, 248);
        fill();
        strokeColor(75, 88, 88, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (std::size_t i = 0; i < itemCount; ++i)
        {
            const bool isSelected = selected == static_cast<int>(i);
            const float y = menu.y + static_cast<float>(i) * 26.0f;
            if (isSelected)
            {
                beginPath();
                rect(menu.x + 4.0f, y + 3.0f, menu.w - 8.0f, 20.0f);
                fillColor(58, 89, 88, 235);
                fill();
                closePath();
            }

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(isSelected ? 244 : 205, isSelected ? 248 : 214, isSelected ? 243 : 211, 255);
            text(menu.x + 10.0f, y + 13.5f, dropdownItemName(parameter, i), nullptr);
        }
    }

    void updateParameterFromPosition(const std::uint32_t parameter, const float x, const Rect& rect)
    {
        if (parameter >= kParameterCount)
            return;
        const auto& spec = kParameterSpecs[parameter];
        const float norm = clampf((x - rect.x) / rect.w, 0.0f, 1.0f);
        commitParameter(parameter, spec.minimum + norm * (spec.maximum - spec.minimum));
    }

    void nudgeParameter(const std::uint32_t parameter, const float delta)
    {
        if (parameter >= kParameterCount)
            return;
        commitParameter(parameter, values_[parameter] + delta);
    }

    void commitParameter(const std::uint32_t parameter, const float value)
    {
        if (parameter >= kParameterCount)
            return;

        const auto& spec = kParameterSpecs[parameter];
        float clamped = clampf(value, spec.minimum, spec.maximum);
        if (spec.integer)
            clamped = std::round(clamped);

        editParameter(parameter, true);
        setParameterValue(parameter, clamped);
        editParameter(parameter, false);
        values_[parameter] = clamped;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloozyUI)
};

UI* createUI()
{
    return new FloozyUI();
}

END_NAMESPACE_DISTRHO
