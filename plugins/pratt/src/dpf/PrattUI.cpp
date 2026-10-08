#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "pratt_params.hpp"
#include "pratt_poly.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;
namespace core = downspout::pratt;

namespace {

using core::ParamId;
using core::kParameterCount;
using core::kParameterSpecs;

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

constexpr Accent kVoiceAccent {176, 112, 62};   // copper
constexpr Accent kFilterAccent {92, 140, 156};  // cold steel
constexpr Accent kModeAccent {198, 132, 58};    // amber

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
    if (spec.logarithmic && spec.minimum > 0.0f)
        return clampf(std::log(std::max(value, spec.minimum) / spec.minimum)
                          / std::log(spec.maximum / spec.minimum), 0.0f, 1.0f);
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

[[nodiscard]] float denormalizedValue(const std::uint32_t parameter, const float normalized)
{
    const auto& spec = kParameterSpecs[parameter];
    if (spec.logarithmic && spec.minimum > 0.0f)
        return spec.minimum * std::pow(spec.maximum / spec.minimum, normalized);
    return spec.minimum + normalized * (spec.maximum - spec.minimum);
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[64];
    switch (static_cast<ParamId>(parameter))
    {
    case ParamId::base:
        if (value < 0.5f)
            std::snprintf(buffer, sizeof(buffer), "voice default");
        else
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    case ParamId::timbre:
        if (value < 1.5f)
            std::snprintf(buffer, sizeof(buffer), "off");
        else
            std::snprintf(buffer, sizeof(buffer), "%d  (order %d)", static_cast<int>(std::lround(value)),
                          core::degree(static_cast<int>(std::lround(value))));
        break;
    case ParamId::brightness:
        std::snprintf(buffer, sizeof(buffer), "x%.2f", static_cast<double>(value));
        break;
    case ParamId::darkness:
        std::snprintf(buffer, sizeof(buffer), "%+.2f", static_cast<double>(value));
        break;
    case ParamId::bendRange:
        std::snprintf(buffer, sizeof(buffer), "%d st", static_cast<int>(std::lround(value)));
        break;
    case ParamId::filterA:
    case ParamId::filterB:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    case ParamId::cutoff:
        if (value >= 1000.0f)
            std::snprintf(buffer, sizeof(buffer), "%.2f kHz", static_cast<double>(value) / 1000.0);
        else
            std::snprintf(buffer, sizeof(buffer), "%d Hz", static_cast<int>(std::lround(value)));
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
        break;
    }
    return buffer;
}

}  // namespace

class PrattUI : public UI {
public:
    PrattUI()
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

        sliderCount_ = 0;
        stepperCount_ = 0;
        modeCount_ = 0;

