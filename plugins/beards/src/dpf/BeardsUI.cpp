#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "beards_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::beards::kMidiChannelNames;
using downspout::beards::kParameterCount;
using downspout::beards::kParameterSpecs;
using downspout::beards::kQualityNames;
using downspout::beards::kSyncNames;
using downspout::beards::ParamId;

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

constexpr Accent kGrainAccent {176, 108, 72};   // copper
constexpr Accent kOutputAccent {92, 140, 156};  // steel
constexpr Accent kMidiAccent {124, 148, 104};   // olive

[[nodiscard]] float clampf(const float v, const float lo, const float hi) { return std::max(lo, std::min(v, hi)); }
[[nodiscard]] std::uint32_t idx(const ParamId id) { return static_cast<std::uint32_t>(id); }

[[nodiscard]] float normalizedValue(const std::uint32_t parameter, const float value)
{
    const auto& spec = kParameterSpecs[parameter];
    if (spec.maximum <= spec.minimum)
        return 0.0f;
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[64];
    switch (static_cast<ParamId>(parameter))
    {
    case ParamId::pitch:
        std::snprintf(buffer, sizeof(buffer), "%+.1f st", static_cast<double>(value));
        break;
    case ParamId::density:
        if (value < 0.48f)
            std::snprintf(buffer, sizeof(buffer), "%d%%  steady", static_cast<int>(std::lround(value * 100.0f)));
        else if (value > 0.52f)
            std::snprintf(buffer, sizeof(buffer), "%d%%  random", static_cast<int>(std::lround(value * 100.0f)));
        else
            std::snprintf(buffer, sizeof(buffer), "%d%%  silent", static_cast<int>(std::lround(value * 100.0f)));
        break;
    case ParamId::size:
        std::snprintf(buffer, sizeof(buffer), "%d%%  %s", static_cast<int>(std::lround(value * 100.0f)),
                      value < 0.3333f ? "reverse" : "forward");
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
        break;
    }
    return buffer;
}

// Mirrors GrainEnvelope::start()/render() in beards_engine.cpp for a unit-length grain.
float envelopeShape(float shape, const float x)
{
    const float sustain = std::max(1.0f - 3.0f * shape, 0.0f);
    shape = (shape - 0.33333333f) * 1.5f;
    if (shape < 0.0f)
        shape = 0.0f;
    else if (shape < 0.5f)
        shape = shape * shape * shape * 4.0f;
    const float attack = shape;
    const float release = (1.0f - attack) * (1.0f - sustain);
    const float total = std::max(attack + sustain + release, 1.0e-4f);
    const float smoothness = attack < 0.5f ? 2.0f * attack : 2.0f - 2.0f * attack;
    const float t = x * total;
    const auto smooth = [](const float u) { return u * u * (3.0f - 2.0f * u); };

    if (t < attack)
    {
        const float u = attack > 1.0e-4f ? t / attack : 1.0f;
        return u + (smooth(u) - u) * smoothness;
    }
    if (t < attack + sustain)
        return 1.0f;
    const float u = release > 1.0e-4f ? 1.0f - (t - attack - sustain) / release : 0.0f;
    const float base = (u * u) * (u * u);
    return base + (smooth(u) - base) * smoothness;
}

}  // namespace

class BeardsUI : public UI {
public:
    BeardsUI()
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
        sliderCount_ = 0;
        stepperCount_ = 0;
        buttonCount_ = 0;

