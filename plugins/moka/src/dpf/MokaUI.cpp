#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"

#include "moka_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::moka::ParamId;
using downspout::moka::kInstrumentNames;
using downspout::moka::kParameterCount;
using downspout::moka::kParameterSpecs;
using downspout::moka::kPresets;

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

constexpr std::array<SectionDef, 2> kSections = {{
    {"Body", {199, 145, 86}, {{{1, "Decay"}, {2, "Mallet"}, {3, "Tone"}, {7, "Level"}}}, 4},
    {"Modal", {93, 158, 180}, {{{4, "Spread"}, {5, "Position"}, {8, "Release"}, {9, "Width"}}}, 4},
}};

inline constexpr std::size_t kCustomPreset = kPresets.size();

std::size_t matchPreset(const std::array<float, kParameterCount>& values)
{
    for (std::size_t p = 0; p < kPresets.size(); ++p)
    {
        bool match = true;
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
        {
            if (std::fabs(values[i] - kPresets[p].values[i]) > 1.0e-4f)
            {
                match = false;
                break;
            }
        }
        if (match)
            return p;
    }
    return kCustomPreset;
}

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
    if (parameter == static_cast<std::uint32_t>(ParamId::instrument))
    {
        const std::size_t index = std::min<std::size_t>(kInstrumentNames.size() - 1,
                                                        static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(value)))));
        std::snprintf(buffer, sizeof(buffer), "%s", kInstrumentNames[index]);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::voices))
    {
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(clampf(value, 1.0f, 12.0f))));
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::release))
    {
        const float t60 = 0.03f * std::pow(50.0f, clampf(value, 0.0f, 1.0f));
        if (t60 < 1.0f)
            std::snprintf(buffer, sizeof(buffer), "%dms", static_cast<int>(std::lround(t60 * 1000.0f)));
        else
            std::snprintf(buffer, sizeof(buffer), "%.1fs", t60);
    }
    else if (parameter == static_cast<std::uint32_t>(ParamId::decay))
    {
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    }
    return buffer;
}

} // namespace

class MokaUI : public UI {
public:
    MokaUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;
        presetIndex_ = matchPreset(values_);

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
            presetIndex_ = matchPreset(values_);
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
        drawSection(0, {pad, top, sectionW, sectionH});
        drawSection(1, {pad + (sectionW + gap), top, sectionW, sectionH});
        drawRoleColumn({pad + 2.0f * (sectionW + gap), top, sectionW, sectionH});
        drawStatusColumn({pad + 3.0f * (sectionW + gap), top, sectionW, sectionH});
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

        if (openDropdown_)
        {
            if (handleOpenDropdownClick(ev.pos.getX(), ev.pos.getY()))
                return true;
            openDropdown_ = false;
        }

