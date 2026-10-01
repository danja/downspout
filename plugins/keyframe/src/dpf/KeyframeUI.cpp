#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "keyframe_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::keyframe::kParameterCount;
using downspout::keyframe::kParameterSpecs;
using downspout::keyframe::ParamId;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kSr = 48000.0f;

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

constexpr Accent kRateAccent {198, 132, 58};   // amber (magneto DRIVE)
constexpr Accent kSpliceAccent {176, 88, 62};  // hot metal (magneto ENGINE)
constexpr Accent kOutAccent {92, 140, 156};    // cold steel (magneto EXHAUST)

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

// A rate reads best as a multiple rather than a percentage: 0.5x is half speed.
[[nodiscard]] float rateDisplay(const float value)
{
    return value;
}

std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[64];
    const auto id = static_cast<ParamId>(parameter);
    switch (id)
    {
    case ParamId::time:
    case ParamId::pitch:
        std::snprintf(buffer, sizeof(buffer), "%.2fx", rateDisplay(value));
        break;
    case ParamId::splice:
        std::snprintf(buffer, sizeof(buffer), "%d kf", static_cast<int>(std::lround(value)));
        break;
    case ParamId::threshold:
        std::snprintf(buffer, sizeof(buffer), "%d dB", static_cast<int>(std::lround(value)));
        break;
    case ParamId::maxSplice:
        std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(value)));
        break;
    case ParamId::hold:
        std::snprintf(buffer, sizeof(buffer), "%s", value > 0.5f ? "On" : "Off");
        break;
    case ParamId::outDensity:
        std::snprintf(buffer, sizeof(buffer), "%.0f/s", value);
        break;
    case ParamId::outDrift:
        std::snprintf(buffer, sizeof(buffer), "%+d kf", static_cast<int>(std::lround(value)));
        break;
    case ParamId::outLatency:
        std::snprintf(buffer, sizeof(buffer), "%d spl", static_cast<int>(std::lround(value)));
        break;
    default:
        std::snprintf(buffer, sizeof(buffer), "%d%%",
                      static_cast<int>(std::lround(clampf(value, 0.0f, 1.0f) * 100.0f)));
        break;
    }
    return buffer;
}

}  // namespace

class KeyframeUI : public UI {
public:
    KeyframeUI()
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
        switchCount_ = 0;