        drawBackground(width);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});

        drawGrainPanel({24.0f, 108.0f, 540.0f, 304.0f});
        drawOutputPanel({580.0f, 108.0f, width - 580.0f - 24.0f, 304.0f});
        drawMidiPanel({24.0f, 428.0f, width - 48.0f, static_cast<float>(getHeight()) - 428.0f - 24.0f});
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press)
        {
            activeSlider_ = -1;
            if (grainHeld_)
            {
                grainHeld_ = false;
                commit(idx(ParamId::grain), 0.0f);
            }
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

        for (std::size_t i = 0; i < buttonCount_; ++i)
        {
            if (!buttons_[i].bounds.contains(mx, my))
                continue;
            if (buttons_[i].parameter == idx(ParamId::grain))
            {
                grainHeld_ = true;
                commit(idx(ParamId::grain), 1.0f);
            }
            else
            {
                commit(buttons_[i].parameter, values_[buttons_[i].parameter] >= 0.5f ? 0.0f : 1.0f);
            }
            return true;
        }

        for (std::size_t i = 0; i < stepperCount_; ++i)
        {
            const Stepper& stepper = steppers_[i];
            if (!stepper.bounds.contains(mx, my))
                continue;
            step(stepper.parameter, (mx < stepper.bounds.x + stepper.bounds.w * 0.5f) ? -1 : 1);
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
    struct Control {
        std::uint32_t parameter = 0;
        Rect bounds {};
    };
    using Slider = Control;
    using Stepper = Control;
    using Button = Control;

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    [[nodiscard]] float value(const ParamId id) const { return values_[idx(id)]; }

    void commit(const std::uint32_t parameter, const float raw)
    {
        const auto& spec = kParameterSpecs[parameter];
        float v = clampf(raw, spec.minimum, spec.maximum);
        if (spec.integer)
            v = std::round(v);
        values_[parameter] = v;
        setParameterValue(parameter, v);
        repaint();
    }

    void step(const std::uint32_t parameter, const int direction)
    {
        const auto& spec = kParameterSpecs[parameter];
        float next = values_[parameter] + static_cast<float>(direction);
        const float span = spec.maximum - spec.minimum + 1.0f;
        if (next < spec.minimum)
            next += span;
        if (next > spec.maximum)
            next -= span;
        commit(parameter, next);
    }

    void setFromMouse(const Slider& slider, const float mouseX)
    {
        const auto& spec = kParameterSpecs[slider.parameter];
        const float n = clampf((mouseX - slider.bounds.x) / std::max(1.0f, slider.bounds.w), 0.0f, 1.0f);
        commit(slider.parameter, spec.minimum + n * (spec.maximum - spec.minimum));
    }

    // ── Primitives ─────────────────────────────────────────────────────────

    void drawBackground(const float width)
    {
        const auto& t = theme();
        beginPath();
        fc(t.background);
        rect(0.0f, 0.0f, width, static_cast<float>(getHeight()));
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
        text(bounds.x, bounds.y, "Beards", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(bounds.x + 122.0f, bounds.y + 10.0f, "granular texture processor", nullptr);

        fontSize(12.0f);
        text(bounds.x, bounds.y + 44.0f,
             "Stereo audio in is recorded into a 4 second buffer; up to 32 grains replay it. After Mutable Instruments Beads.",
             nullptr);

        // Live grain count, from the processor.
        const int grains = static_cast<int>(std::lround(value(ParamId::outGrains)));
        const float lampX = bounds.x + bounds.w - 250.0f;
        beginPath();
        circle(lampX, bounds.y + 18.0f, 7.0f);
        if (grains > 0)
            fillColor(kGrainAccent.r, kGrainAccent.g, kGrainAccent.b, 255);
        else
            fc(t.accentDim);
        fill();

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "GRAINS  %d", grains);
        fc(grains > 0 ? t.textPrimary : t.textDisabled);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, bounds.y + 18.0f, buffer, nullptr);

        // Freeze indicator.
        const bool frozen = value(ParamId::freeze) >= 0.5f;
        fc(frozen ? t.warning : t.textDisabled);
        text(lampX + 100.0f, bounds.y + 18.0f, frozen ? "FROZEN" : "live", nullptr);

        themeRect_ = {bounds.x + bounds.w - 74.0f, bounds.y + 4.0f, 74.0f, 26.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall);
        fill();
        fc(t.textDim);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f, darkTheme_ ? "DARK" : "LIGHT",
             nullptr);
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

        if (sliderCount_ < sliders_.size())
            sliders_[sliderCount_++] = {parameter, {bounds.x, trackY - 9.0f, bounds.w, 28.0f}};
    }

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

        const auto& spec = kParameterSpecs[parameter];
        const int selection = std::clamp(static_cast<int>(std::lround(values_[parameter])),
                                         static_cast<int>(spec.minimum), static_cast<int>(spec.maximum));
        fc(t.textPrimary);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, names[selection], nullptr);

        fc(t.textDim);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);

        if (stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {parameter, box};
    }

    void drawButton(const ParamId id, const char* label, const Rect bounds, const Accent accent)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);
        const bool on = values_[parameter] >= 0.5f;

        beginPath();
        if (on)
            fillColor(accent.r, accent.g, accent.b, 255);
        else
            fc(t.buttonFace);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusSmall);
        stroke();

        if (on)
            fillColor(250, 248, 242, 255);
        else
            fc(t.textPrimary);
        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(bounds.x + bounds.w * 0.5f, bounds.y + bounds.h * 0.5f, label, nullptr);

        if (buttonCount_ < buttons_.size())
            buttons_[buttonCount_++] = {parameter, bounds};
    }

    // ── Sections ───────────────────────────────────────────────────────────

    void drawGrainPanel(const Rect bounds)
    {
        drawPanel(bounds, "GRAINS — what is played from the buffer", kGrainAccent);

        const auto& t = theme();
        const float colW = 240.0f;
        const float left = bounds.x + 16.0f;
        const float right = left + colW + 28.0f;
        const float top = bounds.y + 44.0f;
        const float rowH = 44.0f;

        drawSlider(ParamId::position, "Position  (how far back)", {left, top, colW, rowH}, kGrainAccent);
        drawSlider(ParamId::size, "Size  (grain length)", {left, top + rowH, colW, rowH}, kGrainAccent);
        drawSlider(ParamId::shape, "Shape  (envelope)", {left, top + 2.0f * rowH, colW, rowH}, kGrainAccent);
        drawSlider(ParamId::pitch, "Pitch", {left, top + 3.0f * rowH, colW, rowH}, kGrainAccent);
        drawSlider(ParamId::density, "Density  (grain rate)", {left, top + 4.0f * rowH, colW, rowH}, kGrainAccent);

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(left, top + 5.0f * rowH - 6.0f, "Below 48% a steady clock, above 52% random clouds.", nullptr);

        drawEnvelope({right, top - 2.0f, colW, 44.0f});

        const float scatterTop = top + 52.0f;
        const float scatterRow = 36.0f;
        drawSlider(ParamId::timeRand, "Position scatter", {right, scatterTop, colW, scatterRow}, kGrainAccent);
        drawSlider(ParamId::sizeRand, "Size scatter", {right, scatterTop + scatterRow, colW, scatterRow}, kGrainAccent);
        drawSlider(ParamId::shapeRand, "Shape scatter", {right, scatterTop + 2.0f * scatterRow, colW, scatterRow},
                   kGrainAccent);
        drawSlider(ParamId::pitchRand, "Pitch scatter", {right, scatterTop + 3.0f * scatterRow, colW, scatterRow},
                   kGrainAccent);
        drawStepper(ParamId::sync, "Transport sync  (grain per beat division)",
                    {right, scatterTop + 4.0f * scatterRow + 4.0f, colW, 44.0f}, kSyncNames.data());
    }

    // The grain envelope for the current Shape, so the control reads as a curve.
    void drawEnvelope(const Rect bounds)
    {
        const auto& t = theme();
        beginPath();
        fc(t.panel);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusSmall);
        fill();

        const float shape = value(ParamId::shape);
        const float pad = 6.0f;
        const float x0 = bounds.x + pad;
        const float w = bounds.w - 2.0f * pad;
        const float y0 = bounds.y + bounds.h - pad - 10.0f;
        const float h = bounds.h - 2.0f * pad - 12.0f;

        beginPath();
        strokeColor(kGrainAccent.r, kGrainAccent.g, kGrainAccent.b, 255);
        strokeWidth(2.0f);
        const int steps = 64;
        for (int i = 0; i <= steps; ++i)
        {
            const float x = static_cast<float>(i) / static_cast<float>(steps);
            const float y = envelopeShape(shape, x);
            if (i == 0)
                moveTo(x0 + x * w, y0 - y * h);
            else
                lineTo(x0 + x * w, y0 - y * h);
        }
        stroke();

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(bounds.x + pad, bounds.y + bounds.h - 2.0f, "grain envelope", nullptr);
    }

    void drawOutputPanel(const Rect bounds)
    {
        drawPanel(bounds, "OUTPUT — mix and recirculation", kOutputAccent);

        const float x = bounds.x + 16.0f;
        const float w = bounds.w - 32.0f;
        const float rowH = 44.0f;
        float y = bounds.y + 44.0f;

        drawSlider(ParamId::feedback, "Feedback  (wet back into buffer)", {x, y, w, rowH}, kOutputAccent);
        y += rowH;
        drawSlider(ParamId::dryWet, "Dry / Wet", {x, y, w, rowH}, kOutputAccent);
        y += rowH;
        drawSlider(ParamId::reverb, "Reverb", {x, y, w, rowH}, kOutputAccent);
        y += rowH;
        drawStepper(ParamId::quality, "Quality  (storage fidelity)", {x, y, w, rowH}, kQualityNames.data());
        y += rowH + 6.0f;

        const float half = (w - 10.0f) * 0.5f;
        drawButton(ParamId::freeze, "FREEZE", {x, y, half, 34.0f}, kOutputAccent);
        drawButton(ParamId::grain, "GRAIN", {x + half + 10.0f, y, half, 34.0f}, kGrainAccent);
    }

    void drawMidiPanel(const Rect bounds)
    {
        drawPanel(bounds, "CONTROL — Drift and Conductor", kMidiAccent);

        const auto& t = theme();
        const float top = bounds.y + 44.0f;
        const float colW = 190.0f;
        const float x = bounds.x + 16.0f;

        drawStepper(ParamId::midiCh, "MIDI in  (Drift / controllers)", {x, top, colW, 44.0f}, kMidiChannelNames.data());
        drawStepper(ParamId::conductorCh, "Conductor channel", {x, top + 52.0f, colW, 44.0f}, kMidiChannelNames.data());

        const float tx = x + colW + 36.0f;
        fc(t.textPrimary);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(tx, top, "MIDI in", nullptr);
        text(tx + 380.0f, top, "Conductor", nullptr);

        fc(t.textDim);
        fontSize(11.0f);
        text(tx, top + 16.0f, "CC 1 Position   2 Size   3 Density   4 Dry/Wet", nullptr);
        text(tx, top + 31.0f, "CC 5 Shape   6 Pitch   7 Feedback   8 Reverb", nullptr);
        text(tx, top + 46.0f, "CC 9-12 Pitch, Position, Size, Shape scatter", nullptr);
        text(tx, top + 61.0f, "CC 64 Freeze (63 or less live, 64 or more frozen)", nullptr);
        text(tx, top + 80.0f, "Drift's lanes default to CC 1-4: no setup needed.", nullptr);

        text(tx + 380.0f, top + 16.0f, "CC 21 Density", nullptr);
        text(tx + 380.0f, top + 31.0f, "CC 22 Energy to Dry/Wet", nullptr);
        text(tx + 380.0f, top + 46.0f, "CC 23 Mutation to scatter", nullptr);
        text(tx + 380.0f, top + 61.0f, "CC 24 = 127 fires a grain", nullptr);
        text(tx + 380.0f, top + 80.0f, "The panel does not follow CC changes.", nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 16> sliders_ {};
    std::array<Stepper, 4> steppers_ {};
    std::array<Button, 4> buttons_ {};
    std::size_t sliderCount_ = 0;
    std::size_t stepperCount_ = 0;
    std::size_t buttonCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    bool grainHeld_ = false;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeardsUI)
};

UI* createUI()
{
    return new BeardsUI();
}

END_NAMESPACE_DISTRHO
