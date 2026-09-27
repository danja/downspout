#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "quefrency_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::quefrency::kEstimatorNames;
using downspout::quefrency::kParameterCount;
using downspout::quefrency::kParameterSpecs;
using downspout::quefrency::ParamId;

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct Accent { int r, g, b; };

constexpr Accent kFormantAccent {198, 132, 58};  // amber (magneto DRIVE)
constexpr Accent kExciteAccent {176, 88, 62};    // hot metal (magneto ENGINE)
constexpr Accent kFrameAccent {92, 140, 156};    // cold steel (magneto EXHAUST)
constexpr Accent kOutAccent {124, 148, 104};     // olive (magneto LISTEN)

[[nodiscard]] float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }
[[nodiscard]] std::uint32_t idx(ParamId id) { return static_cast<std::uint32_t>(id); }

[[nodiscard]] float normalizedValue(std::uint32_t p, float v)
{
    const auto& s = kParameterSpecs[p];
    if (s.maximum <= s.minimum) return 0.0f;
    return clampf((v - s.minimum) / (s.maximum - s.minimum), 0.0f, 1.0f);
}

std::string formatValue(std::uint32_t p, float v)
{
    char buf[64];
    switch (static_cast<ParamId>(p))
    {
    case ParamId::formantShift:
    case ParamId::pitchShift:
        std::snprintf(buf, sizeof(buf), "%+.1f st", static_cast<double>(v));
        break;
    case ParamId::formantDepth:
    case ParamId::harmonicDepth:
        std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(v));
        break;
    case ParamId::formantTilt:
        std::snprintf(buf, sizeof(buf), "%+.1f dB/oct", static_cast<double>(v));
        break;
    case ParamId::pitchFine:
        std::snprintf(buf, sizeof(buf), "%+.0f ct", static_cast<double>(v));
        break;
    case ParamId::freqShift:
        std::snprintf(buf, sizeof(buf), "%+.0f Hz", static_cast<double>(v));
        break;
    case ParamId::lifter:
        std::snprintf(buf, sizeof(buf), "%.1f ms", static_cast<double>(v));
        break;
    case ParamId::mix:
        std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(clampf(v, 0.0f, 1.0f) * 100.0f));
        break;
    case ParamId::output:
        std::snprintf(buf, sizeof(buf), "%+.1f dB", static_cast<double>(v));
        break;
    case ParamId::outLatency:
        std::snprintf(buf, sizeof(buf), "%.0f spl", static_cast<double>(v));
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(v));
        break;
    }
    return buf;
}

}  // namespace

class QuefrencyUI : public UI {
public:
    QuefrencyUI()
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
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values_.size()) { values_[index] = value; repaint(); }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        sliderCount_ = 0;
        stepperCount_ = 0;
        drawBackground(width, height);
        drawHeader({24.0f, 20.0f, width - 48.0f, 72.0f});
        drawSplitStrip({24.0f, 108.0f, width - 48.0f, 172.0f});
        const float columnW = (width - 48.0f - 32.0f) / 3.0f;
        const float columnY = 296.0f;
        const float columnH = height - columnY - 24.0f;
        drawFormantPanel({24.0f, columnY, columnW, columnH});
        drawExcitePanel({24.0f + columnW + 16.0f, columnY, columnW, columnH});
        drawOutputPanel({24.0f + 2.0f * (columnW + 16.0f), columnY, columnW, columnH});
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        if (!ev.press) { activeSlider_ = -1; return false; }
        const float mx = ev.pos.getX(), my = ev.pos.getY();
        if (themeRect_.contains(mx, my)) { darkTheme_ = !darkTheme_; repaint(); return true; }
        for (std::size_t i = 0; i < stepperCount_; ++i)
        {
            const Stepper& s = steppers_[i];
            if (!s.bounds.contains(mx, my)) continue;
            step(s.parameter, (mx < s.bounds.x + s.bounds.w * 0.5f) ? -1 : 1);
            return true;
        }
        for (std::size_t i = 0; i < sliderCount_; ++i)
            if (sliders_[i].bounds.contains(mx, my))
            {
                activeSlider_ = static_cast<int>(sliders_[i].parameter);
                setFromMouse(sliders_[i], mx);
                return true;
            }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0) return false;
        for (std::size_t i = 0; i < sliderCount_; ++i)
            if (static_cast<int>(sliders_[i].parameter) == activeSlider_)
            {
                setFromMouse(sliders_[i], ev.pos.getX());
                return true;
            }
        return false;
    }