        drawBackground(width, height);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});
        drawRateStrip({24.0f, 108.0f, width - 48.0f, 172.0f});

        const float columnW = (width - 48.0f - 32.0f) / 3.0f;
        const float columnY = 296.0f;
        const float columnH = height - columnY - 24.0f;
        drawAnalysisPanel({24.0f, columnY, columnW, columnH});
        drawSplicePanel({24.0f + columnW + 16.0f, columnY, columnW, columnH});
        drawOutputPanel({24.0f + 2.0f * (columnW + 16.0f), columnY, columnW, columnH});
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

        for (std::size_t i = 0; i < switchCount_; ++i)
        {
            if (switches_[i].bounds.contains(mx, my))
            {
                const Switch& s = switches_[i];
                commit(s.parameter, values_[s.parameter] > 0.5f ? 0.0f : 1.0f);
                return true;
            }
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

    struct Switch {
        std::uint32_t parameter = 0;
        Rect bounds {};
    };

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }
    [[nodiscard]] float value(const ParamId id) const { return values_[idx(id)]; }
    [[nodiscard]] bool holding() const { return value(ParamId::hold) > 0.5f; }

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

    void setFromMouse(const Slider& slider, const float mouseX)
    {
        const auto& spec = kParameterSpecs[slider.parameter];
        const float n = clampf((mouseX - slider.bounds.x) / std::max(1.0f, slider.bounds.w), 0.0f, 1.0f);
        commit(slider.parameter, spec.minimum + n * (spec.maximum - spec.minimum));
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
        text(bounds.x, bounds.y, "Keyframe", nullptr);

        fc(t.textDim);
        fontSize(15.0f);
        text(bounds.x + 138.0f, bounds.y + 10.0f, "extrema-sampling time stretch", nullptr);

        fc(t.textDim);
        fontSize(12.0f);
        text(bounds.x, bounds.y + 44.0f,
             "sparse keyframes drive an overlap-add splice whose length follows keyframe spacing",
             nullptr);

        const float lamp = clampf(value(ParamId::outSplice), 0.0f, 1.0f);
        const float lampX = bounds.x + bounds.w - 188.0f;
        beginPath();
        circle(lampX, bounds.y + 18.0f, 7.0f);
        if (lamp > 0.02f)
            fillColor(t.accent.r, t.accent.g, t.accent.b, static_cast<uchar>(80 + 175 * lamp));
        else
            fc(t.accentDim);
        fill();

        fc(lamp > 0.02f ? t.accent : t.textDisabled);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, bounds.y + 18.0f, "SPLICE", nullptr);

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
        beginPath();
        fc(theme().surface);
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

        fc(enabled ? t.textPrimary : t.textDisabled);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(bounds.x + bounds.w, bounds.y, formatValue(parameter, values_[parameter]).c_str(), nullptr);

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

    void drawSwitch(const ParamId id, const char* label, const Rect bounds, const Accent accent,
                    const char* offLabel = "Off", const char* onLabel = "On")
    {
        const auto& t = theme();
        const std::uint32_t parameter = idx(id);
        const bool on = values_[parameter] > 0.5f;

        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, label, nullptr);

        const Rect box {bounds.x, bounds.y + 15.0f, bounds.w, 22.0f};
        beginPath();
        if (on)
            fillColor(accent.r, accent.g, accent.b, 255);
        else
            fc(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(on ? laf::Colour(250, 248, 242, 255) : t.border);
        strokeWidth(1.0f);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall);
        stroke();

        fc(on ? laf::Colour(250, 248, 242, 255) : t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, on ? onLabel : offLabel, nullptr);

        if (switchCount_ < switches_.size())
            switches_[switchCount_++] = {parameter, box};
    }

    // ── Sections ───────────────────────────────────────────────────────────

    void drawRateStrip(const Rect bounds)
    {
        drawPanel(bounds, "RATES  —  time and pitch", kRateAccent);

        drawRatioDial({bounds.x + 16.0f, bounds.y + 44.0f, 112.0f, 112.0f});

        const float columnW = 218.0f;
        const float rowH = 40.0f;
        const float top = bounds.y + 42.0f;
        const float colA = bounds.x + 148.0f;
        const float colB = colA + columnW + 20.0f;
        const float colC = colB + columnW + 20.0f;

        drawSlider(ParamId::time, "Time rate  (CC 1)", {colA, top, columnW, rowH}, kRateAccent);
        drawSlider(ParamId::pitch, "Pitch rate  (CC 2)", {colA, top + rowH, columnW, rowH}, kRateAccent);
        drawSwitch(ParamId::hold, "Hold reference", {colA, top + 2.0f * rowH, columnW, rowH}, kRateAccent,
                   "Off", "On");

        drawSlider(ParamId::mix, "Mix", {colB, top, columnW, rowH}, kRateAccent);
        drawSlider(ParamId::level, "Output  (CC 7)", {colB, top + rowH, columnW, rowH}, kRateAccent);
        drawSlider(ParamId::width, "Stereo width", {colB, top + 2.0f * rowH, columnW, rowH}, kRateAccent);

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(colC, top - 4.0f, "Time is a rate: 1.00x is unity, 0.50x is half", nullptr);
        text(colC, top + 9.0f, "speed, so the output is twice as long. It stops", nullptr);
        text(colC, top + 22.0f, "at 1.00x because a live input cannot be read", nullptr);
        text(colC, top + 35.0f, "faster than it arrives.", nullptr);
        text(colC, top + 53.0f, "Pitch rate is read speed, independent of time:", nullptr);
        text(colC, top + 66.0f, "2.00x is an octave up at the same duration.", nullptr);
        text(colC, top + 84.0f, "Hold stops the reference playhead, so a passage", nullptr);
        text(colC, top + 97.0f, "sustains and the splice keeps re-anchoring it.", nullptr);
    }

    // Time and pitch as a two-hand readout on a single dial: the hands show
    // the ratio directly, which is the thing the algorithm actually trades.
    void drawRatioDial(const Rect bounds)
    {
        const auto& t = theme();
        const float cx = bounds.x + bounds.w * 0.5f;
        const float cy = bounds.y + bounds.h * 0.5f;
        const float radius = bounds.w * 0.5f;

        beginPath();
        fc(t.panel);
        circle(cx, cy, radius);
        fill();

        beginPath();
        sc(t.controlTrack);
        strokeWidth(6.0f);
        arc(cx, cy, radius - 6.0f, 0.0f, 2.0f * kPi, CW);
        stroke();

        const float angleTime = 0.75f * kPi
            + 1.5f * kPi * normalizedValue(idx(ParamId::time), values_[idx(ParamId::time)]);
        const float anglePitch = 0.75f * kPi
            + 1.5f * kPi * normalizedValue(idx(ParamId::pitch), values_[idx(ParamId::pitch)]);

        // Pitch hand behind, time hand in front.
        drawDialHand(anglePitch, kSpliceAccent, 4.0f, cx, cy, radius);
        drawDialHand(angleTime, kRateAccent, 6.0f, cx, cy, radius);

        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.2fx", static_cast<double>(values_[idx(ParamId::time)]));
        fc(t.textPrimary);
        fontSize(20.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy - 8.0f, buffer, nullptr);

        std::snprintf(buffer, sizeof(buffer), "%.2fx", static_cast<double>(values_[idx(ParamId::pitch)]));
        fillColor(kSpliceAccent.r, kSpliceAccent.g, kSpliceAccent.b, 255);
        fontSize(13.0f);
        text(cx, cy + 9.0f, buffer, nullptr);

        fc(t.textDim);
        fontSize(9.0f);
        text(cx, cy + 26.0f, "time / pitch", nullptr);

        // Range labels so the dial is readable without the manual.
        fc(t.textDisabled);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(cx, cy - radius - 12.0f, "1.0x", nullptr);
        text(cx, cy + radius + 2.0f, "0.25x", nullptr);
    }

    void drawDialHand(const float angle, const Accent accent, const float width,
                      const float cx, const float cy, const float radius)
    {
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 235);
        circle(cx + (radius - 14.0f) * std::cos(angle),
               cy + (radius - 14.0f) * std::sin(angle), width * 0.5f);
        fill();
    }

    void drawAnalysisPanel(const Rect bounds)
    {
        drawPanel(bounds, "ANALYSIS", kSpliceAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 36.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::threshold, "Threshold (extremum floor)", {x, y, w, rowH}, kSpliceAccent);
        y += rowH;

        drawKeyframeMeter({x, y, w, 130.0f});
        y += 138.0f;

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Density is measured live. Tightly packed", nullptr);
        text(x, y + 13.0f, "keyframes mean dense, bright content; wide", nullptr);
        text(x, y + 26.0f, "spacing means sparse or sustained.", nullptr);
        text(x, y + 39.0f, "A higher threshold discards low-amplitude", nullptr);
        text(x, y + 52.0f, "detail, which reads as a low-pass because", nullptr);
        text(x, y + 65.0f, "high frequencies tend to be quieter.", nullptr);
        text(x, y + 83.0f, "Analysis runs on the mid channel, so both", nullptr);
        text(x, y + 96.0f, "sides share one sparse time base and the", nullptr);
        text(x, y + 109.0f, "stereo image survives a splice.", nullptr);
    }

    // Keyframe density readout. A horizontal bar on a log-ish scale, because
    // the useful range spans two orders of magnitude between a bass note and
    // a cymbal.
    void drawKeyframeMeter(const Rect bounds)
    {
        const auto& t = theme();
        const float rate = std::max(value(ParamId::outDensity), 0.0f);
        const float norm = clampf(std::log10(std::max(rate, 1.0f)) / 4.0f, 0.0f, 1.0f);

        fc(t.textDim);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(bounds.x, bounds.y, "Keyframe density", nullptr);

        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.0f per second", static_cast<double>(rate));
        fc(t.textPrimary);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(bounds.x + bounds.w, bounds.y, buffer, nullptr);

        const float trackY = bounds.y + 18.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(bounds.x, trackY, bounds.w, 12.0f, 3.0f);
        fill();
        if (norm > 0.002f)
        {
            beginPath();
            fillColor(kSpliceAccent.r, kSpliceAccent.g, kSpliceAccent.b, 255);
            roundedRect(bounds.x, trackY, bounds.w * norm, 12.0f, 3.0f);
            fill();
        }

        // Decade ticks at 1, 10, 100 and 1000 per second.
        fc(t.textDisabled);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        for (const float decade : {0.0f, 0.25f, 0.5f, 0.75f})
        {
            const float px = bounds.x + bounds.w * decade;
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(px, trackY + 13.0f);
            lineTo(px, trackY + 17.0f);
            stroke();
        }

        // Below the meter, tick marks whose spacing is drawn from the live
        // rate: the visual claim the algorithm is making about the signal.
        // The rate is mapped logarithmically over the span the method cares
        // about (about 20 to 2000 keyframes per second, i.e. a sparse bass note
        // through to dense cymbal material) and clamped so the ticks stay
        // resolvable on screen. A linear mapping would collapse every real
        // reading into a solid line at the left edge.
        const float stripY = bounds.y + 46.0f;
        const float stripH = 40.0f;
        beginPath();
        fc(t.panel);
        roundedRect(bounds.x, stripY, bounds.w, stripH, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(bounds.x, stripY, bounds.w, stripH, laf::kRadiusSmall);
        stroke();

        // Intervals between ticks, in samples, from one keyframe per sample up
        // to one every 128 samples.
        const float samplesPerKeyframe = clampf(kSr / std::max(rate, 1.0f), 1.0f, 128.0f);
        const float spacing = (samplesPerKeyframe / 128.0f) * (bounds.w / 14.0f);
        for (float px = bounds.x + spacing * 0.5f; px < bounds.x + bounds.w; px += spacing)
        {
            beginPath();
            fillColor(kSpliceAccent.r, kSpliceAccent.g, kSpliceAccent.b, 210);
            circle(px, stripY + stripH * 0.5f, 1.8f);
            fill();
        }

        fc(t.textDisabled);
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(bounds.x + 4.0f, stripY - 6.0f, "spacing encodes bandwidth", nullptr);
    }

    void drawSplicePanel(const Rect bounds)
    {
        drawPanel(bounds, "SPLICE", kSpliceAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 36.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::splice, "Leash length K (keyframes)", {x, y, w, rowH}, kSpliceAccent);
        y += rowH;
        drawSlider(ParamId::maxSplice, "Max splice", {x, y, w, rowH}, kSpliceAccent);
        y += rowH + 8.0f;

        drawLeashDiagram({x, y, w, 84.0f});
        y += 92.0f;

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "Drift %+d keyframes, leash %d.",
                      static_cast<int>(std::lround(value(ParamId::outDrift))),
                      static_cast<int>(std::lround(value(ParamId::splice))));
        text(x, y, buffer, nullptr);
        text(x, y + 13.0f, "K is a macro over splice duration: raising it", nullptr);
        text(x, y + 26.0f, "lengthens the crossfade but keeps its adaptation", nullptr);
        text(x, y + 39.0f, "to transients. Max Splice caps a long stretch", nullptr);
        text(x, y + 52.0f, "through sparse material.", nullptr);
        text(x, y + 72.0f, "When the playhead reaches the end of its", nullptr);
        text(x, y + 85.0f, "leash a splice pulls it back to the reference.", nullptr);
        text(x, y + 98.0f, "Hold stops the reference instead, and the", nullptr);
        text(x, y + 111.0f, "passage sustains indefinitely.", nullptr);
    }

    // The jogger and the dog: reference playhead fixed at the left of the bar,
    // the audio playhead drawn at its live drift, the leash reaching K ahead.
    void drawLeashDiagram(const Rect bounds)
    {
        const auto& t = theme();
        const float y = bounds.y + bounds.h * 0.5f;

        beginPath();
        fc(t.panel);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusSmall);
        fill();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(bounds.x, bounds.y, bounds.w, bounds.h, laf::kRadiusSmall);
        stroke();

        const float leash = std::max(value(ParamId::splice), 2.0f);
        const float drift = clampf(value(ParamId::outDrift), -64.0f, 64.0f);
        const float x0 = bounds.x + 16.0f;
        const float span = bounds.w - 32.0f;

        // Reference playhead and its leash end, K keyframes ahead.
        beginPath();
        sc(t.textDisabled);
        strokeWidth(1.5f);
        moveTo(x0, bounds.y + 8.0f);
        lineTo(x0, bounds.y + bounds.h - 8.0f);
        stroke();

        beginPath();
        sc(t.accentDim);
        strokeWidth(1.5f);
        moveTo(x0, y);
        lineTo(x0 + span, y);
        stroke();

        // Audio playhead at its live drift, in the same keyframe units.
        const float px = clampf(x0 + span * 0.5f * (drift / leash), bounds.x + 4.0f,
                                bounds.x + bounds.w - 4.0f);
        beginPath();
        strokeColor(kSpliceAccent.r, kSpliceAccent.g, kSpliceAccent.b, 255);
        strokeWidth(2.0f);
        moveTo(px, bounds.y + 6.0f);
        lineTo(px, bounds.y + bounds.h - 6.0f);
        stroke();

        beginPath();
        fillColor(holding() ? t.warning.r : kSpliceAccent.r, kSpliceAccent.g, kSpliceAccent.b, 255);
        circle(px, y, 4.0f);
        fill();

        fc(t.textDisabled);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x0, bounds.y + 12.0f, "ref", nullptr);
        text(bounds.x + bounds.w - 12.0f, bounds.y + 12.0f, "+K", nullptr);
        text(px, bounds.y + bounds.h - 10.0f, "play", nullptr);
    }

    void drawOutputPanel(const Rect bounds)
    {
        drawPanel(bounds, "OUTPUT", kOutAccent);

        const float x = bounds.x + 14.0f;
        const float w = bounds.w - 28.0f;
        const float rowH = 36.0f;
        float y = bounds.y + 42.0f;

        drawSlider(ParamId::mix, "Mix", {x, y, w, rowH}, kOutAccent);
        y += rowH;
        drawSlider(ParamId::width, "Stereo width", {x, y, w, rowH}, kOutAccent);
        y += rowH;
        drawSlider(ParamId::level, "Output (CC 7)", {x, y, w, rowH}, kOutAccent);
        y += rowH + 8.0f;

        const auto& t = theme();
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "Reported latency %d samples,",
                      static_cast<int>(std::lround(value(ParamId::outLatency))));
        text(x, y, buffer, nullptr);
        text(x, y + 13.0f, "constant, dry path delayed to match.", nullptr);
        text(x, y + 32.0f, "Time is a rate: 0.50x is half speed, so the", nullptr);
        text(x, y + 45.0f, "output is twice as long. It stops at 1.00x", nullptr);
        text(x, y + 58.0f, "because a live input cannot be read faster", nullptr);
        text(x, y + 71.0f, "than it arrives.", nullptr);
        text(x, y + 90.0f, "Pitch ratio is Pitch / Time. A long stretch", nullptr);
        text(x, y + 103.0f, "through sparse material repeats audibly;", nullptr);
        text(x, y + 116.0f, "dense layered material stretches cleanly.", nullptr);
        text(x, y + 136.0f, "CC 1 time  2 pitch  7 output", nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 24> sliders_ {};
    std::array<Switch, 4> switches_ {};
    std::size_t sliderCount_ = 0;
    std::size_t switchCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyframeUI)
};

UI* createUI()
{
    return new KeyframeUI();
}

END_NAMESPACE_DISTRHO