        drawBackground(width, height);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});
        drawModeStrip({24.0f, 108.0f, width - 48.0f, 44.0f});

        const float panelY = 164.0f;
        const float panelH = height - panelY - 24.0f;
        const float gap = 16.0f;
        const float voiceW = (width - 48.0f - gap) * 0.5f;
        drawVoicePanel({24.0f, panelY, voiceW, panelH});
        drawFilterPanel({24.0f + voiceW + gap, panelY, width - 48.0f - gap - voiceW, panelH});
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

        for (std::size_t i = 0; i < modeCount_; ++i)
        {
            if (modeRects_[i].bounds.contains(mx, my))
            {
                commit(idx(ParamId::mode), static_cast<float>(modeRects_[i].value));
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

    struct ModeButton {
        int value = 0;
        Rect bounds {};
    };

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    [[nodiscard]] float value(const ParamId id) const { return values_[idx(id)]; }
    [[nodiscard]] int intValue(const ParamId id) const { return static_cast<int>(std::lround(values_[idx(id)])); }
    [[nodiscard]] bool voiceActive() const { return intValue(ParamId::mode) != 1; }
    [[nodiscard]] bool filterActive() const { return intValue(ParamId::mode) != 0; }

    [[nodiscard]] int filterIndex() const
    {
        const long long n = static_cast<long long>(intValue(ParamId::filterA)) * intValue(ParamId::filterB);
        return static_cast<int>(std::clamp<long long>(n, 1, core::kMaxIndex));
    }

    void commit(const std::uint32_t parameter, const float raw)
    {
        const float v = core::clampParameter(parameter, raw);
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
        const float normalized = clampf((mouseX - slider.bounds.x) / std::max(1.0f, slider.bounds.w), 0.0f, 1.0f);
        commit(slider.parameter, denormalizedValue(slider.parameter, normalized));
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
        text(bounds.x, bounds.y, "Pratt", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(bounds.x + 92.0f, bounds.y + 10.0f, "number-theoretic synth and filter", nullptr);

        fc(t.textDim);
        fontSize(12.0f);
        text(bounds.x, bounds.y + 44.0f,
             "H_n(s) = n / f_n(2 + s/w0): a stable all-pole filter whose shape is set by the factors of n.", nullptr);

        // Voice lamp.
        const int voices = static_cast<int>(std::lround(value(ParamId::outVoices)));
        const float lampX = bounds.x + bounds.w - 188.0f;
        beginPath();
        circle(lampX, bounds.y + 18.0f, 7.0f);
        if (voices > 0)
            fillColor(kModeAccent.r, kModeAccent.g, kModeAccent.b, 255);
        else
            fc(t.accentDim);
        fill();

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%d VOICE%s", voices, voices == 1 ? "" : "S");
        fc(voices > 0 ? t.textPrimary : t.textDisabled);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, bounds.y + 18.0f, buffer, nullptr);

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

    void drawModeStrip(const Rect bounds)
    {
        const auto& t = theme();
        const int selected = intValue(ParamId::mode);
        const float buttonW = 168.0f;

        for (int i = 0; i < 3; ++i)
        {
            const Rect box {bounds.x + static_cast<float>(i) * (buttonW + 8.0f), bounds.y, buttonW, bounds.h};
            beginPath();
            if (i == selected)
                fillColor(kModeAccent.r, kModeAccent.g, kModeAccent.b, 255);
            else
                fc(t.buttonFace);
            roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
            fill();
            if (i != selected)
            {
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
            fontSize(14.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, core::kModeNames[static_cast<std::size_t>(i)], nullptr);
            if (modeCount_ < modeRects_.size())
                modeRects_[modeCount_++] = {i, box};
        }

        static constexpr const char* kFlow[3] = {
            "MIDI -> Pratt voices -> room -> out.  Audio input is ignored.",
            "audio in -> Pratt filter -> out.  MIDI is ignored.",
            "MIDI -> Pratt voices -> room, plus audio in -> Pratt filter -> out.",
        };
        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(bounds.x + 3.0f * (buttonW + 8.0f) + 12.0f, bounds.y + bounds.h * 0.5f, kFlow[selected], nullptr);
    }

    void drawPanel(const Rect bounds, const char* title, const Accent accent, const bool enabled)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusPanel);
        fill();

        // Inactive panels get a pre-blended (opaque) header so the two
        // overlapping fills below cannot show a seam.
        const auto blend = [&](const int channel, const int surfaceChannel) {
            return enabled ? channel : (channel * 45 + surfaceChannel * 55) / 100;
        };
        const int hr = blend(accent.r, t.surface.r);
        const int hg = blend(accent.g, t.surface.g);
        const int hb = blend(accent.b, t.surface.b);
        beginPath();
        fillColor(hr, hg, hb, 255);
        roundedRect(bounds.x, bounds.y, bounds.w, 32.0f, laf::kRadiusPanel);
        fill();
        beginPath();
        fillColor(hr, hg, hb, 255);
        rect(bounds.x, bounds.y + 20.0f, bounds.w, 12.0f);
        fill();

        fillColor(250, 248, 242, 255);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(bounds.x + 12.0f, bounds.y + 16.0f, title, nullptr);
        if (!enabled)
        {
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fontSize(11.0f);
            text(bounds.x + bounds.w - 12.0f, bounds.y + 16.0f, "inactive in this mode", nullptr);
        }
    }

    void drawSlider(const ParamId id, const char* label, const Rect bounds, const Accent accent,
                    const bool enabled)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);
        const float normalized = normalizedValue(parameter, values_[parameter]);

        fc(enabled ? t.textDim : t.textDisabled);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const std::string shown = formatValue(parameter, values_[parameter]);
        fc(enabled ? t.textPrimary : t.textDisabled);
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
            if (enabled)
                fillColor(accent.r, accent.g, accent.b, 255);
            else
                fc(t.textDisabled);
            roundedRect(bounds.x, trackY, bounds.w * normalized, 10.0f, 3.0f);
            fill();
        }

        // Always interactive: setting up the inactive section ahead of a mode
        // switch is legitimate, so dimming is a hint, not a lock.
        remember(parameter, {bounds.x, trackY - 9.0f, bounds.w, 28.0f});
    }

    void drawStepper(const ParamId id, const char* label, const Rect bounds, const char* const* names,
                     const bool enabled)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);

        fc(enabled ? t.textDim : t.textDisabled);
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

        const int selection = static_cast<int>(std::lround(values_[parameter]));
        fc(enabled ? t.textPrimary : t.textDisabled);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, names[selection], nullptr);

        fc(enabled ? t.textDim : t.textDisabled);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);

        if (stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {parameter, box};
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

    void caption(const float x, const float y, const char* label, const bool right = false)
    {
        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign((right ? ALIGN_RIGHT : ALIGN_LEFT) | ALIGN_TOP);
        text(x, y, label, nullptr);
    }

    // ── Sections ───────────────────────────────────────────────────────────

    void drawVoicePanel(const Rect bounds)
    {
        const bool on = voiceActive();
        drawPanel(bounds, "VOICE  -  wavetable synthesizer", kVoiceAccent, on);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        float y = bounds.y + 42.0f;

        drawStepper(ParamId::preset, "Voice  (GM Program follows each channel's program change)",
                    {x, y, w, 40.0f}, core::kVoiceNames.data(), on);
        y += 40.0f;
        drawSlider(ParamId::base, "Pratt base  (index n = (note+1) x base)", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        drawSlider(ParamId::timbre, "Timbre index  (extra fixed filter chain, same for every note)", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        drawSlider(ParamId::brightness, "Brightness  (filter reaches higher partials)", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        drawSlider(ParamId::darkness, "Rolloff offset  (positive = darker)", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        drawSlider(ParamId::bendRange, "Pitch bend range", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        drawSlider(ParamId::room, "Room", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        drawSlider(ParamId::level, "Level  (soft-clipped)", {x, y, w, 34.0f}, kVoiceAccent, on);
        y += 34.0f;
        static constexpr const char* kOffOn[2] = {"Off", "On"};
        drawStepper(ParamId::percussion, "Drums on MIDI channel 10", {x, y, w, 40.0f}, kOffOn, on);
        y += 44.0f;

        drawSpectrum({x, y, w, bounds.y + bounds.h - y - 14.0f}, on);
    }

    // Harmonic weights of the reference note C4 under the current settings.
    void drawSpectrum(const Rect bounds, const bool enabled)
    {
        const auto& t = theme();
        if (bounds.h < 40.0f)
            return;

        const int preset = std::max(1, intValue(ParamId::preset)) - 1;  // GM Program shows the piano
        const core::Preset& p = core::kPresets[static_cast<std::size_t>(preset)];
        const int base = intValue(ParamId::base) > 0 ? intValue(ParamId::base) : p.base;
        const int n = (60 + 1) * base;
        const int timbre = intValue(ParamId::timbre);
        const double xi = p.xi * value(ParamId::brightness) * (1.20 - 0.30 * 0.5);
        const double roll = p.roll + value(ParamId::darkness);

        constexpr int kHarmonics = 32;
        std::array<double, kHarmonics> amp {};
        double peak = 1.0e-9;
        for (int k = 1; k <= kHarmonics; ++k)
        {
            double a = std::pow(static_cast<double>(k), -roll);
            if (k % 2 == 0)
                a *= p.evenGain;
            if (p.upperStart > 0 && k > p.upperStart)
                a *= p.upperGain;
            amp[static_cast<std::size_t>(k - 1)] = a * std::abs(core::response(n, xi * k)) * std::abs(core::response(timbre, xi * k));
            peak = std::max(peak, amp[static_cast<std::size_t>(k - 1)]);
        }

        const Rect plot {bounds.x, bounds.y + 14.0f, bounds.w, bounds.h - 14.0f};
        char label[96];
        if (timbre > 1)
            std::snprintf(label, sizeof(label), "Harmonics of middle C:  n = 61 x %d = %d, timbre %d  (%s)", base, n,
                          timbre, core::kPresets[static_cast<std::size_t>(preset)].name);
        else
            std::snprintf(label, sizeof(label), "Harmonics of middle C:  n = 61 x %d = %d  (%s)", base, n,
                          core::kPresets[static_cast<std::size_t>(preset)].name);
        caption(bounds.x, bounds.y, label);
        caption(bounds.x + bounds.w, bounds.y, "partials 1-32, low to high", true);
        drawPlotFrame(plot);

        const float barW = (plot.w - 12.0f) / static_cast<float>(kHarmonics);
        for (int k = 0; k < kHarmonics; ++k)
        {
            const float level = static_cast<float>(amp[static_cast<std::size_t>(k)] / peak);
            const float h = (plot.h - 10.0f) * level;
            beginPath();
            if (enabled)
                fillColor(kVoiceAccent.r, kVoiceAccent.g, kVoiceAccent.b, 255);
            else
                fc(t.textDisabled);
            rect(plot.x + 6.0f + static_cast<float>(k) * barW + 1.0f, plot.y + plot.h - 5.0f - h,
                 std::max(1.0f, barW - 2.0f), h);
            fill();
        }
    }

    void drawFilterPanel(const Rect bounds)
    {
        const bool on = filterActive();
        drawPanel(bounds, "FILTER  -  Pratt all-pole", kFilterAccent, on);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::filterA, "Index A", {x, y, w, 36.0f}, kFilterAccent, on);
        y += 36.0f;
        drawSlider(ParamId::filterB, "Index B  (H_A x H_B = H_AB: the filters cascade)", {x, y, w, 36.0f}, kFilterAccent, on);
        y += 36.0f;

        const int n = filterIndex();
        const auto& t = theme();
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "n = %d x %d = %d     order %d", intValue(ParamId::filterA),
                      intValue(ParamId::filterB), n, core::degree(n));
        fc(on ? t.textPrimary : t.textDisabled);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y + 2.0f, buffer, nullptr);
        if (intValue(ParamId::filterA) * intValue(ParamId::filterB) > core::kMaxIndex)
            caption(x + w, y + 5.0f, "capped at 8192", true);
        y += 28.0f;

        drawSlider(ParamId::cutoff, "Cutoff  (w0)", {x, y, w, 36.0f}, kFilterAccent, on);
        y += 36.0f;
        drawSlider(ParamId::mix, "Dry / wet", {x, y, w, 36.0f}, kFilterAccent, on);
        y += 44.0f;

        const float plotH = bounds.y + bounds.h - y - 14.0f;
        const float poleW = 124.0f;
        drawResponse({x, y, w - poleW - 12.0f, plotH}, on, n);
        drawPoles({x + w - poleW, y, poleW, plotH}, on, n);
    }

    void drawResponse(const Rect bounds, const bool enabled, const int n)
    {
        const auto& t = theme();
        if (bounds.h < 40.0f)
            return;
        caption(bounds.x, bounds.y, "Magnitude response (dB)");
        const Rect plot {bounds.x, bounds.y + 14.0f, bounds.w, bounds.h - 28.0f};
        drawPlotFrame(plot);

        constexpr float kMinHz = 20.0f, kMaxHz = 20000.0f, kTop = 6.0f, kBottom = -60.0f;
        const auto xOf = [&](const float hz) {
            return plot.x + plot.w * std::log(hz / kMinHz) / std::log(kMaxHz / kMinHz);
        };
        const auto yOf = [&](const float db) {
            return plot.y + plot.h * (kTop - clampf(db, kBottom, kTop)) / (kTop - kBottom);
        };

        for (const float db : {0.0f, -24.0f, -48.0f})
        {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(plot.x, yOf(db));
            lineTo(plot.x + plot.w, yOf(db));
            stroke();
        }
        for (const float hz : {100.0f, 1000.0f, 10000.0f})
        {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(xOf(hz), plot.y);
            lineTo(xOf(hz), plot.y + plot.h);
            stroke();
        }

        const float cutoff = value(ParamId::cutoff);
        const float mix = value(ParamId::mix);
        beginPath();
        constexpr int kPoints = 160;
        for (int i = 0; i <= kPoints; ++i)
        {
            const float hz = kMinHz * std::pow(kMaxHz / kMinHz, static_cast<float>(i) / kPoints);
            const std::complex<double> h = core::response(n, static_cast<double>(hz / cutoff));
            const std::complex<double> mixed = (1.0 - mix) + static_cast<double>(mix) * h;
            const float db = 20.0f * std::log10(std::max(1.0e-6f, static_cast<float>(std::abs(mixed))));
            if (i == 0)
                moveTo(xOf(hz), yOf(db));
            else
                lineTo(xOf(hz), yOf(db));
        }
        if (enabled)
            strokeColor(kFilterAccent.r, kFilterAccent.g, kFilterAccent.b, 255);
        else
            sc(t.textDisabled);
        strokeWidth(2.0f);
        stroke();

        // Cutoff marker.
        beginPath();
        sc(t.textDim);
        strokeWidth(1.0f);
        moveTo(xOf(cutoff), plot.y);
        lineTo(xOf(cutoff), plot.y + plot.h);
        stroke();

        fc(t.textDim);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        const float labelY = plot.y + plot.h + 3.0f;
        text(xOf(100.0f), labelY, "100", nullptr);
        text(xOf(1000.0f), labelY, "1k", nullptr);
        text(xOf(10000.0f), labelY, "10k", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(plot.x + 3.0f, yOf(0.0f) + 1.0f, "0 dB", nullptr);
        text(plot.x + 3.0f, yOf(-24.0f) + 1.0f, "-24", nullptr);
        text(plot.x + 3.0f, yOf(-48.0f) + 1.0f, "-48", nullptr);
    }

    // Roots of f_n shifted to the filter's poles, in units of w0.
    void drawPoles(const Rect bounds, const bool enabled, const int n)
    {
        const auto& t = theme();
        if (bounds.h < 40.0f)
            return;
        caption(bounds.x, bounds.y, "Poles (s / w0)");
        const Rect plot {bounds.x, bounds.y + 14.0f, bounds.w, bounds.h - 28.0f};
        drawPlotFrame(plot);

        std::vector<std::complex<double>> poles;
        double radius = 1.0;
        for (const auto& root : core::roots(n))
        {
            poles.push_back(root - 2.0);
            radius = std::max(radius, std::abs(poles.back()));
        }
        radius *= 1.1;

        // Real axis left of the imaginary axis; the stable half plane.
        const float cx = plot.x + plot.w * 0.84f;
        const float cy = plot.y + plot.h * 0.5f;
        const float sx = (cx - plot.x - 4.0f) / static_cast<float>(radius);
        const float sy = (plot.h * 0.5f - 4.0f) / static_cast<float>(radius);
        const float scale = std::min(sx, sy);

        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(plot.x + 2.0f, cy);
        lineTo(plot.x + plot.w - 2.0f, cy);
        moveTo(cx, plot.y + 2.0f);
        lineTo(cx, plot.y + plot.h - 2.0f);
        stroke();

        for (const auto& pole : poles)
        {
            beginPath();
            if (enabled)
                fillColor(kFilterAccent.r, kFilterAccent.g, kFilterAccent.b, 255);
            else
                fc(t.textDisabled);
            circle(cx + static_cast<float>(pole.real()) * scale, cy - static_cast<float>(pole.imag()) * scale, 3.0f);
            fill();
        }

        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%zu poles, all stable", poles.size());
        fc(t.textDim);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(plot.x + plot.w * 0.5f, plot.y + plot.h + 3.0f, buffer, nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 16> sliders_ {};
    std::array<Stepper, 4> steppers_ {};
    std::array<ModeButton, 3> modeRects_ {};
    std::size_t sliderCount_ = 0;
    std::size_t stepperCount_ = 0;
    std::size_t modeCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrattUI)
};

UI* createUI()
{
    return new PrattUI();
}

END_NAMESPACE_DISTRHO
