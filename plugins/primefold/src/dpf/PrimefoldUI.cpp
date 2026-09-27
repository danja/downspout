#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "primefold_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::primefold::kGrainNames;
using downspout::primefold::kParameterCount;
using downspout::primefold::kParameterSpecs;
using downspout::primefold::ParamId;

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;
    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct Accent { int r, g, b; };

constexpr Accent kPrimeAccent {198, 132, 58};    // amber (magneto DRIVE)
constexpr Accent kLoopAccent {176, 88, 62};      // hot metal (magneto ENGINE)
constexpr Accent kOutAccent {92, 140, 156};      // cold steel (magneto EXHAUST)
constexpr Accent kMixAccent {124, 148, 104};     // olive (magneto LISTEN)

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
    const auto id = static_cast<ParamId>(p);
    switch (id)
    {
    case ParamId::grain: {
        const int g = static_cast<int>(std::lround(v));
        std::snprintf(buf, sizeof(buf), "%s spl", kGrainNames[static_cast<std::size_t>(std::clamp(g, 0, 2))]);
        break;
    }
    case ParamId::feedback:
    case ParamId::damp:
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(clampf(v, 0.0f, 0.9f) / 0.9f * 100.0f)));
        break;
    case ParamId::outLatency:
        std::snprintf(buf, sizeof(buf), "%d spl", static_cast<int>(std::lround(v)));
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(clampf(v, 0.0f, 1.0f) * 100.0f)));
        break;
    }
    return buf;
}

}  // namespace

class PrimefoldUI : public UI {
public:
    PrimefoldUI()
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
        drawPrimeStrip({24.0f, 108.0f, width - 48.0f, 172.0f});
        const float columnW = (width - 48.0f - 32.0f) / 3.0f;
        const float columnY = 296.0f;
        const float columnH = height - columnY - 24.0f;
        drawPrimesPanel({24.0f, columnY, columnW, columnH});
        drawLoopPanel({24.0f + columnW + 16.0f, columnY, columnW, columnH});
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
        text(b.x, b.y, "Primefold", nullptr);
        fc(t.textDim); fontSize(15.0f);
        text(b.x + 136.0f, b.y + 10.0f, "prime-harmonic feedback shifter", nullptr);
        fc(t.textDim); fontSize(12.0f);
        text(b.x, b.y + 44.0f, "direct 2x 3x 5x, composites via recirculation, bounded loop, explicit delay", nullptr);

        const float lamp = clampf(value(ParamId::outClip), 0.0f, 1.0f);
        const float lampX = b.x + b.w - 188.0f;
        beginPath(); circle(lampX, b.y + 18.0f, 7.0f);
        if (lamp > 0.02f) fillColor(t.danger.r, t.danger.g, t.danger.b, static_cast<uchar>(80 + 175 * lamp));
        else fc(t.accentDim);
        fill();
        fc(lamp > 0.02f ? t.danger : t.textDisabled);
        fontSize(11.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(lampX + 13.0f, b.y + 18.0f, "CLIP", nullptr);

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
        const int sel = std::clamp(static_cast<int>(std::lround(values_[p])), 0, 2);
        fc(enabled ? t.textPrimary : t.textDisabled); fontSize(12.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, names[sel], nullptr);
        fc(enabled ? t.textDim : t.textDisabled); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(box.x + 7.0f, box.y + box.h * 0.5f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(box.x + box.w - 7.0f, box.y + box.h * 0.5f, ">", nullptr);
        if (enabled && stepperCount_ < steppers_.size()) steppers_[stepperCount_++] = {p, box};
    }

    void drawPrimeStrip(Rect b)
    {
        drawPanel(b, "PRIMES — direct pitch voices", kPrimeAccent);
        drawLadder({b.x + 16.0f, b.y + 44.0f, 112.0f, 112.0f});
        const float columnW = 218.0f, rowH = 40.0f, top = b.y + 42.0f;
        const float colA = b.x + 148.0f, colB = colA + columnW + 20.0f, colC = colB + columnW + 20.0f;
        drawSlider(ParamId::level2, "2x voice", {colA, top, columnW, rowH}, kPrimeAccent);
        drawSlider(ParamId::level3, "3x voice", {colA, top + rowH, columnW, rowH}, kPrimeAccent);
        drawSlider(ParamId::level5, "5x voice", {colA, top + 2.0f * rowH, columnW, rowH}, kPrimeAccent);
        drawSlider(ParamId::feedback, "Feedback (composites)", {colB, top, columnW, rowH}, kPrimeAccent);
        drawSlider(ParamId::damp, "Damp (loop brightness)", {colB, top + rowH, columnW, rowH}, kPrimeAccent);
        drawStepper(ParamId::grain, "Grain (latency)", {colB, top + 2.0f * rowH, columnW, rowH}, kGrainNames.data());
        drawSlider(ParamId::dry, "Dry level", {colC, top, columnW, rowH}, kPrimeAccent);
        drawSlider(ParamId::mix, "Mix  (CC 2)", {colC, top + rowH, columnW, rowH}, kPrimeAccent);
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(colC, top + 2.0f * rowH - 4.0f, "4x is 2x fed back; 6x is 2x and", nullptr);
        text(colC, top + 2.0f * rowH + 9.0f, "3x combined; and so on up the", nullptr);
        text(colC, top + 2.0f * rowH + 22.0f, "ladder. CC 1 feedback  7 output", nullptr);
    }

    // Harmonic ladder 1..16: primes lit amber, composites lit dimmer with
    // depth = shortest recirculation path. Mirrors docs/design.md, like the
    // firing diagram mirrors CycleFunctions in magneto.
    void drawLadder(Rect b)
    {
        const auto& t = theme();
        const float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f;
        beginPath(); fc(t.panel); circle(cx, cy, b.w * 0.5f); fill();
        const float fb = clampf(value(ParamId::feedback), 0.0f, 0.9f) / 0.9f;
        constexpr float kPi = 3.14159265358979323846f;
        for (int h = 1; h <= 16; ++h)
        {
            const bool prime = (h == 2 || h == 3 || h == 5);
            // Shortest recirculation depth for composites (BFS over {2,3,5}).
            int depth = 0;
            if (!prime && h > 1)
            {
                int d = h, steps = 0;
                while (d > 1 && steps < 5)  // greedy strip largest prime factor
                {
                    if (d % 5 == 0) d /= 5;
                    else if (d % 3 == 0) d /= 3;
                    else if (d % 2 == 0) d /= 2;
                    else break;
                    ++steps;
                }
                depth = (d == 1) ? steps : 0;
            }
            const float angle = -0.5f * kPi + 2.0f * kPi * static_cast<float>(h - 1) / 16.0f;
            const float r = b.w * 0.5f - 10.0f;
            const float px = cx + r * std::cos(angle), py = cy + r * std::sin(angle);
            const float lit = prime ? 1.0f : (depth > 0 ? clampf(fb * (1.2f - 0.2f * depth), 0.0f, 1.0f) : 0.0f);
            beginPath();
            if (lit > 0.03f) fillColor(kPrimeAccent.r, kPrimeAccent.g, kPrimeAccent.b,
                                       static_cast<uchar>(90 + 165 * lit));
            else fc(t.accentDim);
            circle(px, py, prime ? 6.0f : 4.5f); fill();
        }
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, cy - 7.0f, "harmonic", nullptr);
        text(cx, cy + 7.0f, "ladder", nullptr);
    }

