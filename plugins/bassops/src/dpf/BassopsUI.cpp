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
    kParamSideShape,
    kParamWet,
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
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    const char* unit;
    float       min, max;
    bool        logScale;
};

constexpr std::array<SliderDef, 6> kSliders = {{
    {kParamDuckDepth, "Duck\nDepth",  "%",   0.0f,    100.0f, false},
    {kParamAttackMs,  "Attack",       "ms",  1.0f,    500.0f, true},
    {kParamReleaseMs, "Release",      "ms",  10.0f,  2000.0f, true},
    {kParamCutoffHz,  "M/S\nCutoff",  "Hz",  50.0f,  5000.0f, true},
    {kParamSideShape, "Side\nShape",  "%",   0.0f,    100.0f, false},
    {kParamWet,       "Wet",          "%",   0.0f,    100.0f, false},
}};

struct MeterDef {
    uint32_t    index;
    const char* label;
    bool        isDb;      // true → dBFS display; false → linear 0-1
    bool        isInverted; // true → fill from top (gain reduction)
    uint8_t     r, g, b;
};

constexpr std::array<MeterDef, 4> kMeters = {{
    {kParamInputLevel,  "In",   true,  false, 100, 160, 220},
    {kParamScLevel,     "Side", false, false, 210, 140,  50},
    {kParamDuckGain,    "Cut",  false, true,  200,  70,  60},
    {kParamOutputLevel, "Out",  true,  false,  80, 190, 120},
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] float toNorm(const SliderDef& def, float value) noexcept
{
    if (def.logScale) {
        const float lMin = std::log(def.min), lMax = std::log(def.max);
        return clampf((std::log(std::max(value, def.min)) - lMin) / (lMax - lMin), 0.0f, 1.0f);
    }
    return clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
}

[[nodiscard]] float fromNorm(const SliderDef& def, float t) noexcept
{
    t = clampf(t, 0.0f, 1.0f);
    if (def.logScale) {
        const float lMin = std::log(def.min), lMax = std::log(def.max);
        return std::exp(lMin + t * (lMax - lMin));
    }
    return def.min + t * (def.max - def.min);
}

[[nodiscard]] std::string fmtSlider(const SliderDef& def, float v)
{
    char buf[32];
    if (def.index == kParamDuckDepth)
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
    else if (def.index == kParamCutoffHz)
        v >= 1000.0f
            ? std::snprintf(buf, sizeof(buf), "%.1fk", v / 1000.0f)
            : std::snprintf(buf, sizeof(buf), "%.0f", v);
    else
        std::snprintf(buf, sizeof(buf), "%.0f", v);
    return buf;
}

[[nodiscard]] float meterFill(const MeterDef& def, float value) noexcept
{
    if (def.isInverted) return clampf(1.0f - value, 0.0f, 1.0f); // gain→reduction
    if (def.isDb) {
        if (value < 1e-6f) return 0.0f;
        return clampf((20.0f * std::log10(value) + 60.0f) / 60.0f, 0.0f, 1.0f);
    }
    return clampf(value, 0.0f, 1.0f);
}

[[nodiscard]] std::string fmtMeter(const MeterDef& def, float value)
{
    char buf[16];
    if (def.isInverted) {
        const float g = clampf(value, 0.0001f, 1.0f);
        std::snprintf(buf, sizeof(buf), "%.0fdB", -20.0f * std::log10(g));
    } else if (def.isDb) {
        if (value < 1e-6f) std::snprintf(buf, sizeof(buf), "-inf");
        else               std::snprintf(buf, sizeof(buf), "%.0f", 20.0f * std::log10(value));
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", value * 100.0f);
    }
    return buf;
}

} // namespace