private:
    struct Slider { std::uint32_t parameter = 0; Rect bounds {}; };
    struct Stepper { std::uint32_t parameter = 0; Rect bounds {}; };

    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }
    [[nodiscard]] float value(ParamId id) const { return values_[idx(id)]; }

    void commit(std::uint32_t p, float raw)
    {
        const auto& spec = kParameterSpecs[p];
        float v = clampf(raw, spec.minimum, spec.maximum);
        if (spec.integer) v = std::round(v);
        values_[p] = v;
        setParameterValue(p, v);
        repaint();
    }

    void step(std::uint32_t p, int dir)
    {
        const auto& spec = kParameterSpecs[p];
        float next = values_[p] + static_cast<float>(dir);
        const float span = spec.maximum - spec.minimum + 1.0f;
        if (next < spec.minimum) next += span;
        if (next > spec.maximum) next -= span;
        commit(p, next);
    }

    void setFromMouse(const Slider& s, float mx)
    {
        const auto& spec = kParameterSpecs[s.parameter];
        const float n = clampf((mx - s.bounds.x) / std::max(1.0f, s.bounds.w), 0.0f, 1.0f);
        commit(s.parameter, spec.minimum + n * (spec.maximum - spec.minimum));
    }

    void remember(std::uint32_t p, Rect b)
    {
        if (sliderCount_ < sliders_.size()) sliders_[sliderCount_++] = {p, b};
    }

    void drawBackground(float w, float h)
    {
        const auto& t = theme();
        beginPath(); fc(t.background); rect(0.0f, 0.0f, w, h); fill();
        beginPath(); fc(t.panel); rect(0.0f, 0.0f, w, 96.0f); fill();
    }

    void drawHeader(Rect b)
    {
        const auto& t = theme();
        fc(t.textPrimary); fontSize(30.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x, b.y, "Quefrency", nullptr);
        fc(t.textDim); fontSize(15.0f);
        text(b.x + 140.0f, b.y + 10.0f, "cepstral formant and harmonic shifter", nullptr);
        fc(t.textDim); fontSize(12.0f);
        text(b.x, b.y + 44.0f, "envelope and excitation split at the lifter, shifted apart, multiplied back", nullptr);

        // Latency readout (live from the processor, as in magneto's tachometer).
        char lat[48];
        std::snprintf(lat, sizeof(lat), "LAT %.0f", static_cast<double>(value(ParamId::outLatency)));
        fc(t.textPrimary); fontSize(13.0f); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(b.x + b.w - 88.0f, b.y + 18.0f, lat, nullptr);

        themeRect_ = {b.x + b.w - 74.0f, b.y + 4.0f, 74.0f, 26.0f};
        beginPath(); fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall); fill();
        fc(t.textDim); fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f,
             darkTheme_ ? "DARK" : "LIGHT", nullptr);
    }

    void drawPanel(Rect b, const char* title, Accent a)
    {
        const auto& t = theme();
        beginPath(); fc(t.surface);
        roundedRect(b.x, b.y, b.w, b.h, laf::kRadiusPanel); fill();
        beginPath(); fillColor(a.r, a.g, a.b, 255);
        roundedRect(b.x, b.y, b.w, 32.0f, laf::kRadiusPanel); fill();
        beginPath(); fillColor(a.r, a.g, a.b, 255);
        rect(b.x, b.y + 20.0f, b.w, 12.0f); fill();
        fillColor(250, 248, 242, 255); fontSize(14.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(b.x + 12.0f, b.y + 16.0f, title, nullptr);
    }

    void drawSlider(ParamId id, const char* label, Rect b, Accent a, bool enabled = true)
    {
        const auto& t = theme();
        const std::uint32_t p = idx(id);
        const float n = normalizedValue(p, values_[p]);
        fc(enabled ? t.textDim : t.textDisabled); fontSize(12.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x, b.y, label, nullptr);
        fc(enabled ? t.textPrimary : t.textDisabled); textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(b.x + b.w, b.y, formatValue(p, values_[p]).c_str(), nullptr);
        const float trackY = b.y + 17.0f;
        beginPath(); fc(t.controlTrack); roundedRect(b.x, trackY, b.w, 10.0f, 3.0f); fill();
        if (n > 0.002f)
        {
            beginPath();
            if (enabled) fillColor(a.r, a.g, a.b, 255); else fc(t.textDisabled);
            roundedRect(b.x, trackY, b.w * n, 10.0f, 3.0f); fill();
        }
        if (enabled) remember(p, {b.x, trackY - 9.0f, b.w, 28.0f});
    }

    void drawStepper(ParamId id, const char* label, Rect b, const char* const* names, bool enabled = true)
    {
        const auto& t = theme();
        const std::uint32_t p = idx(id);
        fc(enabled ? t.textDim : t.textDisabled); fontSize(12.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x, b.y, label, nullptr);
        const Rect box {b.x, b.y + 15.0f, b.w, 22.0f};
        beginPath(); fc(enabled ? t.buttonFace : t.controlTrack);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall); fill();
        beginPath(); sc(t.border); strokeWidth(1.0f);
        roundedRect(box.x, box.y, box.w, box.h, laf::kRadiusSmall); stroke();
        const int sel = std::clamp(static_cast<int>(std::lround(values_[p])), 0, 1);
        fc(enabled ? t.textPrimary : t.textDisabled); fontSize(12.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, names[sel], nullptr);
        fc(enabled ? t.textDim : t.textDisabled); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);
        if (enabled && stepperCount_ < steppers_.size()) steppers_[stepperCount_++] = {p, box};
    }

    void drawSplitStrip(Rect b)
    {
        drawPanel(b, "SPLIT — envelope x excitation", kFormantAccent);
        drawWarpCurve({b.x + 16.0f, b.y + 44.0f, 112.0f, 112.0f});
        const float columnW = 218.0f, rowH = 40.0f, top = b.y + 42.0f;
        const float colA = b.x + 148.0f, colB = colA + columnW + 20.0f, colC = colB + columnW + 20.0f;
        drawSlider(ParamId::formantShift, "Formant shift (CC 70)", {colA, top, columnW, rowH}, kFormantAccent);
        drawSlider(ParamId::pitchShift, "Pitch shift (CC 73)", {colA, top + rowH, columnW, rowH}, kFormantAccent);
        drawSlider(ParamId::freqShift, "Freq shift Hz (CC 75)", {colA, top + 2.0f * rowH, columnW, rowH}, kFormantAccent);
        drawSlider(ParamId::mix, "Mix (CC 79)", {colB, top, columnW, rowH}, kFormantAccent);
        drawSlider(ParamId::output, "Output dB (CC 80)", {colB, top + rowH, columnW, rowH}, kFormantAccent);
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(colB, top + 2.0f * rowH + 4.0f, "Played from here; tuned below.", nullptr);
        text(colC, top, "CC 70-80 map to params in order;", nullptr);
        text(colC, top + 13.0f, "64 selects the default. A CC holds", nullptr);
        text(colC, top + 26.0f, "until the host moves that control.", nullptr);
        text(colC, top + 45.0f, "Spectral params take effect at", nullptr);
        text(colC, top + 58.0f, "the next hop; mix and output act", nullptr);
        text(colC, top + 71.0f, "at once. Latency is compensated", nullptr);
        text(colC, top + 84.0f, "by the host from the LAT readout.", nullptr);
    }

    // Envelope warp transfer: output bin (x) reads the envelope at input bin
    // x / ratio (y), log-frequency axes. Unity diagonal for reference.
    void drawWarpCurve(Rect b)
    {
        const auto& t = theme();
        beginPath(); fc(t.panel);
        roundedRect(b.x, b.y, b.w, b.h, 2.0f); fill();
        const float ratio = std::exp2(value(ParamId::formantShift) / 12.0f);
        const float pad = 12.0f;
        const float x0 = b.x + pad, x1 = b.x + b.w - pad;
        const float y0 = b.y + b.h - pad, y1 = b.y + pad;
        constexpr float kMinLog = 1.30103f;  // log10(20)
        constexpr float kMaxLog = 4.30103f;  // log10(20000)
        auto mapX = [&](float outLog) { return x0 + (outLog - kMinLog) / (kMaxLog - kMinLog) * (x1 - x0); };
        auto mapY = [&](float inLog) { return y0 - (inLog - kMinLog) / (kMaxLog - kMinLog) * (y0 - y1); };
        beginPath(); sc(t.border); strokeWidth(1.0f);
        moveTo(x0, y0); lineTo(mapX(kMaxLog), mapY(kMaxLog)); stroke();
        beginPath();
        strokeColor(kFormantAccent.r, kFormantAccent.g, kFormantAccent.b, 255);
        strokeWidth(2.0f);
        bool pen = false;
        for (int i = 0; i <= 64; ++i)
        {
            const float outLog = kMinLog + (kMaxLog - kMinLog) * i / 64.0f;
            const float inLog = outLog - std::log10(std::max(ratio, 1e-6f));
            const float clamped = clampf(inLog, kMinLog, kMaxLog);
            const float px = mapX(outLog), py = mapY(clamped);
            if (!pen) { moveTo(px, py); pen = true; }
            else lineTo(px, py);
        }
        stroke();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text((x0 + x1) * 0.5f, b.y + b.h - 5.0f, "envelope warp", nullptr);
    }

    void drawFormantPanel(Rect b)
    {
        drawPanel(b, "FORMANT", kFormantAccent);
        const float x = b.x + 14.0f, w = b.w - 28.0f, rowH = 36.0f;
        float y = b.y + 42.0f;
        drawSlider(ParamId::formantShift, "Shift (semitones)", {x, y, w, rowH}, kFormantAccent); y += rowH;
        drawSlider(ParamId::formantDepth, "Depth (0 flat, 200 wide)", {x, y, w, rowH}, kFormantAccent); y += rowH;
        drawSlider(ParamId::formantTilt, "Tilt per octave", {x, y, w, rowH}, kFormantAccent); y += rowH;
        drawSlider(ParamId::lifter, "Lifter (split point)", {x, y, w, rowH}, kFormantAccent); y += rowH;
        drawStepper(ParamId::estimator, "Estimator", {x, y, w, rowH}, kEstimatorNames.data()); y += rowH + 8.0f;
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Lifter must sit below the shortest", nullptr);
        text(x, y + 13.0f, "pitch period or harmonics leak", nullptr);
        text(x, y + 26.0f, "into the envelope and move.", nullptr);
    }

    void drawExcitePanel(Rect b)
    {
        drawPanel(b, "EXCITATION", kExciteAccent);
        const float x = b.x + 14.0f, w = b.w - 28.0f, rowH = 36.0f;
        float y = b.y + 42.0f;
        drawSlider(ParamId::pitchShift, "Pitch (semitones)", {x, y, w, rowH}, kExciteAccent); y += rowH;
        drawSlider(ParamId::pitchFine, "Fine (cents)", {x, y, w, rowH}, kExciteAccent); y += rowH;
        drawSlider(ParamId::freqShift, "Frequency (Hz offset)", {x, y, w, rowH}, kExciteAccent); y += rowH;
        drawSlider(ParamId::harmonicDepth, "Depth (0 breathy, 200 buzzy)", {x, y, w, rowH}, kExciteAccent); y += rowH + 8.0f;
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Pitch keeps notes harmonic;", nullptr);
        text(x, y + 13.0f, "Hz offset makes them inharmonic.", nullptr);
        text(x, y + 26.0f, "Peak regions move; transients smear.", nullptr);
    }

    void drawOutputPanel(Rect b)
    {
        drawPanel(b, "OUTPUT", kOutAccent);
        const float x = b.x + 14.0f, w = b.w - 28.0f, rowH = 36.0f;
        float y = b.y + 42.0f;
        drawSlider(ParamId::mix, "Mix (dry delayed to match)", {x, y, w, rowH}, kOutAccent); y += rowH;
        drawSlider(ParamId::output, "Output gain", {x, y, w, rowH}, kOutAccent); y += rowH + 8.0f;
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        char lat[64];
        std::snprintf(lat, sizeof(lat), "Latency %.0f spl, reported to host.",
                      static_cast<double>(value(ParamId::outLatency)));
        text(x, y, lat, nullptr);
        text(x, y + 13.0f, "2047 at 50 kHz and below,", nullptr);
        text(x, y + 26.0f, "4095 above. Dry matches wet, so", nullptr);
        text(x, y + 39.0f, "half mix does not comb.", nullptr);
        text(x, y + 52.0f, "CC holds till the host moves it.", nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 24> sliders_ {};
    std::array<Stepper, 4> steppers_ {};
    std::size_t sliderCount_ = 0, stepperCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuefrencyUI)
};

UI* createUI() { return new QuefrencyUI(); }

END_NAMESPACE_DISTRHO
