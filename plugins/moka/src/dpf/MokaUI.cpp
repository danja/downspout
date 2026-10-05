#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "moka_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::moka::computeModeWeights;
using downspout::moka::kInstrumentNames;
using downspout::moka::kMaxVoices;
using downspout::moka::kModeCount;
using downspout::moka::kParameterCount;
using downspout::moka::kParameterSpecs;
using downspout::moka::kPresets;
using downspout::moka::modelRingTimeSeconds;
using downspout::moka::toneCutoffHz;
using downspout::moka::ModeWeights;
using downspout::moka::ParamId;

constexpr float kPi = 3.14159265358979323846f;

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

// Same palette as the rest of the set: amber for the strike, cold steel for
// the spectrum, olive for the presets, hot metal for polyphony.
constexpr Accent kStrikeAccent {198, 132, 58};
constexpr Accent kPartialAccent {92, 140, 156};
constexpr Accent kPresetAccent {124, 148, 104};
constexpr Accent kPolyAccent {176, 88, 62};

[[nodiscard]] float clampf(const float value, const float minimum, const float maximum)
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] std::uint32_t idx(const ParamId id)
{
    return static_cast<std::uint32_t>(id);
}

[[nodiscard]] float normalizedValue(const std::uint32_t parameter, const float value)
{
    const auto& spec = kParameterSpecs[parameter];
    if (spec.maximum <= spec.minimum)
        return 0.0f;
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

std::string formatSeconds(const float seconds)
{
    char buffer[32];
    if (seconds < 1.0f)
        std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(seconds * 1000.0f)));
    else
        std::snprintf(buffer, sizeof(buffer), "%.1f s", static_cast<double>(seconds));
    return buffer;
}

std::string formatHz(const float hz)
{
    char buffer[32];
    if (hz >= 1000.0f)
        std::snprintf(buffer, sizeof(buffer), "%.1f kHz", static_cast<double>(hz) / 1000.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%d Hz", static_cast<int>(std::lround(hz)));
    return buffer;
}

std::string formatPercent(const float value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%d%%",
                  static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
    return buffer;
}

}  // namespace

class MokaUI : public UI {
public:
    MokaUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;
        presetIndex_ = matchPreset(values_);

        // The stepper indexes its label table by parameter value, so entry 4
        // has to read "4" — not the fourth voice count.
        for (std::size_t i = 0; i < kMaxVoices; ++i)
        {
            std::snprintf(voiceLabels_[i].data(), voiceLabels_[i].size(), "%d", static_cast<int>(i));
            voiceNames_[i] = voiceLabels_[i].data();
        }

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

        sliderCount_ = 0;
        stepperCount_ = 0;
        presetCount_ = 0;

        drawBackground(width, height);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});

        drawStrikeStrip({24.0f, 108.0f, width - 48.0f, 176.0f});

        const float columnW = (width - 48.0f - 32.0f) / 3.0f;
        const float columnY = 300.0f;
        const float columnH = height - columnY - 24.0f;
        drawPresetPanel({24.0f, columnY, columnW, columnH});
        drawPartialPanel({24.0f + columnW + 16.0f, columnY, columnW, columnH});
        drawPolyPanel({24.0f + 2.0f * (columnW + 16.0f), columnY, columnW, columnH});
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press)
        {
            activeSlider_ = -1;
            return false;
        }

        const float mx = ev.pos.getX();
        const float my = ev.pos.getY();

        if (themeRect_.contains(mx, my))
        {
            darkTheme_ = !darkTheme_;
            repaint();
            return true;
        }

        for (std::size_t i = 0; i < presetCount_; ++i)
        {
            if (presetRects_[i].bounds.contains(mx, my))
            {
                applyPreset(presetRects_[i].index);
                return true;
            }
        }

        for (std::size_t i = 0; i < stepperCount_; ++i)
        {
            const Stepper& stepper = steppers_[i];
            if (!stepper.bounds.contains(mx, my))
                continue;
            const float middle = stepper.bounds.x + stepper.bounds.w * 0.5f;
            step(stepper.parameter, (mx < middle) ? -1 : 1);
            return true;
        }

        for (std::size_t i = 0; i < sliderCount_; ++i)
        {
            if (sliders_[i].bounds.contains(mx, my))
            {
                activeSlider_ = static_cast<int>(sliders_[i].parameter);
                setFromMouse(sliders_[i], mx);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0)
            return false;
        for (std::size_t i = 0; i < sliderCount_; ++i)
        {
            if (static_cast<int>(sliders_[i].parameter) == activeSlider_)
            {
                setFromMouse(sliders_[i], ev.pos.getX());
                return true;
            }
        }
        return false;
    }

