#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "magneto_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::magneto::kListenNames;
using downspout::magneto::kMidiChannelNames;
using downspout::magneto::kParameterCount;
using downspout::magneto::kParameterSpecs;
using downspout::magneto::kRpmSourceNames;
using downspout::magneto::kSyncRatioNames;
using downspout::magneto::ParamId;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSpeedOfSound = 343.0f;

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

constexpr Accent kDriveAccent {198, 132, 58};    // amber
constexpr Accent kEngineAccent {176, 88, 62};    // hot metal
constexpr Accent kPipeAccent {92, 140, 156};     // cold steel
constexpr Accent kListenAccent {124, 148, 104};  // olive

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

// Tube fundamental, so a length in metres reads as something audible.
[[nodiscard]] int tubeResonanceHz(const float metres)
{
    return static_cast<int>(std::lround(kSpeedOfSound / (2.0f * std::max(metres, 0.01f))));
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[64];
    const auto id = static_cast<ParamId>(parameter);

    switch (id)
    {
    case ParamId::cylinders:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    case ParamId::displacement:
        std::snprintf(buffer, sizeof(buffer), "%d cc", static_cast<int>(std::lround(value)));
        break;
    case ParamId::compression:
        std::snprintf(buffer, sizeof(buffer), "%.1f:1", static_cast<double>(value));
        break;
    case ParamId::intakeLen:
    case ParamId::extractorLen:
    case ParamId::pipeLen:
    case ParamId::mufflerLen:
    case ParamId::outletLen:
        std::snprintf(buffer, sizeof(buffer), "%.2f m  %d Hz", static_cast<double>(value),
                      tubeResonanceHz(value));
        break;
    case ParamId::rpm:
    case ParamId::idleRpm:
    case ParamId::outRpm:
        std::snprintf(buffer, sizeof(buffer), "%d rpm", static_cast<int>(std::lround(value)));
        break;
    case ParamId::inertia:
        if (value < 1000.0f)
            std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(value)));
        else
            std::snprintf(buffer, sizeof(buffer), "%.1f s", static_cast<double>(value) / 1000.0);
        break;
    case ParamId::seed:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%d%%",
                      static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
        break;
    }
    return buffer;
}

}  // namespace

class MagnetoUI : public UI {
public:
    MagnetoUI()
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
        positionCount_ = 0;

