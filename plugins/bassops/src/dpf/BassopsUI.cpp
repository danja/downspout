#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamDuckDepth = 0,
    kParamAttackMs,
    kParamReleaseMs,
    kParamCutoffHz,
    kParamInputLevel,
    kParamScLevel,
    kParamDuckGain,
    kParamOutputLevel,
    kParameterCount
};

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    const char* unit;
    const char* hint;
    float       min;
    float       max;
    bool        logScale;
};

constexpr std::array<SliderDef, 4> kSliders = {{
    {kParamDuckDepth, "Duck Depth", "%",  "Amount of gain reduction when sidechain fires",  0.0f,    100.0f, false},
    {kParamAttackMs,  "Attack",     "ms", "Envelope follower attack time",                  1.0f,    500.0f, true},
    {kParamReleaseMs, "Release",    "ms", "Envelope follower release time",                 10.0f,  2000.0f, true},
    {kParamCutoffHz,  "M/S Cutoff", "Hz", "LP (mid) / HP (side) crossover frequency",      50.0f,  5000.0f, true},
}};

// Meter definitions (index, label, colour R/G/B)
struct MeterDef {
    uint32_t    index;
    const char* label;
    const char* sublabel;
    uint8_t     r, g, b;
    bool        isDb;        // true = dBFS display; false = linear 0-1 display
    bool        isInverted;  // true = draw from right (gain reduction)
};

constexpr std::array<MeterDef, 4> kMeters = {{
    {kParamInputLevel,  "Input",     "pre-duck",   100, 160, 220, true,  false},
    {kParamScLevel,     "Sidechain", "envelope",   210, 140,  50, false, false},
    {kParamDuckGain,    "Reduction", "gain cut",   200,  70,  60, false, true},
    {kParamOutputLevel, "Output",    "post-EQ",     80, 190, 120, true,  false},
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] float toNorm(const SliderDef& def, float value) noexcept
{
    if (def.logScale) {
        const float logMin = std::log(def.min);
        const float logMax = std::log(def.max);
        return clampf((std::log(std::max(value, def.min)) - logMin) / (logMax - logMin), 0.0f, 1.0f);
    }
    return clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
}

[[nodiscard]] float fromNorm(const SliderDef& def, float t) noexcept
{
    t = clampf(t, 0.0f, 1.0f);
    if (def.logScale) {
        const float logMin = std::log(def.min);
        const float logMax = std::log(def.max);
        return std::exp(logMin + t * (logMax - logMin));
    }
    return def.min + t * (def.max - def.min);
}

[[nodiscard]] std::string formatSliderValue(const SliderDef& def, float value)
{
    char buf[32];
    if (def.index == kParamDuckDepth) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value);
    } else if (def.index == kParamCutoffHz) {
        if (value >= 1000.0f) {
            std::snprintf(buf, sizeof(buf), "%.2f kHz", value / 1000.0f);
        } else {
            std::snprintf(buf, sizeof(buf), "%.0f Hz", value);
        }
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f ms", value);
    }
    return buf;
}

// Linear amplitude 0-1 → meter fill position 0-1, using -60 dBFS floor
[[nodiscard]] float levelToMeter(float linear) noexcept
{
    if (linear < 1e-6f) { return 0.0f; }
    const float dB = 20.0f * std::log10(linear);
    return clampf((dB + 60.0f) / 60.0f, 0.0f, 1.0f);
}

[[nodiscard]] std::string formatDb(float linear)
{
    char buf[16];
    if (linear < 1e-6f) {
        std::snprintf(buf, sizeof(buf), "-inf");
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f dB", 20.0f * std::log10(linear));
    }
    return buf;
}

[[nodiscard]] std::string formatMeterValue(const MeterDef& def, float value)
{
    char buf[24];
    if (def.isInverted) {
        // duckGain: 0=full duck, 1=no duck; show gain reduction in dB
        const float gain = clampf(value, 0.0001f, 1.0f);
        const float reductionDb = -20.0f * std::log10(gain);
        std::snprintf(buf, sizeof(buf), "%.1f dB", reductionDb);
    } else if (def.isDb) {
        return formatDb(value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
    }
    return buf;
}

// Fill fraction for a meter given raw parameter value
[[nodiscard]] float meterFill(const MeterDef& def, float value) noexcept
{
    if (def.isInverted) {
        // gain 1→no fill (no reduction), gain 0→full fill
        return clampf(1.0f - value, 0.0f, 1.0f);
    }
    if (def.isDb) {
        return levelToMeter(value);
    }
    return clampf(value, 0.0f, 1.0f);
}

}  // namespace

