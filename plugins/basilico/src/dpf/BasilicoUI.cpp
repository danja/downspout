#include "DistrhoUI.hpp"

#include "basilico_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::basilico::ParamId;
using downspout::basilico::kDriveTypeNames;
using downspout::basilico::kModelNames;
using downspout::basilico::kParameterCount;
using downspout::basilico::kParameterSpecs;
using downspout::basilico::kWaveformNames;
using downspout::basilico::kWobbleDivisionNames;
using downspout::basilico::kWobbleShapeNames;
using downspout::basilico::kWobbleSyncNames;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(const float px, const float py) const noexcept
    {
        return w > 0.0f && h > 0.0f && px >= x && px <= x + w && py >= y && py <= y + h;
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
    std::array<ControlDef, 7> controls;
    std::size_t count;
};

constexpr std::uint32_t paramId(const ParamId id)
{
    return static_cast<std::uint32_t>(id);
}

constexpr std::array<SectionDef, 6> kSections = {{
    {"Voice", {112, 177, 139}, {{{paramId(ParamId::model), "Model"}, {paramId(ParamId::waveform), "Wave"}, {paramId(ParamId::subLevel), "Sub"}, {paramId(ParamId::body), "Body"}, {paramId(ParamId::bite), "Bite"}, {0, ""}, {0, ""}}}, 5},
    {"Phrase", {215, 163, 78}, {{{paramId(ParamId::mute), "Mute"}, {paramId(ParamId::glide), "Glide"}, {paramId(ParamId::accent), "Accent"}, {paramId(ParamId::punch), "Punch"}, {0, ""}, {0, ""}, {0, ""}}}, 4},
    {"Filter", {105, 158, 218}, {{{paramId(ParamId::cutoff), "Cutoff"}, {paramId(ParamId::resonance), "Res"}, {paramId(ParamId::filterEnv), "Env"}, {paramId(ParamId::keyTrack), "Track"}, {paramId(ParamId::lfoDepth), "Wob Filter"}, {paramId(ParamId::squelch), "Squelch"}, {0, ""}}}, 6},
    {"Wobble", {92, 185, 194}, {{{paramId(ParamId::lfoFrequency), "Rate"}, {paramId(ParamId::wobbleStart), "Start"}, {paramId(ParamId::wobbleSync), "Sync"}, {paramId(ParamId::wobbleDivision), "Division"}, {paramId(ParamId::wobbleShape), "Shape"}, {paramId(ParamId::ampWobble), "Amp"}, {paramId(ParamId::phaseWobble), "Phase"}}}, 7},
    {"Envelope", {176, 132, 214}, {{{paramId(ParamId::attack), "Attack"}, {paramId(ParamId::decay), "Decay"}, {paramId(ParamId::sustain), "Sustain"}, {paramId(ParamId::release), "Release"}, {0, ""}, {0, ""}, {0, ""}}}, 4},
    {"Drive", {214, 112, 92}, {{{paramId(ParamId::driveType), "Type"}, {paramId(ParamId::drive), "Drive"}, {paramId(ParamId::output), "Output"}, {0, ""}, {0, ""}, {0, ""}, {0, ""}}}, 3},
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

bool isDropdownParameter(const std::uint32_t parameter)
{
    return parameter == static_cast<std::uint32_t>(ParamId::model) ||
           parameter == static_cast<std::uint32_t>(ParamId::waveform) ||
           parameter == static_cast<std::uint32_t>(ParamId::driveType) ||
           parameter == static_cast<std::uint32_t>(ParamId::wobbleSync) ||
           parameter == static_cast<std::uint32_t>(ParamId::wobbleDivision) ||
           parameter == static_cast<std::uint32_t>(ParamId::wobbleShape);
}

std::size_t dropdownItemCount(const std::uint32_t parameter)
{
    if (parameter == static_cast<std::uint32_t>(ParamId::model))
        return kModelNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::waveform))
        return kWaveformNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::driveType))
        return kDriveTypeNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::wobbleSync))
        return kWobbleSyncNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::wobbleDivision))
        return kWobbleDivisionNames.size();
    if (parameter == static_cast<std::uint32_t>(ParamId::wobbleShape))
        return kWobbleShapeNames.size();
    return 0;
}

