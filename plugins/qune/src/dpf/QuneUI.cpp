#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "qune_params.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::qune::kFormantNames;
using downspout::qune::kMidiChannelNames;
using downspout::qune::kMidiOutNames;
using downspout::qune::kNoNotesNames;
using downspout::qune::kNoteNames;
using downspout::qune::kParameterCount;
using downspout::qune::kParameterSpecs;
using downspout::qune::kQualityNames;
using downspout::qune::kRanges;
using downspout::qune::kScaleNames;
using downspout::qune::kSourceNames;
using downspout::qune::ParamId;

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

constexpr Accent kPitchAccent {198, 132, 58};    // amber
constexpr Accent kScaleAccent {124, 148, 104};   // olive
constexpr Accent kCorrectAccent {176, 88, 62};   // hot metal
constexpr Accent kEngineAccent {92, 140, 156};   // cold steel

constexpr std::size_t kTraceLength = 160;        // about five seconds
constexpr float kTraceSpan = 18.0f;              // semitones shown
constexpr float kItemHeight = 22.0f;

[[nodiscard]] float clampf(const float value, const float lo, const float hi)
{
    return std::max(lo, std::min(value, hi));
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

std::string noteName(const float midi)
{
    const int note = static_cast<int>(std::lround(midi));
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%s%d", kNoteNames[static_cast<std::size_t>(((note % 12) + 12) % 12)],
                  note / 12 - 1);
    return buffer;
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[64];
    switch (static_cast<ParamId>(parameter))
    {
    case ParamId::amount:
    case ParamId::voicing:
    case ParamId::mix:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
        break;
    case ParamId::glide:
        if (value < 0.5f)
            std::snprintf(buffer, sizeof(buffer), "off");
        else
            std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(value)));
        break;
    case ParamId::stickiness:
        std::snprintf(buffer, sizeof(buffer), "%d cents", static_cast<int>(std::lround(value)));
        break;
    case ParamId::transpose:
        std::snprintf(buffer, sizeof(buffer), "%+d st", static_cast<int>(std::lround(value)));
        break;
    case ParamId::outVelocity:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        break;
    case ParamId::bendRange:
        std::snprintf(buffer, sizeof(buffer), "+/-%d st", static_cast<int>(std::lround(value)));
        break;
    case ParamId::formantShift:
        std::snprintf(buffer, sizeof(buffer), "%+.1f st", static_cast<double>(value));
        break;
    case ParamId::level:
        std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(value));
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
        break;
    }
    return buffer;
}

// Label lists for the steppers and the scale dropdown.
struct NameList {
    const char* const* names;
    int count;
};

NameList namesFor(const ParamId id)
{
    switch (id)
    {
    case ParamId::root: return {kNoteNames.data(), static_cast<int>(kNoteNames.size())};
    case ParamId::scale: return {kScaleNames.data(), static_cast<int>(kScaleNames.size())};
    case ParamId::source: return {kSourceNames.data(), static_cast<int>(kSourceNames.size())};
    case ParamId::noNotes: return {kNoNotesNames.data(), static_cast<int>(kNoNotesNames.size())};
    case ParamId::midiCh: return {kMidiChannelNames.data(), static_cast<int>(kMidiChannelNames.size())};
    case ParamId::midiOut: return {kMidiOutNames.data(), static_cast<int>(kMidiOutNames.size())};
    case ParamId::outChannel: return {kMidiChannelNames.data(), static_cast<int>(kMidiChannelNames.size())};
    case ParamId::quality: return {kQualityNames.data(), static_cast<int>(kQualityNames.size())};
    case ParamId::formantMode: return {kFormantNames.data(), static_cast<int>(kFormantNames.size())};
    default: return {nullptr, 0};
    }
}

}  // namespace

class QuneUI : public UI {
public:
    QuneUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
        lastSample_ = std::chrono::steady_clock::now();
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