class BassopsUI : public UI
{
public:
    BassopsUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamDuckDepth]   = 80.0f;
        values_[kParamAttackMs]    = 10.0f;
        values_[kParamReleaseMs]   = 100.0f;
        values_[kParamCutoffHz]    = 200.0f;
        values_[kParamInputLevel]  = 0.0f;
        values_[kParamScLevel]     = 0.0f;
        values_[kParamDuckGain]    = 1.0f;
        values_[kParamOutputLevel] = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < kParameterCount) {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float W   = static_cast<float>(getWidth());
        const float H   = static_cast<float>(getHeight());
        const float pad = 20.0f;

        drawBackground(W, H);
        drawHeader(pad, pad, W - pad * 2.0f, 70.0f);

        const float contentY = pad + 88.0f;
        const float contentH = H - contentY - pad;

        drawControlPanel(pad, contentY, W * 0.55f - pad * 0.5f, contentH);
        drawMeterPanel(W * 0.55f + pad * 0.5f, contentY, W * 0.45f - pad * 1.5f, contentH);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) { return false; }

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            draggingSlider_ = -1;
            return false;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                draggingSlider_ = static_cast<int>(i);
                updateSliderFromX(draggingSlider_, x);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingSlider_ >= 0) {
            updateSliderFromX(draggingSlider_, static_cast<float>(ev.pos.getX()));
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                nudgeSlider(static_cast<int>(i), ev.delta.getY() > 0.0f ? 1.0f : -1.0f);
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect,  kSliders.size()> sliderRects_ {};
    int                                draggingSlider_ = -1;

    void drawBackground(float W, float H)
    {
        beginPath();
        fillColor(10, 14, 20, 255);
        rect(0, 0, W, H);
        fill();
        closePath();

        beginPath();
        fillColor(18, 32, 46, 255);
        rect(0, 0, W, H * 0.28f);
        fill();
        closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(16, 25, 36, 240);
        fill();
        closePath();

        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(235, 240, 245, 255);
        text(x + 20.0f, y + 14.0f, "Bassops", nullptr);

        fontSize(12.0f);
        fillColor(140, 160, 178, 255);
        text(x + 22.0f, y + 46.0f,
             DOWNSPOUT_PLUGIN_VERSION_STRING "  |  Sidechain ducker + mid/side EQ",
             nullptr);
    }

    void drawControlPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(15, 21, 29, 248);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(220, 228, 235, 255);
        text(x + 18.0f, y + 16.0f, "Controls", nullptr);

        const float innerX = x + 18.0f;
        const float innerW = w - 36.0f;
        const float rowH   = 62.0f;
        const float rowGap = 12.0f;

        for (std::size_t i = 0; i < kSliders.size(); ++i) {
            const float ry = y + 44.0f + static_cast<float>(i) * (rowH + rowGap);
            sliderRects_[i] = {innerX, ry + 28.0f, innerW, 18.0f};
            drawSlider(kSliders[i], sliderRects_[i], values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, float value, bool active)
    {
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(155, 172, 187, 255);
        text(rect.x, rect.y - 24.0f, def.label, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(228, 234, 240, 255);
        const std::string valStr = formatSliderValue(def, value);
        text(rect.x + rect.w, rect.y - 24.0f, valStr.c_str(), nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(100, 118, 135, 255);
        text(rect.x, rect.y + 24.0f, def.hint, nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 9.0f);
        fillColor(35, 44, 56, 255);
        fill();
        closePath();

        const float t = toNorm(def, value);
        beginPath();
        roundedRect(rect.x, rect.y, std::max(12.0f, rect.w * t), rect.h, 9.0f);
        fillColor(active ? 220 : 180, active ? 130 : 100, 55, 255);
        fill();
        closePath();
    }

    void drawMeterPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(12, 18, 26, 248);
        fill();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(220, 228, 235, 255);
        text(x + 18.0f, y + 16.0f, "Ducker", nullptr);

        // dBFS tick marks: -60 -40 -20 -10 -6 -3 0
        const float barX = x + 18.0f;
        const float barW = w - 36.0f;
        const float tickY = y + 44.0f;
        drawDbTicks(barX, tickY, barW);

        // 4 meter rows
        const float firstRowY = tickY + 18.0f;
        const float rowH   = 44.0f;
        const float rowGap = 14.0f;

        for (std::size_t i = 0; i < kMeters.size(); ++i) {
            const float ry = firstRowY + static_cast<float>(i) * (rowH + rowGap);
            drawMeter(kMeters[i], barX, ry, barW, 20.0f, values_[kMeters[i].index]);
        }

        // Separator + legend at bottom
        const float legendY = y + h - 38.0f;
        beginPath();
        strokeColor(40, 55, 70, 200);
        strokeWidth(1.0f);
        moveTo(x + 18.0f, legendY - 8.0f);
        lineTo(x + w - 18.0f, legendY - 8.0f);
        stroke();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fillColor(90, 110, 128, 255);
        text(x + 18.0f, legendY + 22.0f, "Reduction shows VCA gain cut in dB", nullptr);
    }

    void drawDbTicks(float x, float y, float w)
    {
        struct Tick { float dB; const char* label; };
        constexpr Tick ticks[] = {
            {-60.0f, "-60"}, {-40.0f, "-40"}, {-20.0f, "-20"},
            {-10.0f, "-10"}, {-6.0f,  "-6"},  { 0.0f,  "0"},
        };

        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);

        for (const Tick& t : ticks) {
            const float xPos = x + w * clampf((t.dB + 60.0f) / 60.0f, 0.0f, 1.0f);

            beginPath();
            strokeColor(50, 65, 80, 200);
            strokeWidth(1.0f);
            moveTo(xPos, y);
            lineTo(xPos, y + 6.0f);
            stroke();
            closePath();

            fillColor(90, 108, 124, 255);
            text(xPos, y + 7.0f, t.label, nullptr);
        }
    }

    void drawMeter(const MeterDef& def, float x, float y, float w, float barH, float value)
    {
        // Label + sub-label
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(200, 212, 222, 255);
        text(x, y, def.label, nullptr);

        fontSize(9.0f);
        fillColor(100, 120, 138, 255);
        text(x, y + 13.0f, def.sublabel, nullptr);

        // Value text on the right
        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(def.r, def.g, def.b, 220);
        const std::string valStr = formatMeterValue(def, value);
        text(x + w, y, valStr.c_str(), nullptr);

        // Bar background
        const float barY = y + barH * 0.15f;  // centre bar in the row
        beginPath();
        roundedRect(x, barY, w, barH * 0.6f, 5.0f);
        fillColor(28, 36, 46, 255);
        fill();
        closePath();

        // Bar fill
        const float fillFrac = meterFill(def, value);
        if (fillFrac > 0.0f) {
            const float fillW = std::max(6.0f, w * fillFrac);

            // Colour gradient: dim at low end, bright at high end
            // For reduction meter: red gets brighter as more gain is cut
            const float brightness = 0.4f + 0.6f * fillFrac;
            const int   rr = static_cast<int>(def.r * brightness);
            const int   gg = static_cast<int>(def.g * brightness);
            const int   bb = static_cast<int>(def.b * brightness);

            beginPath();
            roundedRect(x, barY, fillW, barH * 0.6f, 5.0f);
            fillColor(static_cast<uint8_t>(rr), static_cast<uint8_t>(gg), static_cast<uint8_t>(bb), 255);
            fill();
            closePath();
        }

        // 0 dB / unity marker line for level meters
        if (!def.isInverted) {
            const float markerX = x + w * (def.isDb ? 1.0f : 1.0f);
            beginPath();
            strokeColor(200, 200, 200, 60);
            strokeWidth(1.0f);
            moveTo(markerX, barY - 2.0f);
            lineTo(markerX, barY + barH * 0.6f + 2.0f);
            stroke();
            closePath();
        }
    }

    void updateSliderFromX(int sliderIndex, float mouseX)
    {
        const SliderDef& def  = kSliders[sliderIndex];
        const Rect&      rect = sliderRects_[sliderIndex];
        const float      t    = clampf((mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        commitParameter(def.index, fromNorm(def, t));
    }

    void nudgeSlider(int sliderIndex, float direction)
    {
        const SliderDef& def  = kSliders[sliderIndex];
        const float      step = def.logScale
            ? (def.max - def.min) / 200.0f
            : (def.max - def.min) / 100.0f;
        commitParameter(def.index, values_[def.index] + direction * step);
    }

    void commitParameter(uint32_t index, float value)
    {
        for (const SliderDef& def : kSliders) {
            if (def.index == index) {
                value = clampf(value, def.min, def.max);
                break;
            }
        }
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassopsUI)
};

UI* createUI()
{
    return new BassopsUI();
}

END_NAMESPACE_DISTRHO