    void drawPrimesPanel(Rect b)
    {
        drawPanel(b, "PRIMES", kPrimeAccent);
        const float x = b.x + 14.0f, w = b.w - 28.0f, rowH = 36.0f;
        float y = b.y + 42.0f;
        drawSlider(ParamId::level2, "2x (octave)", {x, y, w, rowH}, kPrimeAccent); y += rowH;
        drawSlider(ParamId::level3, "3x (twelfth)", {x, y, w, rowH}, kPrimeAccent); y += rowH;
        drawSlider(ParamId::level5, "5x (two octaves+third)", {x, y, w, rowH}, kPrimeAccent); y += rowH;
        drawSlider(ParamId::dry, "Dry", {x, y, w, rowH}, kPrimeAccent); y += rowH;
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y + 6.0f, "Direct voices share one nominal delay,", nullptr);
        text(x, y + 19.0f, "so they stay mutually aligned.", nullptr);
    }

    void drawLoopPanel(Rect b)
    {
        drawPanel(b, "LOOP", kLoopAccent);
        const float x = b.x + 14.0f, w = b.w - 28.0f, rowH = 34.0f;
        float y = b.y + 42.0f;
        drawSlider(ParamId::feedback, "Feedback (CC 1)", {x, y, w, rowH}, kLoopAccent); y += rowH;
        drawSlider(ParamId::damp, "Damp", {x, y, w, rowH}, kLoopAccent); y += rowH;
        drawStepper(ParamId::grain, "Grain window", {x, y, w, rowH}, kGrainNames.data()); y += rowH + 8.0f;
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        char lat[64];
        std::snprintf(lat, sizeof(lat), "Feedforward latency %d spl (reported).",
                      static_cast<int>(std::lround(value(ParamId::outLatency))));
        text(x, y, lat, nullptr);
        text(x, y + 13.0f, "Explicit loop delay 256 spl,", nullptr);
        text(x, y + 26.0f, "never compensated inside.", nullptr);
        text(x, y + 39.0f, "Composites bloom late by", nullptr);
        text(x, y + 52.0f, "k x (grain + 256) per pass.", nullptr);
    }

    void drawOutputPanel(Rect b)
    {
        drawPanel(b, "OUTPUT", kOutAccent);
        const float x = b.x + 14.0f, w = b.w - 28.0f, rowH = 36.0f;
        float y = b.y + 42.0f;
        drawSlider(ParamId::mix, "Mix (CC 2)", {x, y, w, rowH}, kMixAccent); y += rowH;
        drawSlider(ParamId::width, "Stereo width", {x, y, w, rowH}, kMixAccent); y += rowH;
        drawSlider(ParamId::level, "Output (CC 7)", {x, y, w, rowH}, kMixAccent); y += rowH + 8.0f;
        const auto& t = theme();
        fc(t.textDim); fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, y, "Dry carries half a grain (voice", nullptr);
        text(x, y + 13.0f, "average) so dry/wet and the host", nullptr);
        text(x, y + 26.0f, "stay aligned. Loop taps are not.", nullptr);
    }

    std::array<float, kParameterCount> values_ {};
    std::array<Slider, 24> sliders_ {};
    std::array<Stepper, 4> steppers_ {};
    std::size_t sliderCount_ = 0, stepperCount_ = 0;
    Rect themeRect_ {};
    int activeSlider_ = -1;
    bool darkTheme_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrimefoldUI)
};

UI* createUI() { return new PrimefoldUI(); }

END_NAMESPACE_DISTRHO