const char* dropdownItemName(const std::uint32_t parameter, const std::size_t index)
{
    if (parameter == static_cast<std::uint32_t>(ParamId::model))
        return kModelNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::waveform))
        return kWaveformNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::driveType))
        return kDriveTypeNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::wobbleSync))
        return kWobbleSyncNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::wobbleDivision))
        return kWobbleDivisionNames[index];
    if (parameter == static_cast<std::uint32_t>(ParamId::wobbleShape))
        return kWobbleShapeNames[index];
    return "";
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[48];
    if (isDropdownParameter(parameter))
    {
        const std::size_t count = dropdownItemCount(parameter);
        const std::size_t index = std::min<std::size_t>(count - 1, static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(value)))));
        std::snprintf(buffer, sizeof(buffer), "%s", dropdownItemName(parameter, index));
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::output))
    {
        std::snprintf(buffer, sizeof(buffer), "%.1fx", clampf(value, 0.0f, 1.0f) * 2.0f);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::lfoFrequency))
    {
        std::snprintf(buffer, sizeof(buffer), "%.2f Hz", value);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::wobbleStart))
    {
        std::snprintf(buffer, sizeof(buffer), "%d deg", static_cast<int>(std::lround(clampf(value, 0.0f, 360.0f))));
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    }
    return buffer;
}

} // namespace