        drawBackground(width, height);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});

        drawDriveStrip({24.0f, 108.0f, width - 48.0f, 172.0f});

        const float columnW = (width - 48.0f - 32.0f) / 3.0f;
        const float columnY = 296.0f;
        const float columnH = height - columnY - 24.0f;
        drawEnginePanel({24.0f, columnY, columnW, columnH});
        drawExhaustPanel({24.0f + columnW + 16.0f, columnY, columnW, columnH});
        drawListeningPanel({24.0f + 2.0f * (columnW + 16.0f), columnY, columnW, columnH});
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

        for (std::size_t i = 0; i < positionCount_; ++i)
        {
            if (positionRects_[i].bounds.contains(mx, my))
            {
                commit(idx(ParamId::listen), static_cast<float>(positionRects_[i].value));
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

    struct PositionDot {
        int value = 0;
        Rect bounds {};
    };

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    [[nodiscard]] float value(const ParamId id) const { return values_[idx(id)]; }
    [[nodiscard]] int intValue(const ParamId id) const { return static_cast<int>(std::lround(values_[idx(id)])); }
    [[nodiscard]] bool syncing() const { return intValue(ParamId::rpmSource) == 1; }

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
        text(bounds.x, bounds.y, "Magneto", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(bounds.x + 132.0f, bounds.y + 10.0f, "combustion engine generator", nullptr);

        fc(t.textDim);
        fontSize(12.0f);
        text(bounds.x, bounds.y + 44.0f,
             "four-stroke cycle, waveguide cylinders, intake runners, extractors, muffler, tailpipe",
             nullptr);

        // Backfire lamp.
        const float lamp = clampf(value(ParamId::outBackfire), 0.0f, 1.0f);
        const float lampX = bounds.x + bounds.w - 188.0f;
        beginPath();
        circle(lampX, bounds.y + 18.0f, 7.0f);
        if (lamp > 0.02f)
            fillColor(t.danger.r, t.danger.g, t.danger.b, static_cast<uchar>(80 + 175 * lamp));
        else
            fc(t.accentDim);
        fill();

        fc(lamp > 0.02f ? t.danger : t.textDisabled);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, bounds.y + 18.0f, "BACKFIRE", nullptr);

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

    void drawSlider(const ParamId id, const char* label, const Rect bounds, const Accent accent,
                    const bool enabled = true)
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

        if (enabled)
            remember(parameter, {bounds.x, trackY - 9.0f, bounds.w, 28.0f});
    }

    // Compact value stepper: click the left half to go down, the right to go up.
    void drawStepper(const ParamId id, const char* label, const Rect bounds, const char* const* names,
                     const bool enabled = true)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);

        fc(enabled ? t.textDim : t.textDisabled);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const Rect box {bounds.x, bounds.y + 15.0f, bounds.w, 22.0f};
        beginPath();
        fc(enabled ? t.buttonFace : t.controlTrack);
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

        if (enabled && stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {parameter, box};
    }

    // ── Sections ───────────────────────────────────────────────────────────

    void drawDriveStrip(const Rect bounds)
    {
        drawPanel(bounds, "DRIVE — engine speed and load", kDriveAccent);

        drawTachometer({bounds.x + 16.0f, bounds.y + 44.0f, 112.0f, 112.0f});

        const float columnW = 218.0f;
        // A stepper is taller than a slider (label plus a 22px box), so the row
        // pitch has to clear the taller of the two.
        const float rowH = 40.0f;
        const float top = bounds.y + 42.0f;
        const float colA = bounds.x + 148.0f;
        const float colB = colA + columnW + 20.0f;
        const float colC = colB + columnW + 20.0f;

        drawSlider(ParamId::rpm, "RPM", {colA, top, columnW, rowH}, kDriveAccent, !syncing());
        drawSlider(ParamId::throttle, "Throttle  (CC 1 / 11)", {colA, top + rowH, columnW, rowH}, kDriveAccent);
        drawSlider(ParamId::inertia, "Inertia (flywheel)", {colA, top + 2.0f * rowH, columnW, rowH}, kDriveAccent);

        drawStepper(ParamId::rpmSource, "Speed source", {colB, top, columnW, rowH}, kRpmSourceNames.data());
        drawStepper(ParamId::syncRatio, "Sync ratio", {colB, top + rowH, columnW, rowH},
                    kSyncRatioNames.data(), syncing());
        drawSlider(ParamId::idleRpm, "Idle rpm (stopped)", {colB, top + 2.0f * rowH, columnW, rowH},
                   kDriveAccent, syncing());

        drawStepper(ParamId::midiCh, "MIDI control channel", {colC, top, columnW, rowH},
                    kMidiChannelNames.data());
        drawSlider(ParamId::seed, "Seed (backfire, turbulence)", {colC, top + rowH, columnW, rowH}, kDriveAccent);

        const auto& t = theme();
        fc(t.textDim);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(colC, top + 2.0f * rowH + 4.0f,
             syncing() ? "Sync: locked to host tempo, idles when stopped."
                       : "Manual: RPM from the panel, automation or CC 2.",
             nullptr);
    }

    void drawTachometer(const Rect bounds)
    {
        const auto& t = theme();
        const float cx = bounds.x + bounds.w * 0.5f;
        const float cy = bounds.y + bounds.h * 0.5f;
        const float radius = bounds.w * 0.5f;

        const auto& rpmSpec = kParameterSpecs[idx(ParamId::rpm)];
        // Prefer the processor's live speed; fall back to the panel value so the
        // dial is never blank in a host that does not poll output parameters.
        const float live = value(ParamId::outRpm);
        const float shown = (live > 1.0f) ? live : value(ParamId::rpm);
        const float normalized = clampf((shown - rpmSpec.minimum) / (rpmSpec.maximum - rpmSpec.minimum), 0.0f, 1.0f);

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

        // Redline: the top fifth of the range.
        beginPath();
        sc(t.danger);
        strokeWidth(6.0f);
        arc(cx, cy, radius - 6.0f, startAngle + sweep * 0.8f, startAngle + sweep, CW);
        stroke();

        beginPath();
        strokeColor(kDriveAccent.r, kDriveAccent.g, kDriveAccent.b, 235);
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

        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(shown)));
        fc(t.textPrimary);
        fontSize(20.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy + 20.0f, buffer, nullptr);

        fc(t.textDim);
        fontSize(10.0f);
        text(cx, cy + 36.0f, "RPM", nullptr);
    }

    void drawEnginePanel(const Rect bounds)
    {
        drawPanel(bounds, "ENGINE", kEngineAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 36.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::cylinders, "Cylinders", {x, y, w, rowH}, kEngineAccent);
        y += rowH;
        drawSlider(ParamId::displacement, "Displacement (per cylinder)", {x, y, w, rowH}, kEngineAccent);
        y += rowH;
        drawSlider(ParamId::compression, "Compression", {x, y, w, rowH}, kEngineAccent);
        y += rowH;
        drawSlider(ParamId::ignition, "Ignition (explosion width)", {x, y, w, rowH}, kEngineAccent);
        y += rowH;
        drawSlider(ParamId::asymmetry, "Growl (uneven firing)", {x, y, w, rowH}, kEngineAccent);
        y += rowH + 8.0f;

        drawFiringDiagram({x, y, w, bounds.y + bounds.h - y - 14.0f});
    }

    // One revolution of the engine cycle with a tick per cylinder, so Growl is
    // visible as unequal firing intervals rather than a number.
    void drawFiringDiagram(const Rect bounds)
    {
        const auto& t = theme();
        const float radius = std::min(bounds.w, bounds.h) * 0.5f - 22.0f;
        if (radius < 12.0f)
            return;

        const float cx = bounds.x + bounds.w * 0.5f;
        const float cy = bounds.y + bounds.h * 0.5f - 6.0f;

        beginPath();
        sc(t.border);
        strokeWidth(1.5f);
        circle(cx, cy, radius);
        stroke();

        const int count = std::max(1, intValue(ParamId::cylinders));
        const float asymmetry = clampf(value(ParamId::asymmetry), 0.0f, 1.0f);

        // Mirrors cylinderPhaseOffsets() in modules/CycleFunctions.hpp.
        float total = 0.0f;
        std::array<float, 12> offsets {};
        for (int k = 0; k < count; ++k)
        {
            offsets[static_cast<std::size_t>(k)] = total;
            const float sign = (k & 1) ? -1.0f : 1.0f;
            total += (1.0f + asymmetry * 0.5f * sign) / static_cast<float>(count);
        }
        if (total > 1.0e-6f)
        {
            for (int k = 0; k < count; ++k)
                offsets[static_cast<std::size_t>(k)] /= total;
        }

        for (int k = 0; k < count; ++k)
        {
            const float angle = -0.5f * kPi + 2.0f * kPi * offsets[static_cast<std::size_t>(k)];
            const float px = cx + radius * std::cos(angle);
            const float py = cy + radius * std::sin(angle);

            beginPath();
            fillColor(kEngineAccent.r, kEngineAccent.g, kEngineAccent.b, 255);
            circle(px, py, 6.0f);
            fill();

            if (count <= 8)
            {
                char label[8];
                std::snprintf(label, sizeof(label), "%d", k + 1);
                fillColor(250, 248, 242, 255);
                fontSize(9.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                text(px, py, label, nullptr);
            }
        }

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy - 7.0f, "firing order", nullptr);
        char summary[48];
        std::snprintf(summary, sizeof(summary), "%d per cycle", count);
        text(cx, cy + 7.0f, summary, nullptr);

        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(cx, bounds.y + bounds.h - 12.0f, "Growl spaces the firings unevenly.", nullptr);
    }

    void drawExhaustPanel(const Rect bounds)
    {
        drawPanel(bounds, "INTAKE  /  EXHAUST", kPipeAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 34.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::intakeLen, "Intake runner", {x, y, w, rowH}, kPipeAccent);
        y += rowH;
        drawSlider(ParamId::turbulence, "Turbulence (aspiration noise)", {x, y, w, rowH}, kPipeAccent);
        y += rowH + 4.0f;

        drawSlider(ParamId::extractorLen, "Extractor", {x, y, w, rowH}, kPipeAccent);
        y += rowH;
        drawSlider(ParamId::pipeLen, "Straight pipe", {x, y, w, rowH}, kPipeAccent);
        y += rowH;
        drawSlider(ParamId::mufflerLen, "Muffler", {x, y, w, rowH}, kPipeAccent);
        y += rowH;
        drawSlider(ParamId::mufflerAction, "Silencing", {x, y, w, rowH}, kPipeAccent);
        y += rowH;
        drawSlider(ParamId::outletLen, "Tailpipe", {x, y, w, rowH}, kPipeAccent);
        y += rowH;
        drawSlider(ParamId::backfire, "Backfire (on overrun)", {x, y, w, rowH}, kPipeAccent);
        y += rowH;

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y + 4.0f, "Lengths in metres, with each tube's resonance.", nullptr);
        text(x, y + 17.0f, "Backfire only fires on overrun.", nullptr);
    }

    void drawListeningPanel(const Rect bounds)
    {
        drawPanel(bounds, "LISTENING POSITION", kListenAccent);

        drawCarPlan({bounds.x + 14.0f, bounds.y + 40.0f, bounds.w - 28.0f, 106.0f});

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 36.0f;
        float y = bounds.y + 152.0f;

        drawSlider(ParamId::intakeGain, "Intake level", {x, y, w, rowH}, kListenAccent);
        y += rowH;
        drawSlider(ParamId::blockGain, "Block level", {x, y, w, rowH}, kListenAccent);
        y += rowH;
        drawSlider(ParamId::outletGain, "Exhaust level", {x, y, w, rowH}, kListenAccent);
        y += rowH;
        drawSlider(ParamId::width, "Stereo width", {x, y, w, rowH}, kListenAccent);
        y += rowH;
        drawSlider(ParamId::level, "Output", {x, y, w, rowH}, kListenAccent);
        y += rowH;

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y + 4.0f, "Intake, block and exhaust, mixed for the vantage above.", nullptr);
    }

    // Plan view of the car with a selectable listening point at each vantage.
    void drawCarPlan(const Rect bounds)
    {
        const auto& t = theme();

        const float bodyW = bounds.w * 0.34f;
        const float bodyH = bounds.h * 0.78f;
        const float cx = bounds.x + bounds.w * 0.5f;
        const float cy = bounds.y + bounds.h * 0.5f;

        beginPath();
        sc(t.border);
        strokeWidth(1.5f);
        roundedRect(cx - bodyW * 0.5f, cy - bodyH * 0.5f, bodyW, bodyH, 10.0f);
        stroke();

        // Cabin outline, so "front" and "rear" are unambiguous.
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(cx - bodyW * 0.34f, cy - bodyH * 0.14f, bodyW * 0.68f, bodyH * 0.30f, 4.0f);
        stroke();

        const int selected = std::clamp(intValue(ParamId::listen), 0, 3);
        const std::array<float, 4> dotX {cx, cx, cx, cx - bodyW * 1.15f};
        const std::array<float, 4> dotY {cy + bodyH * 0.02f,          // cabin
                                         cy - bodyH * 0.40f,          // front
                                         cy + bodyH * 0.40f,          // rear
                                         cy};                         // exterior

        for (int i = 0; i < 4; ++i)
        {
            const auto index = static_cast<std::size_t>(i);
            const bool active = (i == selected);

            beginPath();
            if (active)
                fillColor(kListenAccent.r, kListenAccent.g, kListenAccent.b, 255);
            else
                fc(t.accentDim);
            circle(dotX[index], dotY[index], active ? 8.0f : 5.5f);
            fill();

            fc(active ? t.textPrimary : t.textDim);
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            // Front reads above its dot, everything else below, so no label
            // ever lands on the car outline or leaves the panel.
            const float labelY = dotY[index] + (i == 1 ? -14.0f : 14.0f);
            text(dotX[index], labelY, kListenNames[index], nullptr);

            if (positionCount_ < positionRects_.size())
            {
                positionRects_[positionCount_++] =
                    {i, {dotX[index] - 13.0f, dotY[index] - 13.0f, 26.0f, 26.0f}};
            }
        }
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 40> sliders_ {};
    std::array<Stepper, 8> steppers_ {};
    std::array<PositionDot, 4> positionRects_ {};
    std::size_t sliderCount_ = 0;
    std::size_t stepperCount_ = 0;
    std::size_t positionCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetoUI)
};

UI* createUI()
{
    return new MagnetoUI();
}

END_NAMESPACE_DISTRHO