    void uiIdle() override
    {
        // The pitch trace is sampled on a clock of its own, so it scrolls at a
        // steady rate whatever the host does with output parameters.
        const auto now = std::chrono::steady_clock::now();
        if (now - lastSample_ < std::chrono::milliseconds(30))
            return;
        lastSample_ = now;

        const float detected = value(ParamId::outPitch);
        const float target = value(ParamId::outTarget);
        trace_[traceHead_] = {detected, target};
        traceHead_ = (traceHead_ + 1) % kTraceLength;
        if (detected > 0.0f)
            viewCentre_ += (detected - viewCentre_) * 0.04f;
        repaint();
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());

        sliderCount_ = 0;
        stepperCount_ = 0;
        dropdown_ = {};

        drawBackground(width, height);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});
        drawPitchPanel({24.0f, 108.0f, width - 48.0f, 212.0f});

        const float columnW = (width - 48.0f - 32.0f) / 3.0f;
        const float columnY = 336.0f;
        const float columnH = height - columnY - 24.0f;
        drawScalePanel({24.0f, columnY, columnW, columnH});
        drawCorrectionPanel({24.0f + columnW + 16.0f, columnY, columnW, columnH});
        drawEnginePanel({24.0f + 2.0f * (columnW + 16.0f), columnY, columnW, columnH});

        if (dropdownOpen_)
            drawDropdownMenu();
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

        if (dropdownOpen_)
        {
            const Rect menu = dropdownMenuRect();
            if (menu.contains(mx, my))
            {
                const int rows = dropdownRows();
                const float itemW = menu.w / static_cast<float>(dropdownColumns());
                const int col = static_cast<int>((mx - menu.x) / itemW);
                const int row = static_cast<int>((my - menu.y) / kItemHeight);
                const int item = col * rows + row;
                if (item >= 0 && item < namesFor(ParamId::scale).count)
                    commit(idx(ParamId::scale), static_cast<float>(item));
            }
            dropdownOpen_ = false;
            repaint();
            return true;
        }

        if (themeRect_.contains(mx, my))
        {
            darkTheme_ = !darkTheme_;
            repaint();
            return true;
        }

        if (dropdown_.contains(mx, my))
        {
            dropdownOpen_ = true;
            repaint();
            return true;
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

    struct TracePoint {
        float detected = 0.0f;
        float target = 0.0f;
    };

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    [[nodiscard]] float value(const ParamId id) const { return values_[idx(id)]; }
    [[nodiscard]] int intValue(const ParamId id) const { return static_cast<int>(std::lround(values_[idx(id)])); }
    [[nodiscard]] bool midiTargeting() const { return intValue(ParamId::source) != 0; }
    [[nodiscard]] bool isFast() const { return intValue(ParamId::quality) == 0; }

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
        text(bounds.x, bounds.y, "Qune", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(bounds.x + 100.0f, bounds.y + 10.0f, "pitch quantiser", nullptr);

        fc(t.textDim);
        fontSize(12.0f);
        text(bounds.x, bounds.y + 44.0f,
             "detects the pitch of the input and moves it to the nearest note of a scale, or to MIDI notes you play",
             nullptr);

        // Voiced lamp: lit while a pitch is being tracked.
        const bool voiced = value(ParamId::outPitch) > 0.0f;
        const float lampX = bounds.x + bounds.w - 330.0f;
        beginPath();
        circle(lampX, bounds.y + 18.0f, 7.0f);
        if (voiced)
            fillColor(kPitchAccent.r, kPitchAccent.g, kPitchAccent.b, 255);
        else
            fc(t.accentDim);
        fill();

        fc(voiced ? t.textPrimary : t.textDisabled);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, bounds.y + 18.0f, voiced ? "PITCH FOUND" : "NO PITCH", nullptr);

        char latency[48];
        std::snprintf(latency, sizeof(latency), "LATENCY %.1f ms", static_cast<double>(value(ParamId::outLatency)));
        fc(t.textDim);
        text(lampX + 118.0f, bounds.y + 18.0f, latency, nullptr);

        themeRect_ = {bounds.x + bounds.w - 74.0f, bounds.y + 4.0f, 74.0f, 26.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall);
        fill();
        fc(t.textDim);
        fontSize(11.0f);
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

    // Value stepper: click the left half to go down, the right half to go up.
    void drawStepper(const ParamId id, const char* label, const Rect bounds, const bool enabled = true)
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);
        const NameList list = namesFor(id);

        fc(enabled ? t.textDim : t.textDisabled);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const Rect box {bounds.x, bounds.y + 15.0f, bounds.w, 22.0f};
        drawBox(box, enabled);

        const int selection = std::clamp(static_cast<int>(std::lround(values_[parameter])), 0, std::max(0, list.count - 1));
        fc(enabled ? t.textPrimary : t.textDisabled);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, list.names[selection], nullptr);

        fc(enabled ? t.textDim : t.textDisabled);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);

        if (enabled && stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {parameter, box};
    }

    // The scale chooser, as a dropdown: it lists twenty-four names, too many to step through.
    void drawDropdown(const char* label, const Rect bounds)
    {
        const auto& t = theme();
        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const Rect box {bounds.x, bounds.y + 15.0f, bounds.w, 22.0f};
        drawBox(box, true);
        const int selection = std::clamp(intValue(ParamId::scale), 0, namesFor(ParamId::scale).count - 1);
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 9.0f, box.y + box.h * 0.5f, kScaleNames[static_cast<std::size_t>(selection)], nullptr);
        fc(t.textDim);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 8.0f, box.y + box.h * 0.5f, "v", nullptr);
        dropdown_ = box;
    }

    void drawBox(const Rect box, const bool enabled)
    {
        const auto& t = theme();
        beginPath();
        fc(enabled ? t.buttonFace : t.controlTrack);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        stroke();
    }

    void note(const float x, const float y, const char* message)
    {
        fc(theme().textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, message, nullptr);
    }

    // ── Dropdown menu ──────────────────────────────────────────────────────

    [[nodiscard]] int dropdownColumns() const { return 2; }
    [[nodiscard]] int dropdownRows() const
    {
        const int count = namesFor(ParamId::scale).count;
        return (count + dropdownColumns() - 1) / dropdownColumns();
    }

    [[nodiscard]] Rect dropdownMenuRect() const
    {
        const float itemW = std::max(dropdown_.w, 150.0f);
        const float menuW = itemW * static_cast<float>(dropdownColumns());
        const float menuH = static_cast<float>(dropdownRows()) * kItemHeight;
        const float margin = 14.0f;
        const float x = clampf(dropdown_.x, margin, std::max(margin, static_cast<float>(getWidth()) - margin - menuW));
        float y = dropdown_.y + dropdown_.h + 6.0f;
        if (y + menuH > static_cast<float>(getHeight()) - margin)
            y = dropdown_.y - menuH - 6.0f;
        return {x, clampf(y, margin, std::max(margin, static_cast<float>(getHeight()) - margin - menuH)), menuW, menuH};
    }

    void drawDropdownMenu()
    {
        const auto& t = theme();
        const Rect menu = dropdownMenuRect();
        const int rows = dropdownRows();
        const int columns = dropdownColumns();
        const float itemW = menu.w / static_cast<float>(columns);
        const int selected = intValue(ParamId::scale);

        beginPath();
        roundedRect(menu.x, menu.y, menu.w, menu.h, laf::kRadiusPanel);
        fc(t.panel.withAlpha(250));
        fill();
        sc(t.border);
        strokeWidth(1.0f);
        stroke();

        const int count = namesFor(ParamId::scale).count;
        for (int i = 0; i < count; ++i)
        {
            const int col = i / rows;
            const int row = i % rows;
            const float x = menu.x + static_cast<float>(col) * itemW;
            const float y = menu.y + static_cast<float>(row) * kItemHeight;
            if (i == selected)
            {
                beginPath();
                roundedRect(x + 4.0f, y + 2.0f, itemW - 8.0f, kItemHeight - 4.0f, laf::kRadiusSmall);
                fillColor(kScaleAccent.r, kScaleAccent.g, kScaleAccent.b, 200);
                fill();
            }
            fc(t.textPrimary);
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(x + 14.0f, y + kItemHeight * 0.5f, kScaleNames[static_cast<std::size_t>(i)], nullptr);
        }
    }

    // ── Pitch panel ────────────────────────────────────────────────────────

    [[nodiscard]] unsigned inScaleMask() const
    {
        return downspout::qune::rotatedScaleMask(static_cast<std::size_t>(intValue(ParamId::scale)),
                                                 intValue(ParamId::root));
    }

    void drawPitchPanel(const Rect bounds)
    {
        drawPanel(bounds, "PITCH  -  what was sung, and where it was moved to", kPitchAccent);

        const Rect plot {bounds.x + 52.0f, bounds.y + 44.0f, bounds.w - 52.0f - 250.0f, bounds.h - 58.0f};
        drawTrace(plot);
        drawReadout({bounds.x + bounds.w - 226.0f, bounds.y + 44.0f, 210.0f, bounds.h - 58.0f});
    }

    void drawTrace(const Rect plot)
    {
        const auto& t = theme();

        beginPath();
        fc(t.panel);
        roundedRect(plot.x, plot.y, plot.w, plot.h, laf::kRadiusSmall);
        fill();

        save();
        scissor(plot.x, plot.y, plot.w, plot.h);

        const float bottomNote = viewCentre_ - kTraceSpan * 0.5f;
        const auto yFor = [&](const float midi) {
            return plot.y + plot.h - (midi - bottomNote) / kTraceSpan * plot.h;
        };

        // One line per semitone; the notes of the scale are drawn stronger and named.
        const unsigned mask = inScaleMask();
        const int first = static_cast<int>(std::floor(bottomNote)) - 1;
        const int last = static_cast<int>(std::ceil(bottomNote + kTraceSpan)) + 1;
        for (int n = first; n <= last; ++n)
        {
            const bool member = (mask >> (((n % 12) + 12) % 12)) & 1u;
            const float y = yFor(static_cast<float>(n));
            beginPath();
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            if (member)
                strokeColor(kScaleAccent.r, kScaleAccent.g, kScaleAccent.b, 150);
            else
                strokeColor(t.border.r, t.border.g, t.border.b, 70);
            strokeWidth(member ? 1.2f : 0.7f);
            stroke();
        }
        resetScissor();
        restore();

        // Note names down the left edge, for the scale notes only.
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        for (int n = first; n <= last; ++n)
        {
            const float y = yFor(static_cast<float>(n));
            if (y < plot.y + 5.0f || y > plot.y + plot.h - 5.0f)
                continue;
            if ((mask >> (((n % 12) + 12) % 12)) & 1u)
            {
                fc(t.textDim);
                text(plot.x - 6.0f, y, noteName(static_cast<float>(n)).c_str(), nullptr);
            }
        }

        save();
        scissor(plot.x, plot.y, plot.w, plot.h);
        const float stepX = plot.w / static_cast<float>(kTraceLength - 1);

        // Corrected pitch: a stepped line, thick.
        drawTraceLine(plot, stepX, yFor, true);
        // What was sung: thin.
        drawTraceLine(plot, stepX, yFor, false);
        resetScissor();
        restore();

        fc(t.textDisabled);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(plot.x + 8.0f, plot.y + plot.h - 6.0f, "5 s", nullptr);

        bool any = false;
        for (const TracePoint& point : trace_)
            any = any || point.detected > 0.0f;
        if (!any)
        {
            fc(t.textDim);
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f - 8.0f, "Feed a voice or a single-note instrument into the input.", nullptr);
            fontSize(11.0f);
            text(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f + 12.0f,
                 "Thin line: what is heard.   Thick line: where it is moved to.", nullptr);
        }
    }

    template <typename YFor>
    void drawTraceLine(const Rect plot, const float stepX, const YFor& yFor, const bool corrected)
    {
        bool drawing = false;
        beginPath();
        for (std::size_t i = 0; i < kTraceLength; ++i)
        {
            const TracePoint& point = trace_[(traceHead_ + i) % kTraceLength];
            const float v = corrected ? point.target : point.detected;
            const float x = plot.x + static_cast<float>(i) * stepX;
            if (v <= 0.0f)
            {
                drawing = false;
                continue;
            }
            const float y = yFor(v);
            if (!drawing)
            {
                moveTo(x, y);
                drawing = true;
            }
            else
                lineTo(x, y);
        }
        if (corrected)
        {
            strokeColor(kPitchAccent.r, kPitchAccent.g, kPitchAccent.b, 255);
            strokeWidth(3.0f);
        }
        else
        {
            const auto& t = theme();
            strokeColor(t.textPrimary.r, t.textPrimary.g, t.textPrimary.b, 200);
            strokeWidth(1.3f);
        }
        stroke();
    }

    void drawReadout(const Rect bounds)
    {
        const auto& t = theme();
        const float detected = value(ParamId::outPitch);
        const float target = value(ParamId::outTarget);
        const float shift = value(ParamId::outShift);

        const auto line = [&](const float y, const char* label, const std::string& shown, const bool strong) {
            fc(t.textDim);
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(bounds.x, y, label, nullptr);
            fc(strong ? t.textPrimary : t.textDim);
            fontSize(strong ? 22.0f : 16.0f);
            text(bounds.x, y + 14.0f, shown.c_str(), nullptr);
        };

        std::string heard = "-";
        if (detected > 0.0f)
        {
            const float nearest = std::round(detected);
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%s  %+d c", noteName(nearest).c_str(),
                          static_cast<int>(std::lround((detected - nearest) * 100.0f)));
            heard = buffer;
        }
        line(bounds.y, "HEARD", heard, false);

        line(bounds.y + 52.0f, "SINGS", target > 0.0f ? noteName(target) : std::string("-"), true);

        char shiftText[32];
        if (target > 0.0f)
            std::snprintf(shiftText, sizeof(shiftText), "%+.2f st", static_cast<double>(shift));
        else
            std::snprintf(shiftText, sizeof(shiftText), "none");
        line(bounds.y + 104.0f, "SHIFT", shiftText, false);

        // Shift meter: centre is no change.
        const Rect meter {bounds.x, bounds.y + 146.0f, bounds.w, 8.0f};
        beginPath();
        fc(t.controlTrack);
        roundedRect(meter.x, meter.y, meter.w, meter.h, 3.0f);
        fill();
        const float centre = meter.x + meter.w * 0.5f;
        const float amount = clampf(shift / 6.0f, -1.0f, 1.0f) * meter.w * 0.5f;
        if (target > 0.0f && std::fabs(amount) > 0.5f)
        {
            beginPath();
            fillColor(kPitchAccent.r, kPitchAccent.g, kPitchAccent.b, 255);
            rect(std::min(centre, centre + amount), meter.y, std::fabs(amount), meter.h);
            fill();
        }
        beginPath();
        sc(t.textDim);
        strokeWidth(1.0f);
        moveTo(centre, meter.y - 3.0f);
        lineTo(centre, meter.y + meter.h + 3.0f);
        stroke();
    }

    // ── Scale panel ────────────────────────────────────────────────────────

    void drawScalePanel(const Rect bounds)
    {
        drawPanel(bounds, "TARGET  -  the notes it is moved to", kScaleAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        float y = bounds.y + 42.0f;

        const float rootW = 80.0f;
        drawStepper(ParamId::root, "Root", {x, y, rootW, 40.0f}, true);
        drawDropdown("Scale", {x + rootW + 12.0f, y, w - rootW - 12.0f, 40.0f});
        y += 46.0f;

        drawKeyboard({x, y, w, 52.0f});
        y += 62.0f;

        drawStepper(ParamId::source, "Take notes from", {x, y, w, 40.0f});
        y += 42.0f;

        const float half = (w - 12.0f) * 0.5f;
        drawStepper(ParamId::noNotes, "When no key is held", {x, y, half, 40.0f}, midiTargeting());
        drawStepper(ParamId::midiCh, "MIDI input", {x + half + 12.0f, y, half, 40.0f}, midiTargeting());
        y += 42.0f;

        drawSlider(ParamId::bendRange, "Pitch bend range", {x, y, w, 36.0f}, kScaleAccent, midiTargeting());
        y += 38.0f;

        note(x, y, midiTargeting() ? "Held MIDI notes are the targets; pitch bend moves them."
                                   : "Scale mode. Pick MIDI to sing the notes you play.");
    }

    // One octave of piano keys: lit for scale members, outlined where the
    // voice is now, dotted where it is being sent.
    void drawKeyboard(const Rect bounds)
    {
        const auto& t = theme();
        const unsigned mask = inScaleMask();
        const int heardClass = value(ParamId::outPitch) > 0.0f
                                   ? ((static_cast<int>(std::lround(value(ParamId::outPitch))) % 12) + 12) % 12
                                   : -1;
        const int targetClass = value(ParamId::outTarget) > 0.0f
                                    ? ((static_cast<int>(std::lround(value(ParamId::outTarget))) % 12) + 12) % 12
                                    : -1;

        constexpr std::array<int, 7> whites {0, 2, 4, 5, 7, 9, 11};
        constexpr std::array<int, 5> blacks {1, 3, 6, 8, 10};
        constexpr std::array<float, 5> blackAt {1.0f, 2.0f, 4.0f, 5.0f, 6.0f};  // boundary after white key n
        const float keyW = bounds.w / 7.0f;

        for (std::size_t k = 0; k < whites.size(); ++k)
        {
            const int pc = whites[k];
            const Rect key {bounds.x + static_cast<float>(k) * keyW, bounds.y, keyW, bounds.h};
            drawKey(key, pc, (mask >> pc) & 1u, pc == heardClass, pc == targetClass, false);
        }
        for (std::size_t k = 0; k < blacks.size(); ++k)
        {
            const int pc = blacks[k];
            const float bw = keyW * 0.62f;
            const Rect key {bounds.x + blackAt[k] * keyW - bw * 0.5f, bounds.y, bw, bounds.h * 0.6f};
            drawKey(key, pc, (mask >> pc) & 1u, pc == heardClass, pc == targetClass, true);
        }
        (void)t;
    }

    void drawKey(const Rect key, const int pitchClass, const bool member, const bool heard, const bool sent,
                 const bool black)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(key.x + 0.5f, key.y, key.w - 1.0f, key.h, 3.0f);
        if (member)
            fillColor(kScaleAccent.r, kScaleAccent.g, kScaleAccent.b, black ? 235 : 215);
        else if (black)
            fc(t.controlTrack.darker(60));
        else
            fc(t.buttonFace);
        fill();

        beginPath();
        roundedRect(key.x + 0.5f, key.y, key.w - 1.0f, key.h, 3.0f);
        if (heard)
        {
            strokeColor(t.textPrimary.r, t.textPrimary.g, t.textPrimary.b, 255);
            strokeWidth(2.5f);
        }
        else
        {
            sc(t.border);
            strokeWidth(1.0f);
        }
        stroke();

        if (sent)
        {
            beginPath();
            circle(key.x + key.w * 0.5f, key.y + key.h - (black ? 9.0f : 11.0f), 5.0f);
            fillColor(kPitchAccent.r, kPitchAccent.g, kPitchAccent.b, 255);
            fill();
        }
        if (!black)
        {
            fc(member ? t.textPrimary : t.textDim);
            fontSize(9.0f);
            textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
            if (!sent)
                text(key.x + key.w * 0.5f, key.y + key.h - 4.0f, kNoteNames[static_cast<std::size_t>(pitchClass)], nullptr);
        }
    }

    // ── Correction panel ───────────────────────────────────────────────────

    void drawCorrectionPanel(const Rect bounds)
    {
        drawPanel(bounds, "CORRECTION  -  how hard and how fast", kCorrectAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 34.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::amount, "Amount (strength)", {x, y, w, rowH}, kCorrectAccent);
        y += rowH;
        drawSlider(ParamId::glide, "Glide (off = steps at once)", {x, y, w, rowH}, kCorrectAccent);
        y += rowH;
        drawSlider(ParamId::stickiness, "Stickiness (hold the note)", {x, y, w, rowH}, kCorrectAccent);
        y += rowH;
        drawSlider(ParamId::transpose, "Transpose (target offset)", {x, y, w, rowH}, kCorrectAccent);
        y += rowH;
        drawSlider(ParamId::voicing, "Voicing (breath tolerance)", {x, y, w, rowH}, kCorrectAccent);
        y += rowH + 4.0f;

        const float half = (w - 12.0f) * 0.5f;
        const bool sending = intValue(ParamId::midiOut) != 0;
        drawStepper(ParamId::midiOut, "MIDI out (target notes)", {x, y, half, 40.0f});
        drawStepper(ParamId::outChannel, "Out channel", {x + half + 12.0f, y, half, 40.0f}, sending);
        y += 42.0f;
        drawSlider(ParamId::outVelocity, "Out velocity", {x, y, w, 36.0f}, kCorrectAccent, sending);
    }

    // ── Engine panel ───────────────────────────────────────────────────────

    void drawEnginePanel(const Rect bounds)
    {
        drawPanel(bounds, "ENGINE  -  quality, range, formants", kEngineAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float half = (w - 12.0f) * 0.5f;
        float y = bounds.y + 42.0f;

        drawStepper(ParamId::quality, "Quality", {x, y, half, 40.0f});
        // Range is a stepper too; it has its own name list.
        drawRangeStepper({x + half + 12.0f, y, half, 40.0f});
        y += 42.0f;

        static const char* const kQualityNotes[3][2] = {
            {"Fast: pitch-synchronous splice delay line,", "a few ms. Formants follow the pitch."},
            {"Good: pitch-synchronous overlap-add.", "Formants are kept, or moved by Formant shift."},
            {"Best: phase vocoder, cepstral formants.", "Smoothest on chords and noise; slowest."},
        };
        const int quality = std::clamp(intValue(ParamId::quality), 0, 2);
        note(x, y, kQualityNotes[quality][0]);
        note(x, y + 13.0f, kQualityNotes[quality][1]);
        y += 32.0f;

        drawStepper(ParamId::formantMode, "Formants", {x, y, w, 40.0f}, !isFast());
        y += 42.0f;
        drawSlider(ParamId::formantShift, "Formant shift", {x, y, w, 36.0f}, kEngineAccent, !isFast());
        y += 40.0f;
        drawSlider(ParamId::mix, "Mix (dry / corrected)", {x, y, w, 36.0f}, kEngineAccent);
        y += 38.0f;
        drawSlider(ParamId::level, "Output level", {x, y, w, 36.0f}, kEngineAccent);
        y += 38.0f;

        note(x, y, "Pitch range sets the lowest note tracked. In Good");
        note(x, y + 13.0f, "it also sets the latency: higher floor, faster.");
    }

    void drawRangeStepper(const Rect bounds)
    {
        const auto& t = theme();
        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, "Pitch range", nullptr);

        const Rect box {bounds.x, bounds.y + 15.0f, bounds.w, 22.0f};
        drawBox(box, true);
        const int selection = std::clamp(intValue(ParamId::range), 0, static_cast<int>(kRanges.size()) - 1);
        char label[48];
        std::snprintf(label, sizeof(label), "%s %d-%d Hz", kRanges[static_cast<std::size_t>(selection)].name,
                      static_cast<int>(kRanges[static_cast<std::size_t>(selection)].minHz),
                      static_cast<int>(kRanges[static_cast<std::size_t>(selection)].maxHz));
        fc(t.textPrimary);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, label, nullptr);
        fc(t.textDim);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);

        if (stepperCount_ < steppers_.size())
            steppers_[stepperCount_++] = {idx(ParamId::range), box};
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 24> sliders_ {};
    std::array<Stepper, 12> steppers_ {};
    std::array<TracePoint, kTraceLength> trace_ {};
    std::size_t traceHead_ = 0;
    std::size_t sliderCount_ = 0;
    std::size_t stepperCount_ = 0;
    std::chrono::steady_clock::time_point lastSample_;
    float viewCentre_ = 60.0f;
    Rect themeRect_ {};
    Rect dropdown_ {};
    bool dropdownOpen_ = false;
    int activeSlider_ = -1;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuneUI)
};

UI* createUI()
{
    return new QuneUI();
}

END_NAMESPACE_DISTRHO