class BasilicoUI : public UI
{
public:
    BasilicoUI()
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
        const float pad = 22.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 76.0f);

        const float top = 124.0f;
        const float gap = 14.0f;
        const float sectionW = (width - pad * 2.0f - gap * 5.0f) / 6.0f;
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

        if (dropdownParameter_ >= 0 && handleDropdownClick(x, y))
            return true;

        for (std::size_t i = 0; i < controlRects_.size(); ++i)
        {
            if (!controlRects_[i].contains(x, y))
                continue;

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

        dropdownParameter_ = -1;
        repaint();
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
            if (!controlRects_[i].contains(x, y))
                continue;
            const auto& spec = kParameterSpecs[i];
            nudgeParameter(static_cast<std::uint32_t>(i), direction * (spec.integer ? 1.0f : 0.02f));
            return true;
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
        fillColor(12, 15, 14, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(28, 34, 31, 255);
        rect(0.0f, 0.0f, width, 112.0f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fillColor(18, 23, 21, 240);
        fill();
        strokeColor(51, 62, 57, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(32.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 243, 237, 255);
        text(x + 22.0f, y + 13.0f, "Basilico", nullptr);

        fontSize(13.0f);
        fillColor(157, 169, 162, 255);
        text(x + 180.0f, y + 27.0f, "upright, electric, dub, acid, industrial bass", nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(210, 219, 212, 255);
        text(x + w - 22.0f, y + 29.0f, "MIDI in | stereo out", nullptr);
    }

    void drawSection(const std::size_t index, const Rect& rect)
    {
        const SectionDef& section = kSections[index];
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fillColor(18, 22, 21, 240);
        fill();
        strokeColor(45, 55, 51, 255);
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
        fillColor(233, 238, 233, 255);
        text(rect.x + 14.0f, rect.y + 33.0f, section.title, nullptr);

        const float startY = rect.y + 78.0f;
        const float rowH = 42.0f;
        const float rowGap = 14.0f;
        for (std::size_t i = 0; i < section.count; ++i)
        {
            const Rect control {rect.x + 14.0f, startY + static_cast<float>(i) * (rowH + rowGap), rect.w - 28.0f, rowH};
            controlRects_[section.controls[i].parameter] = control;
            drawControl(section.controls[i], control, section.color);
        }
    }

    void drawControl(const ControlDef& control, const Rect& rect, const Color& color)
    {
        if (isDropdownParameter(control.parameter))
        {
            drawDropdownControl(control, rect, color);
            return;
        }

        const float value = values_[control.parameter];
        const float norm = normalizedValue(control.parameter, value);
        const std::string textValue = formatValue(control.parameter, value);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(213, 220, 215, 255);
        text(rect.x, rect.y, control.label, nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(color.r, color.g, color.b, 255);
        text(rect.x + rect.w, rect.y + 1.0f, textValue.c_str(), nullptr);

        const float barY = rect.y + 23.0f;
        beginPath();
        roundedRect(rect.x, barY, rect.w, 12.0f, 6.0f);
        fillColor(38, 45, 42, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x, barY, std::max(8.0f, rect.w * norm), 12.0f, 6.0f);
        fillColor(color.r, color.g, color.b, 220);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + rect.w * norm - 5.0f, barY - 4.0f, 10.0f, 20.0f, 5.0f);
        fillColor(238, 242, 237, 255);
        fill();
        closePath();
    }

    void drawDropdownControl(const ControlDef& control, const Rect& rect, const Color& color)
    {
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(213, 220, 215, 255);
        text(rect.x, rect.y, control.label, nullptr);

        const Rect box {rect.x, rect.y + 21.0f, rect.w, 25.0f};
        const bool open = dropdownParameter_ == static_cast<int>(control.parameter);
        beginPath();
        roundedRect(box.x, box.y, box.w, box.h, 6.0f);
        fillColor(open ? 45 : 33, open ? 54 : 41, open ? 50 : 39, 255);
        fill();
        strokeColor(color.r, color.g, color.b, open ? 230 : 150);
        strokeWidth(open ? 1.5f : 1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(235, 240, 235, 255);
        text(box.x + 10.0f, box.y + box.h * 0.5f + 1.0f, formatValue(control.parameter, values_[control.parameter]).c_str(), nullptr);

        beginPath();
        moveTo(box.x + box.w - 18.0f, box.y + 10.0f);
        lineTo(box.x + box.w - 10.0f, box.y + 10.0f);
        lineTo(box.x + box.w - 14.0f, box.y + 16.0f);
        fillColor(color.r, color.g, color.b, 255);
        fill();
        closePath();
    }

    bool handleDropdownClick(const float x, const float y)
    {
        const std::uint32_t parameter = static_cast<std::uint32_t>(dropdownParameter_);
        const std::size_t itemCount = dropdownItemCount(parameter);
        const Rect menu {dropdownRect_.x, dropdownRect_.y + dropdownRect_.h + 4.0f, dropdownRect_.w, 26.0f * static_cast<float>(itemCount)};

        if (menu.contains(x, y))
        {
            const std::size_t item = std::min<std::size_t>(itemCount - 1, static_cast<std::size_t>((y - menu.y) / 26.0f));
            commitParameter(parameter, static_cast<float>(item));
            dropdownParameter_ = -1;
            return true;
        }

        if (!dropdownRect_.contains(x, y))
        {
            dropdownParameter_ = -1;
            repaint();
        }
        return false;
    }

    void drawOpenDropdown()
    {
        if (dropdownParameter_ < 0)
            return;

        const std::uint32_t parameter = static_cast<std::uint32_t>(dropdownParameter_);
        const std::size_t itemCount = dropdownItemCount(parameter);
        const Rect menu {dropdownRect_.x, dropdownRect_.y + dropdownRect_.h + 4.0f, dropdownRect_.w, 26.0f * static_cast<float>(itemCount)};
        const int selected = static_cast<int>(std::lround(values_[parameter]));

        beginPath();
        roundedRect(menu.x, menu.y, menu.w, menu.h, 7.0f);
        fillColor(13, 17, 16, 248);
        fill();
        strokeColor(76, 91, 84, 255);
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
                fillColor(54, 91, 73, 235);
                fill();
                closePath();
            }

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(isSelected ? 244 : 205, isSelected ? 248 : 216, isSelected ? 241 : 211, 255);
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
        commitParameter(parameter, values_[parameter] + delta * (kParameterSpecs[parameter].maximum - kParameterSpecs[parameter].minimum));
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BasilicoUI)
};

UI* createUI()
{
    return new BasilicoUI();
}

END_NAMESPACE_DISTRHO