private:
    struct Slider {
        std::uint32_t parameter = 0;
        Rect bounds {};
    };

    struct Stepper {
        std::uint32_t parameter = 0;
        Rect bounds {};
    };

    struct PresetRow {
        std::size_t index = 0;
        Rect bounds {};
    };

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    [[nodiscard]] float value(const ParamId id) const { return values_[idx(id)]; }
    [[nodiscard]] int intValue(const ParamId id) const { return static_cast<int>(std::lround(values_[idx(id)])); }

    [[nodiscard]] const char* const* voiceNameTable() const { return voiceNames_.data(); }

    [[nodiscard]] int model() const
    {
        return std::clamp(intValue(ParamId::instrument), 0, static_cast<int>(kInstrumentNames.size()) - 1);
    }

    // Every control reports its percentage plus whatever real-world unit makes
    // it readable, so "62%  5.6 kHz" says what the value will actually do.
    [[nodiscard]] std::string formatValue(const std::uint32_t parameter, const float raw) const
    {
        const auto id = static_cast<ParamId>(parameter);
        char buffer[64];

        switch (id)
        {
        case ParamId::instrument:
            std::snprintf(buffer, sizeof(buffer), "%s", kInstrumentNames[static_cast<std::size_t>(model())]);
            break;
        case ParamId::voices:
            std::snprintf(buffer, sizeof(buffer), "%d",
                          static_cast<int>(std::lround(clampf(raw, 1.0f, static_cast<float>(kMaxVoices)))));
            break;
        case ParamId::decay:
            std::snprintf(buffer, sizeof(buffer), "%s  %s", formatPercent(raw).c_str(),
                          formatSeconds(modelRingTimeSeconds(model(), raw)).c_str());
            break;
        case ParamId::tone:
            std::snprintf(buffer, sizeof(buffer), "%s  %s", formatPercent(raw).c_str(),
                          formatHz(toneCutoffHz(raw)).c_str());
            break;
        case ParamId::position:
            // Edge is bright and clangorous, centre is round; name the side.
            std::snprintf(buffer, sizeof(buffer), "%s  %s", formatPercent(raw).c_str(),
                          raw < 0.34f ? "edge" : (raw > 0.66f ? "centre" : "mid"));
            break;
        case ParamId::release:
            std::snprintf(buffer, sizeof(buffer), "%s",
                          formatSeconds(0.03f * std::pow(50.0f, clampf(raw, 0.0f, 1.0f))).c_str());
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%s", formatPercent(raw).c_str());
            break;
        }
        return buffer;
    }

    void commit(const std::uint32_t parameter, const float raw)
    {
        const auto& spec = kParameterSpecs[parameter];
        float v = clampf(raw, spec.minimum, spec.maximum);
        if (spec.integer)
            v = std::round(v);
        values_[parameter] = v;
        setParameterValue(parameter, v);
        presetIndex_ = matchPreset(values_);
        repaint();
    }

    void applyPreset(const std::size_t index)
    {
        if (index >= kPresets.size())
            return;
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
        {
            values_[i] = kPresets[index].values[i];
            setParameterValue(i, values_[i]);
        }
        presetIndex_ = index;
        repaint();
    }

    // Any control that no longer matches a factory snapshot reads as Custom.
    [[nodiscard]] std::size_t matchPreset(const std::array<float, kParameterCount>& candidate) const
    {
        for (std::size_t p = 0; p < kPresets.size(); ++p)
        {
            bool match = true;
            for (std::uint32_t i = 0; i < kParameterCount; ++i)
            {
                if (std::fabs(candidate[i] - kPresets[p].values[i]) > 1.0e-4f)
                {
                    match = false;
                    break;
                }
            }
            if (match)
                return p;
        }
        return kPresets.size();
    }

    void step(const std::uint32_t parameter, const int direction)
    {
        const auto& spec = kParameterSpecs[parameter];
        const float next = values_[parameter] + static_cast<float>(direction);
        const float span = spec.maximum - spec.minimum + 1.0f;
        float wrapped = next;
        if (wrapped < spec.minimum)
            wrapped += span;
        if (wrapped > spec.maximum)
            wrapped -= span;
        commit(parameter, wrapped);
    }

    void setFromMouse(const Slider& slider, const float mouseX)
    {
        const auto& spec = kParameterSpecs[slider.parameter];
        const float normalized = clampf((mouseX - slider.bounds.x) / std::max(1.0f, slider.bounds.w), 0.0f, 1.0f);
        commit(slider.parameter, spec.minimum + normalized * (spec.maximum - spec.minimum));
    }

    void remember(const std::uint32_t parameter, const Rect bounds)
    {
        if (sliderCount_ < sliders_.size())
            sliders_[sliderCount_++] = {parameter, bounds};
    }

    // ── Primitives ─────────────────────────────────────────────────────────

    void drawBackground(const float width, const float height)
    {
        const auto& t = theme();
        beginPath();
        fc(t.background);
        rect(0.0f, 0.0f, width, height);
        fill();

        beginPath();
        fc(t.panel);
        rect(0.0f, 0.0f, width, 96.0f);
        fill();
    }

    void drawHeader(const Rect bounds)
    {
        const auto& t = theme();

        fc(t.textPrimary);
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, "Moka", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(bounds.x + 106.0f, bounds.y + 10.0f, "modal hit-object synth", nullptr);

        fc(t.textDim);
        fontSize(12.0f);
        text(bounds.x, bounds.y + 44.0f,
             "each voice is eight damped sine modes struck by a mallet burst — "
             "xylophone, glockenspiel, woodblock, glass bowl, metal sheet, tube",
             nullptr);

        // Voice-cap lamp, the one live readout in the header.
        const float lampX = bounds.x + bounds.w - 188.0f;
        beginPath();
        circle(lampX, bounds.y + 18.0f, 7.0f);
        fillColor(kStrikeAccent.r, kStrikeAccent.g, kStrikeAccent.b, 255);
        fill();

        fc(t.textPrimary);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, bounds.y + 18.0f, "POLYPHONY", nullptr);

        // Theme toggle.
        themeRect_ = {bounds.x + bounds.w - 74.0f, bounds.y + 4.0f, 74.0f, 26.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall);
        fill();
        fc(t.textDim);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f,
             darkTheme_ ? "DARK" : "LIGHT", nullptr);
    }

    void drawPanel(const Rect bounds, const char* title, const Accent accent)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusPanel);
        fill();

        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        roundedRect(bounds.x, bounds.y, bounds.w, 32.0f, laf::kRadiusPanel);
        fill();
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        rect(bounds.x, bounds.y + 20.0f, bounds.w, 12.0f);
        fill();

        fillColor(250, 248, 242, 255);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(bounds.x + 12.0f, bounds.y + 16.0f, title, nullptr);
    }

    void drawSlider(const ParamId id, const char* label, const Rect bounds, const Accent accent)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);
        const float normalized = normalizedValue(parameter, values_[parameter]);

        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const std::string shown = formatValue(parameter, values_[parameter]);
        fc(t.textPrimary);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(bounds.x + bounds.w, bounds.y, shown.c_str(), nullptr);

        const float trackY = bounds.y + 17.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(bounds.x, trackY, bounds.w, 10.0f, 3.0f);
        fill();

        if (normalized > 0.002f)
        {
            beginPath();
            fillColor(accent.r, accent.g, accent.b, 255);
            roundedRect(bounds.x, trackY, bounds.w * normalized, 10.0f, 3.0f);
            fill();
        }

        remember(parameter, {bounds.x, trackY - 9.0f, bounds.w, 28.0f});
    }

    // Compact value stepper: click the left half to go down, the right to go up.
    void drawStepper(const ParamId id, const char* label, const Rect bounds, const char* const* names)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);

        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const Rect box {bounds.x, bounds.y + 15.0f, bounds.w, 22.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        stroke();

        // Index by value and clamp, so the table can never be read past its end.
        const int selection = std::clamp(static_cast<int>(std::lround(values_[parameter])), 0,
                                         static_cast<int>(kMaxVoices) - 1);
        // A stepper draws one of a fixed set of labels, so index rather than
        // format; both callers pass a table that covers the parameter's range.
        const char* const name = names[selection];
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, name, nullptr);

        fc(t.textDim);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);

        if (stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {parameter, box};
    }

    // ── Sections ───────────────────────────────────────────────────────────

    void drawStrikeStrip(const Rect bounds)
    {
        drawPanel(bounds, "STRIKE — excitation and resonance", kStrikeAccent);

        drawRingDial({bounds.x + 16.0f, bounds.y + 44.0f, 122.0f, 122.0f});

        const float columnW = 240.0f;
        // A stepper is taller than a slider (label plus a 22px box), so the row
        // pitch has to clear the taller of the two.
        const float rowH = 38.0f;
        const float top = bounds.y + 44.0f;
        const float colA = bounds.x + 154.0f;
        const float colB = colA + columnW + 21.0f;
        const float colC = colB + columnW + 21.0f;

        drawStepper(ParamId::instrument, "Model", {colA, top, columnW, rowH}, kInstrumentNames.data());
        drawSlider(ParamId::mallet, "Mallet  (beater hardness)", {colA, top + rowH, columnW, rowH}, kStrikeAccent);
        drawSlider(ParamId::position, "Strike position", {colA, top + 2.0f * rowH, columnW, rowH}, kStrikeAccent);

        drawSlider(ParamId::decay, "Decay  (ring time)", {colB, top, columnW, rowH}, kStrikeAccent);
        drawSlider(ParamId::spread, "Spread  (inharmonicity)", {colB, top + rowH, columnW, rowH}, kStrikeAccent);
        drawSlider(ParamId::release, "Release  (note-off)", {colB, top + 2.0f * rowH, columnW, rowH}, kStrikeAccent);

        drawSlider(ParamId::tone, "Tone  (cutoff)", {colC, top, columnW, rowH}, kStrikeAccent);
        drawSlider(ParamId::width, "Stereo width", {colC, top + rowH, columnW, rowH}, kStrikeAccent);
        drawSlider(ParamId::level, "Output", {colC, top + 2.0f * rowH, columnW, rowH}, kStrikeAccent);

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(colA, bounds.y + bounds.h - 18.0f,
             "Mallet sets beater hardness and Strike position runs edge to centre. "
             "Decay and Spread bend the eight partials plotted below.", nullptr);
    }

    // Round gauge reading the fundamental's ring time, which is what Decay
    // controls once the instrument's own base is taken into account.
    void drawRingDial(const Rect bounds)
    {
        const auto& t = theme();
        const float cx = bounds.x + bounds.w * 0.5f;
        const float cy = bounds.y + bounds.h * 0.5f - 4.0f;
        const float radius = bounds.w * 0.5f;

        const float seconds = modelRingTimeSeconds(model(), value(ParamId::decay));
        // Log sweep from 0.1 s to 10 s, so the short percussive models still
        // get most of the travel.
        constexpr float lo = -2.302585f; // log(0.1)
        constexpr float hi = 2.302585f;  // log(10)
        const float normalized = clampf((std::log(std::max(0.1f, seconds)) - lo) / (hi - lo), 0.0f, 1.0f);

        constexpr float startAngle = 0.75f * kPi;
        constexpr float sweep = 1.5f * kPi;

        beginPath();
        fc(t.panel);
        circle(cx, cy, radius);
        fill();

        beginPath();
        sc(t.controlTrack);
        strokeWidth(6.0f);
        arc(cx, cy, radius - 6.0f, startAngle, startAngle + sweep, CW);
        stroke();

        // Long-ring end of the range. Red rather than amber so it stays
        // distinguishable from the value arc underneath it.
        beginPath();
        sc(t.danger);
        strokeWidth(6.0f);
        arc(cx, cy, radius - 6.0f, startAngle + sweep * 0.72f, startAngle + sweep, CW);
        stroke();

        beginPath();
        strokeColor(kStrikeAccent.r, kStrikeAccent.g, kStrikeAccent.b, 235);
        strokeWidth(6.0f);
        arc(cx, cy, radius - 6.0f, startAngle, startAngle + sweep * normalized, CW);
        stroke();

        const float needle = startAngle + sweep * normalized;
        beginPath();
        sc(t.textPrimary);
        strokeWidth(2.0f);
        moveTo(cx, cy);
        lineTo(cx + (radius - 12.0f) * std::cos(needle), cy + (radius - 12.0f) * std::sin(needle));
        stroke();

        const std::string shown = formatSeconds(seconds);
        fc(t.textPrimary);
        fontSize(17.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy + 18.0f, shown.c_str(), nullptr);

        fc(t.textDim);
        fontSize(10.0f);
        text(cx, cy + 34.0f, "RING TIME", nullptr);
    }

    void drawPresetPanel(const Rect bounds)
    {
        drawPanel(bounds, "PRESETS", kPresetAccent);

        const auto& t = theme();
        const float x = bounds.x + 12.0f;
        const float w = bounds.w - 24.0f;
        float y = bounds.y + 42.0f;

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "A preset writes all ten controls;", nullptr);
        text(x, y + 12.0f, "tweak any of them to hear Custom.", nullptr);
        y += 30.0f;

        // The whole list is drawn: eleven rows fit the panel, and a hidden menu
        // would make the range undiscoverable.
        const float rowH = clampf((bounds.y + bounds.h - 10.0f - y) / static_cast<float>(kPresets.size()),
                                  12.0f, 22.0f);
        const bool custom = presetIndex_ >= kPresets.size();

        for (std::size_t i = 0; i < kPresets.size(); ++i)
        {
            const Rect row {x, y + static_cast<float>(i) * rowH, w, rowH - 1.0f};
            const bool selected = presetIndex_ == i;

            if (selected || custom)
            {
                beginPath();
                fc(t.selection);
                roundedRect(row.x, row.y, row.w, row.h, laf::kRadiusSmall);
                fill();
            }
            if (selected)
            {
                beginPath();
                fillColor(kPresetAccent.r, kPresetAccent.g, kPresetAccent.b, 255);
                rect(row.x, row.y + 2.0f, 3.0f, row.h - 5.0f);
                fill();
            }

            fc(selected ? t.textPrimary : t.textDim);
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(row.x + 8.0f, row.y + row.h * 0.5f, kPresets[i].name, nullptr);

            // Name the modal table each preset lands on, so switching presets
            // never silently changes the model the dial is reporting on.
            const auto presetModel = static_cast<std::size_t>(
                std::clamp(static_cast<int>(std::lround(kPresets[i].values[0])), 0,
                           static_cast<int>(kInstrumentNames.size()) - 1));
            const bool sameName = std::strcmp(kPresets[i].name, kInstrumentNames[presetModel]) == 0;
            if (!sameName)
            {
                fc(t.textDisabled);
                fontSize(10.0f);
                textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
                text(row.x + row.w - 8.0f, row.y + row.h * 0.5f, kInstrumentNames[presetModel], nullptr);
            }

            if (presetCount_ < presetRects_.size())
                presetRects_[presetCount_++] = {i, row};
        }
    }

    // Partial ladder: every slot the current model can excite, at the ratio and
    // level the engine will actually use. Read straight from the shared
    // weighting so the picture cannot disagree with the sound.
    void drawPartialPanel(const Rect bounds)
    {
        drawPanel(bounds, "PARTIALS", kPartialAccent);

        const auto& t = theme();
        const float x = bounds.x + 12.0f;
        const float w = bounds.w - 24.0f;
        float y = bounds.y + 40.0f;

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Level against the fundamental, at the", nullptr);
        text(x, y + 12.0f, "stretch Mallet and Position give them.", nullptr);
        y += 32.0f;

        const ModeWeights weights = computeModeWeights(model(), value(ParamId::spread), value(ParamId::mallet),
                                                        value(ParamId::position));

        // Scale to the loudest partial so the ladder always fills the panel.
        float loudest = 0.0f;
        for (std::size_t i = 0; i < kModeCount; ++i)
            loudest = std::max(loudest, weights.weight[i]);
        if (loudest < 1.0e-6f)
            loudest = 1.0f;

        const float barX = x + 34.0f;
        const float barW = w - 34.0f;
        const float rowH = clampf((bounds.y + bounds.h - 46.0f - y) / static_cast<float>(kModeCount), 12.0f, 24.0f);

        for (std::size_t i = 0; i < kModeCount; ++i)
        {
            const Rect row {x, y + static_cast<float>(i) * rowH, w, rowH};
            const bool used = weights.ratio[i] > 0.0f;

            char label[16];
            if (used)
                std::snprintf(label, sizeof(label), "%.1fx", static_cast<double>(weights.ratio[i]));
            else
                std::snprintf(label, sizeof(label), "—");

            fc(used ? t.textDim : t.textDisabled);
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(row.x, row.y + row.h * 0.5f, label, nullptr);

            // An unexcited slot gets no groove at all: a dark empty track
            // reads as a loud bar, which is the opposite of the truth.
            if (!used)
                continue;

            beginPath();
            fc(t.controlTrack);
            roundedRect(barX, row.y + 3.0f, barW, rowH - 8.0f, 2.0f);
            fill();

            const float filled = clampf(weights.weight[i] / loudest, 0.0f, 1.0f);
            if (filled > 0.004f)
            {
                beginPath();
                fillColor(kPartialAccent.r, kPartialAccent.g, kPartialAccent.b, 255);
                roundedRect(barX, row.y + 3.0f, barW * filled, rowH - 8.0f, 2.0f);
                fill();
            }
        }

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, bounds.y + bounds.h - 21.0f, "Empty slots are modes this model does", nullptr);
        text(x, bounds.y + bounds.h - 9.0f, "not excite at these settings.", nullptr);
    }

    void drawPolyPanel(const Rect bounds)
    {
        drawPanel(bounds, "POLYPHONY", kPolyAccent);

        const auto& t = theme();
        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        float y = bounds.y + 44.0f;

        drawStepper(ParamId::voices, "Voices", {x, y, w, 38.0f}, voiceNameTable());
        y += 52.0f;

        // Voice pool: lit slots are the cap, the rest are dark. Stealing takes
        // the oldest, so the picture shows the size of the pool a chord draws on.
        beginPath();
        fc(t.controlTrack);
        roundedRect(x, y, w, 34.0f, laf::kRadiusSmall);
        fill();

        const float cap = static_cast<float>(std::clamp(intValue(ParamId::voices), 1, static_cast<int>(kMaxVoices)));
        const float slotW = w / static_cast<float>(kMaxVoices);
        for (int i = 0; i < static_cast<int>(kMaxVoices); ++i)
        {
            const bool lit = static_cast<float>(i) < cap;
            beginPath();
            if (lit)
                fillColor(kPolyAccent.r, kPolyAccent.g, kPolyAccent.b, 255);
            else
                fc(t.accentDim);
            roundedRect(x + static_cast<float>(i) * slotW + 2.0f, y + 8.0f, slotW - 4.0f, 18.0f, 2.0f);
            fill();
        }
        y += 42.0f;

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, y, "oldest note", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(x + w, y, "newest", nullptr);
        y += 28.0f;

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Over the cap the oldest voice gives way,", nullptr);
        text(x, y + 13.0f, "a releasing one first. A held note", nullptr);
        text(x, y + 26.0f, "re-struck restarts that voice.", nullptr);
        y += 50.0f;

        fc(t.textDim);
        text(x, y, "Velocity sets strike level.", nullptr);
        text(x, y + 13.0f, "CC 120 / 123 release everything.", nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 16> sliders_ {};
    std::array<Stepper, 4> steppers_ {};
    std::array<PresetRow, 16> presetRects_ {};
    // Small string storage, so the voice labels can outlive any single frame.
    std::array<std::array<char, 8>, kMaxVoices> voiceLabels_ {};
    std::array<const char*, kMaxVoices> voiceNames_ {};
    std::size_t sliderCount_ = 0;
    std::size_t stepperCount_ = 0;
    std::size_t presetCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    std::size_t presetIndex_ = 0;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MokaUI)
};

UI* createUI()
{
    return new MokaUI();
}

END_NAMESPACE_DISTRHO