        // Theme toggle lives in the status column.
        if (themeRect_.contains(ev.pos.getX(), ev.pos.getY()))
        {
            darkTheme_ = !darkTheme_;
            repaint();
            return true;
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i)
        {
            if (selectorRects_[i].contains(ev.pos.getX(), ev.pos.getY()))
            {
                activeParameter_ = -1;
                openDropdown_ = true;
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
    const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }

    struct ControlRect {
        int parameter = -1;
        Rect bounds {};
    };

    void drawBackground(const float width, const float height)
    {
        const auto& t = theme();
        beginPath();
        fc(t.panel);
        rect(0.0f, 0.0f, width, height);
        fill();

        beginPath();
        fc(t.surface);
        rect(0.0f, 0.0f, width, 104.0f);
        fill();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        const auto& t = theme();
        beginPath();
        fc(t.textPrimary);
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y + 3.0f, "Moka", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(x + 110.0f, y + 13.0f, "modal hit-object synth", nullptr);

        fc(t.textDim);
        fontSize(12.0f);
        text(x, y + h - 18.0f, "xylophone, glockenspiel, woodblock, glass bowl, metal sheet, tube", nullptr);

        // Voice-count lamp in the header.
        char voices[32];
        std::snprintf(voices, sizeof(voices), "POLY %d",
                      static_cast<int>(std::lround(values_[static_cast<std::size_t>(ParamId::voices)])));
        fc(t.accent);
        fontSize(14.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(x + w, y + 8.0f, voices, nullptr);
    }

    void drawSection(const std::size_t index, const Rect rect)
    {
        const auto& t = theme();
        const auto& section = kSections[index];
        beginPath();
        fc(t.surface);
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fill();

        beginPath();
        fillColor(section.color.r, section.color.g, section.color.b, 255);
        roundedRect(rect.x, rect.y, rect.w, 42.0f, 7.0f);
        fill();

        fc(t.panel);
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

    void drawRoleColumn(const Rect rect)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fill();

        beginPath();
        fillColor(178, 128, 188, 255);
        roundedRect(rect.x, rect.y, rect.w, 42.0f, 7.0f);
        fill();

        fc(t.panel);
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(rect.x + 14.0f, rect.y + 21.0f, "Sound", nullptr);

        const Rect presetRect {rect.x + 14.0f, rect.y + 54.0f, rect.w - 28.0f, 90.0f};
        selectorRects_[0] = presetRect;
        drawPresetBox(presetRect, openDropdown_);

        // Read-only instrument readout: the preset menu above is the only
        // place that changes the model, so the two can never disagree.
        const float x = rect.x + 14.0f;
        float y = rect.y + 170.0f;
        fc(t.textDim);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Model", nullptr);
        y += 22.0f;

        const std::string model = formatValue(static_cast<std::uint32_t>(ParamId::instrument),
                                              values_[static_cast<std::size_t>(ParamId::instrument)]);
        fc(t.textPrimary);
        fontSize(14.0f);
        text(x, y, model.c_str(), nullptr);
        y += 30.0f;

        fc(t.textDim);
        fontSize(12.0f);
        text(x, y, "A preset sets all", nullptr);
        text(x, y + 16.0f, "ten controls at once.", nullptr);
        text(x, y + 36.0f, "Any tweak shows", nullptr);
        text(x, y + 52.0f, "Custom above.", nullptr);
    }

    void drawStatusColumn(const Rect rect)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(rect.x, rect.y, rect.w, rect.h, 7.0f);
        fill();

        beginPath();
        fillColor(132, 168, 104, 255);
        roundedRect(rect.x, rect.y, rect.w, 42.0f, 7.0f);
        fill();

        fc(t.panel);
        fontSize(16.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(rect.x + 14.0f, rect.y + 21.0f, "Panel", nullptr);

        const float x = rect.x + 14.0f;
        const float w = rect.w - 28.0f;
        float y = rect.y + 60.0f;

        fc(t.textDim);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Theme", nullptr);
        y += 24.0f;

        themeRect_ = {x, y, w, 30.0f};
        beginPath();
        fc(darkTheme_ ? t.controlTrack : t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, 5.0f);
        fill();
        fc(t.textPrimary);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 10.0f, y + 16.0f, darkTheme_ ? "Dark" : "Light", nullptr);
        fc(t.textDim);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x + w - 10.0f, y + 16.0f, darkTheme_ ? "o-" : "-o", nullptr);
        y += 48.0f;

        const Rect voicesRect {x, y, w, 64.0f};
        drawSlider(static_cast<std::uint32_t>(ParamId::voices), "Voices", voicesRect, {132, 168, 104});
        y += 76.0f;

        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Voices cap stealing:", nullptr);
        text(x, y + 16.0f, "oldest note gives way.", nullptr);
    }

    const char* presetLabel() const
    {
        return presetIndex_ < kPresets.size() ? kPresets[presetIndex_].name : "Custom";
    }