class BassopsUI : public UI
{
public:
    BassopsUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamDuckDepth]   = 80.0f;
        values_[kParamAttackMs]    = 10.0f;
        values_[kParamReleaseMs]   = 100.0f;
        values_[kParamCutoffHz]    = 200.0f;
        values_[kParamSideShape]   = 0.0f;
        values_[kParamWet]         = 100.0f;
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
        if (index < kParameterCount) { values_[index] = value; repaint(); }
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        constexpr float pad = 18.0f;

        drawBackground(W, H);
        drawHeader(pad, pad, W - pad * 2.0f, 64.0f);

        const float cy = pad + 80.0f;
        const float ch = H - cy - pad;
        const float splitX = W * 0.54f;

        drawSliderPanel(pad, cy, splitX - pad * 1.5f, ch);
        drawMeterPanel(splitX + pad * 0.5f, cy, W - splitX - pad * 1.5f, ch);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        if (!ev.press) { dragging_ = -1; return false; }
        for (int i = 0; i < static_cast<int>(sliderRects_.size()); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                dragging_ = i;
                updateFromY(i, y);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragging_ >= 0) { updateFromY(dragging_, static_cast<float>(ev.pos.getY())); return true; }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        for (int i = 0; i < static_cast<int>(sliderRects_.size()); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                nudge(i, ev.delta.getY() > 0.0f ? 1.0f : -1.0f);
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect,  kSliders.size()> sliderRects_ {};
    int dragging_ = -1;

    // ── drawing helpers ────────────────────────────────────────────────────

    void drawBackground(float W, float H)
    {
        beginPath(); fillColor(9, 13, 19, 255); rect(0, 0, W, H); fill(); closePath();
        beginPath(); fillColor(17, 30, 44, 255); rect(0, 0, W, H * 0.26f); fill(); closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 16.0f); fillColor(15, 23, 33, 240); fill(); closePath();

        fontSize(26.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(232, 238, 245, 255);
        text(x + 18.0f, y + h * 0.38f, "Bassops", nullptr);

        fontSize(11.0f); fillColor(130, 152, 172, 255);
        text(x + 20.0f, y + h * 0.76f,
             DOWNSPOUT_PLUGIN_VERSION_STRING "  |  Sidechain ducker + mid/side EQ", nullptr);
    }

    void drawSliderPanel(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 16.0f); fillColor(14, 20, 28, 248); fill(); closePath();

        fontSize(12.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(210, 220, 230, 255);
        text(x + 16.0f, y + 14.0f, "Controls", nullptr);

        const float innerX = x + 14.0f;
        const float innerW = w - 28.0f;
        const float barH   = h - 100.0f;   // height of the slider track
        const float barY   = y + 42.0f;
        const float colW   = (innerW - 5.0f * 10.0f) / 6.0f;

        for (int i = 0; i < 6; ++i) {
            const float cx = innerX + i * (colW + 10.0f);
            // The interactive hit area is the full column
            sliderRects_[i] = {cx, barY, colW, barH};
            drawVSlider(kSliders[i], cx, barY, colW, barH,
                        values_[kSliders[i].index], dragging_ == i);
        }
    }

    // Draws a vertical slider column.
    // Track fills from bottom up.  Label above, value below.
    void drawVSlider(const SliderDef& def, float x, float y, float w, float h, float value, bool active)
    {
        const float trackW = 18.0f;
        const float trackX = x + (w - trackW) * 0.5f;

        // Label (supports \n for two-line labels)
        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(150, 168, 184, 255);
        // Simple two-line label: split on '\n'
        std::string lbl(def.label);
        const auto nl = lbl.find('\n');
        if (nl != std::string::npos) {
            text(x + w * 0.5f, y,           lbl.substr(0, nl).c_str(), nullptr);
            text(x + w * 0.5f, y + 13.0f,   lbl.substr(nl + 1).c_str(), nullptr);
        } else {
            text(x + w * 0.5f, y + 6.0f, lbl.c_str(), nullptr);
        }

        // Track background
        const float trackTop = y + 28.0f;
        const float trackH   = h - 54.0f;
        beginPath(); roundedRect(trackX, trackTop, trackW, trackH, trackW * 0.5f);
        fillColor(30, 40, 52, 255); fill(); closePath();

        // Fill from bottom up
        const float t     = toNorm(def, value);
        const float fillH = std::max(trackW, trackH * t);
        const float fillY = trackTop + trackH - fillH;
        beginPath(); roundedRect(trackX, fillY, trackW, fillH, trackW * 0.5f);
        fillColor(active ? 220 : 175, active ? 128 : 98, 50, 255);
        fill(); closePath();

        // Unit label inside track (tiny)
        fontSize(9.0f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(120, 136, 150, 180);
        text(trackX + trackW * 0.5f, trackTop + 4.0f, def.unit, nullptr);

        // Value readout below track
        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(225, 232, 240, 255);
        text(x + w * 0.5f, trackTop + trackH + 6.0f, fmtSlider(def, value).c_str(), nullptr);
    }

    void drawMeterPanel(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 16.0f); fillColor(11, 17, 25, 248); fill(); closePath();

        fontSize(12.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(210, 220, 230, 255);
        text(x + 16.0f, y + 14.0f, "Ducker", nullptr);

        const float innerX = x + 14.0f;
        const float innerW = w - 28.0f;
        const float barH   = h - 100.0f;
        const float barY   = y + 42.0f;
        const float colW   = (innerW - 3.0f * 10.0f) / 4.0f;

        for (int i = 0; i < 4; ++i) {
            const float cx = innerX + i * (colW + 10.0f);
            drawVMeter(kMeters[i], cx, barY, colW, barH, values_[kMeters[i].index]);
        }

        // dBFS scale on the left edge of panel
        drawDbScale(innerX, barY, barH);
    }

    void drawVMeter(const MeterDef& def, float x, float y, float w, float h, float value)
    {
        const float barW  = 20.0f;
        const float barX  = x + (w - barW) * 0.5f;
        const float barTop = y + 20.0f;
        const float barH  = h - 44.0f;

        // Label above
        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(190, 205, 218, 255);
        text(x + w * 0.5f, y + 2.0f, def.label, nullptr);

        // Track background
        beginPath(); roundedRect(barX, barTop, barW, barH, barW * 0.5f);
        fillColor(25, 33, 43, 255); fill(); closePath();

        // Fill fraction
        const float fillFrac = meterFill(def, value);
        if (fillFrac > 0.0f) {
            const float fillH = std::max(barW, barH * fillFrac);
            const float fillY = def.isInverted
                ? barTop                              // fills from top for reduction
                : barTop + barH - fillH;              // fills from bottom for levels

            const float brt = 0.45f + 0.55f * fillFrac;
            beginPath(); roundedRect(barX, fillY, barW, fillH, barW * 0.5f);
            fillColor(static_cast<uint8_t>(def.r * brt),
                      static_cast<uint8_t>(def.g * brt),
                      static_cast<uint8_t>(def.b * brt), 255);
            fill(); closePath();
        }

        // 0 dBFS / unity tick
        if (!def.isInverted) {
            const float tickY = barTop;  // top of bar = 0 dBFS
            beginPath(); strokeColor(180, 180, 180, 60); strokeWidth(1.0f);
            moveTo(barX - 2.0f, tickY); lineTo(barX + barW + 2.0f, tickY);
            stroke(); closePath();
        }

        // Value readout below bar
        fontSize(10.0f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(static_cast<uint8_t>(def.r * 0.9f),
                  static_cast<uint8_t>(def.g * 0.9f),
                  static_cast<uint8_t>(def.b * 0.9f), 220);
        text(x + w * 0.5f, barTop + barH + 5.0f, fmtMeter(def, value).c_str(), nullptr);
    }

    // Draws dB tick marks along the left side of the meter panel bars
    void drawDbScale(float x, float y, float h)
    {
        const float barTop = y + 20.0f;
        const float barH   = h - 44.0f;

        struct Tick { float dB; };
        constexpr Tick ticks[] = {{0.0f},{-6.0f},{-12.0f},{-24.0f},{-48.0f}};

        fontSize(8.0f); textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(80, 100, 118, 200);

        for (const Tick& t : ticks) {
            const float frac = (t.dB + 60.0f) / 60.0f;
            const float ty   = barTop + barH * (1.0f - frac);
            beginPath(); strokeColor(50, 65, 80, 160); strokeWidth(1.0f);
            moveTo(x, ty); lineTo(x + 6.0f, ty); stroke(); closePath();
            char buf[8]; std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(t.dB));
            text(x - 1.0f, ty, buf, nullptr);
        }
    }

    // ── interaction ───────────────────────────────────────────────────────

    void updateFromY(int idx, float mouseY)
    {
        const SliderDef& def  = kSliders[idx];
        const Rect&      rect = sliderRects_[idx];
        // top of rect = max, bottom = min
        const float t = clampf(1.0f - (mouseY - rect.y) / rect.h, 0.0f, 1.0f);
        commit(def.index, fromNorm(def, t));
    }

    void nudge(int idx, float dir)
    {
        const SliderDef& def = kSliders[idx];
        const float step = def.logScale ? (def.max - def.min) / 150.0f
                                        : (def.max - def.min) / 100.0f;
        commit(def.index, values_[def.index] + dir * step);
    }

    void commit(uint32_t index, float value)
    {
        for (const SliderDef& def : kSliders) {
            if (def.index == index) { value = clampf(value, def.min, def.max); break; }
        }
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassopsUI)
};

UI* createUI() { return new BassopsUI(); }

END_NAMESPACE_DISTRHO