    void drawPresetBox(const Rect rect, const bool open)
    {
        const auto& t = theme();
        fc(t.textDim);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(rect.x, rect.y, "Preset", nullptr);

        const Rect box {rect.x, rect.y + 24.0f, rect.w, 30.0f};
        beginPath();
        fillColor(open ? 58 : 24, open ? 45 : 28, open ? 62 : 30, 255);
        roundedRect(box.x, box.y, box.w, box.h, 5.0f);
        fill();

        fc(t.textPrimary);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 10.0f, box.y + box.h * 0.5f + 1.0f, presetLabel(), nullptr);

        fc(t.textDim);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 10.0f, box.y + box.h * 0.5f, open ? "^" : "v", nullptr);
    }

    void drawOpenDropdown()
    {
        if (!openDropdown_)
            return;

        const auto& t = theme();
        const Rect base = selectorRects_[0];
        const Rect menu {base.x, base.y + 56.0f, base.w, 28.0f * static_cast<float>(kPresets.size())};

        beginPath();
        fc(t.panel.withAlpha(250));
        roundedRect(menu.x, menu.y, menu.w, menu.h, 6.0f);
        fill();

        beginPath();
        strokeColor(178, 128, 188, 220);
        strokeWidth(1.0f);
        roundedRect(menu.x, menu.y, menu.w, menu.h, 6.0f);
        stroke();

        for (std::size_t i = 0; i < kPresets.size(); ++i)
        {
            const float y = menu.y + static_cast<float>(i) * 28.0f;
            if (presetIndex_ == i)
            {
                beginPath();
                fc(t.border);
                roundedRect(menu.x + 4.0f, y + 3.0f, menu.w - 8.0f, 22.0f, 5.0f);
                fill();
            }

            fc(t.textPrimary);
            fontSize(13.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(menu.x + 10.0f, y + 14.0f, kPresets[i].name, nullptr);
        }
    }

    bool handleOpenDropdownClick(const float x, const float y)
    {
        const Rect base = selectorRects_[0];
        if (base.contains(x, y))
            return false;

        const Rect menu {base.x, base.y + 56.0f, base.w, 28.0f * static_cast<float>(kPresets.size())};
        if (!menu.contains(x, y))
            return false;

        const int item = std::clamp(static_cast<int>((y - menu.y) / 28.0f), 0, static_cast<int>(kPresets.size()) - 1);
        presetIndex_ = static_cast<std::size_t>(item);
        for (std::uint32_t p = 0; p < kParameterCount; ++p)
            commitParameterSilent(p, kPresets[presetIndex_].values[p]);
        presetIndex_ = matchPreset(values_);
        openDropdown_ = false;
        repaint();
        return true;
    }

    void drawSlider(const std::uint32_t parameter, const char* label, const Rect rect, const Color color)
    {
        const auto& t = theme();
        const float normalized = normalizedValue(parameter, values_[parameter]);
        fc(t.textDim);
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(rect.x, rect.y, label, nullptr);

        const std::string value = formatValue(parameter, values_[parameter]);
        fc(t.textDim);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(rect.x + rect.w, rect.y, value.c_str(), nullptr);

        const float trackY = rect.y + 27.0f;
        beginPath();
        fc(t.panel);
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
        commitParameterSilent(parameter, value);
        presetIndex_ = matchPreset(values_);
        repaint();
    }

    void commitParameterSilent(const std::uint32_t parameter, const float value)
    {
        values_[parameter] = value;
        setParameterValue(parameter, value);
        repaint();
    }

    std::array<float, kParameterCount> values_ {};
    std::array<ControlRect, 32> controlRects_ {};
    std::array<Rect, 1> selectorRects_ {};
    Rect themeRect_ {};
    std::size_t controlRectCount_ = 0;
    int activeParameter_ = -1;
    bool openDropdown_ = false;
    std::size_t presetIndex_ = 0;
    bool darkTheme_ = true;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MokaUI)
};

UI* createUI()
{
    return new MokaUI();
}

END_NAMESPACE_DISTRHO
