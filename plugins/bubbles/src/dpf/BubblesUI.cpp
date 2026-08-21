#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamFlow,
    kParamTurbulence,
    kParamSize,
    kParamDensity,
    kParamHeat,
    kParamDepth,
    kParamBrightness,
    kParamResonance,
    kParamRandomness,
    kParamSpace,
    kParamDrive,
    kParamOutput,
    kParamConductorCh,
    kParamLfoTarget,
    kParamLfoShape,
    kParamLfoRate,
    kParamLfoDepth,
    kParamLfoSync,
    kParamLfoDivision,
    kParameterCount
};

struct Rect {
    float x, y, w, h;
    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    float       min, max;
};

constexpr std::array<SliderDef, 12> kSliders = {{
    {kParamFlow,       "Flow",       0.0f, 1.0f},
    {kParamTurbulence, "Turbulence", 0.0f, 1.0f},
    {kParamSize,       "Size",       0.0f, 1.0f},
    {kParamDensity,    "Density",    0.0f, 1.0f},
    {kParamHeat,       "Heat",       0.0f, 1.0f},
    {kParamDepth,      "Depth",      0.0f, 1.0f},
    {kParamBrightness, "Brightness", 0.0f, 1.0f},
    {kParamResonance,  "Resonance",  0.0f, 1.0f},
    {kParamRandomness, "Randomness", 0.0f, 1.0f},
    {kParamSpace,      "Space",      0.0f, 1.0f},
    {kParamDrive,      "Drive",      0.0f, 1.0f},
    {kParamOutput,     "Output",     0.0f, 1.0f},
}};

static constexpr const char* kModeNames[7] = {
    "Stream", "River", "Ocean", "Bubbles", "Drips", "Rain", "Custom"
};

static constexpr const char* kLfoTargetNames[7] = {
    "Off", "Flow", "Density", "Bright", "Size", "Heat", "Random"
};

static constexpr const char* kLfoShapeNames[5] = {
    "Sine", "Tri", "Ramp Dn", "Ramp Up", "Square"
};

static constexpr const char* kLfoDivNames[8] = {
    "1/1", "1/2", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"
};

static constexpr float kLfoRateMin = 0.05f;
static constexpr float kLfoRateMax = 20.0f;

static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

}  // namespace

class BubblesUI : public UI
{
public:
    BubblesUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        values_.fill(0.0f);
        values_[kParamMode]        = 6.0f;
        values_[kParamFlow]        = 0.50f;
        values_[kParamTurbulence]  = 0.40f;
        values_[kParamSize]        = 0.45f;
        values_[kParamDensity]     = 0.50f;
        values_[kParamHeat]        = 0.30f;
        values_[kParamDepth]       = 0.20f;
        values_[kParamBrightness]  = 0.55f;
        values_[kParamResonance]   = 0.55f;
        values_[kParamRandomness]  = 0.40f;
        values_[kParamSpace]       = 0.35f;
        values_[kParamDrive]       = 0.35f;
        values_[kParamOutput]      = 0.80f;
        values_[kParamConductorCh] = 0.0f;
        values_[kParamLfoTarget]   = 0.0f;
        values_[kParamLfoShape]    = 0.0f;
        values_[kParamLfoRate]     = 0.5f;
        values_[kParamLfoDepth]    = 0.0f;
        values_[kParamLfoSync]     = 0.0f;
        values_[kParamLfoDivision] = 2.0f;
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
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        drawBackground(W, H);
        drawTitleBar(W);
        drawModeBar(W);
        drawSliderGrid(W, H);
        drawLfoStrip(W);
        drawWaveDecoration(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (!ev.press) {
            dragIndex_ = -1;
            return false;
        }

        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());

        // Mode bar
        for (int m = 0; m < 7; ++m) {
            Rect r = modeButtonRect(m, static_cast<float>(getWidth()));
            if (r.contains(mx, my)) {
                setParameterValue(kParamMode, static_cast<float>(m));
                values_[kParamMode] = static_cast<float>(m);
                repaint();
                return true;
            }
        }

        // Main sliders
        for (int s = 0; s < 12; ++s) {
            Rect r = sliderTrackRect(s);
            if (r.contains(mx, my)) {
                dragIndex_ = s;
                updateSlider(s, mx, r);
                return true;
            }
        }

        // LFO target buttons
        for (int t = 0; t < 7; ++t) {
            if (lfoTargetBtnRect(t).contains(mx, my)) {
                values_[kParamLfoTarget] = static_cast<float>(t);
                setParameterValue(kParamLfoTarget, static_cast<float>(t));
                repaint();
                return true;
            }
        }

        // LFO shape buttons
        for (int s = 0; s < 5; ++s) {
            if (lfoShapeBtnRect(s).contains(mx, my)) {
                values_[kParamLfoShape] = static_cast<float>(s);
                setParameterValue(kParamLfoShape, static_cast<float>(s));
                repaint();
                return true;
            }
        }

        // LFO sync toggle
        if (lfoSyncBtnRect().contains(mx, my)) {
            const float newVal = (values_[kParamLfoSync] >= 0.5f) ? 0.0f : 1.0f;
            values_[kParamLfoSync] = newVal;
            setParameterValue(kParamLfoSync, newVal);
            repaint();
            return true;
        }

        // LFO division buttons
        for (int d = 0; d < 8; ++d) {
            if (lfoDivBtnRect(d).contains(mx, my)) {
                values_[kParamLfoDivision] = static_cast<float>(d);
                setParameterValue(kParamLfoDivision, static_cast<float>(d));
                repaint();
                return true;
            }
        }

        // LFO Rate slider (dragIndex_ 12)
        if (lfoRateSliderRect().contains(mx, my)) {
            dragIndex_ = 12;
            updateLfoSlider(12, mx);
            return true;
        }

        // LFO Depth slider (dragIndex_ 13)
        if (lfoDepthSliderRect().contains(mx, my)) {
            dragIndex_ = 13;
            updateLfoSlider(13, mx);
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragIndex_ < 0) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        if (dragIndex_ < 12) {
            Rect r = sliderTrackRect(dragIndex_);
            updateSlider(dragIndex_, mx, r);
        } else {
            updateLfoSlider(dragIndex_, mx);
        }
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        const float dy = static_cast<float>(ev.delta.getY());

        for (int s = 0; s < 12; ++s) {
            Rect r = sliderTrackRect(s);
            if (r.contains(mx, my)) {
                const SliderDef& def = kSliders[s];
                float v = clampf(values_[def.index] + dy * 0.02f, def.min, def.max);
                values_[def.index] = v;
                setParameterValue(def.index, v);
                repaint();
                return true;
            }
        }

        if (lfoRateSliderRect().contains(mx, my)) {
            float v = clampf(values_[kParamLfoRate] + dy * 0.5f, kLfoRateMin, kLfoRateMax);
            values_[kParamLfoRate] = v;
            setParameterValue(kParamLfoRate, v);
            repaint();
            return true;
        }

        if (lfoDepthSliderRect().contains(mx, my)) {
            float v = clampf(values_[kParamLfoDepth] + dy * 0.02f, 0.0f, 1.0f);
            values_[kParamLfoDepth] = v;
            setParameterValue(kParamLfoDepth, v);
            repaint();
            return true;
        }

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    int dragIndex_ = -1;

    // Main grid layout
    static constexpr float kModeBarY  = 50.0f;
    static constexpr float kModeBarH  = 28.0f;
    static constexpr float kGridTop   = 92.0f;
    static constexpr float kGridLeft  = 14.0f;
    static constexpr float kSliderW   = 196.0f;
    static constexpr float kSliderH   = 13.0f;
    static constexpr float kRowH      = 110.0f;
    static constexpr float kColW      = 218.0f;

    // LFO strip layout (below main grid, above wave decoration)
    static constexpr float kLfoTop   = 424.0f;
    static constexpr float kLfoH     = 112.0f;
    static constexpr float kLfoRow1  = kLfoTop + 10.0f;   // target buttons row
    static constexpr float kLfoRow2  = kLfoRow1 + 26.0f;  // shape/sync/div row
    static constexpr float kLfoRow3  = kLfoRow2 + 26.0f;  // rate/depth slider row
    static constexpr float kLfoBtnH  = 22.0f;

    // --- Layout helpers ---

    Rect modeButtonRect(int m, float W) const
    {
        const float totalW = W - 28.0f;
        const float btnW   = totalW / 7.0f - 4.0f;
        return {14.0f + m * (btnW + 4.0f), kModeBarY, btnW, kModeBarH};
    }

    Rect sliderTrackRect(int s) const
    {
        const int col = s % 4;
        const int row = s / 4;
        float x = kGridLeft + col * kColW + 12.0f;
        float y = kGridTop  + row * kRowH + 42.0f;
        return {x, y, kSliderW, kSliderH};
    }

    Rect lfoTargetBtnRect(int t) const
    {
        // 7 buttons × 100px + 6 × 4px gaps, starting at x=128
        return {128.0f + t * 104.0f, kLfoRow1, 100.0f, kLfoBtnH};
    }

    Rect lfoShapeBtnRect(int s) const
    {
        // 5 buttons × 55px + 4 × 4px gaps, starting at x=128
        return {128.0f + s * 59.0f, kLfoRow2, 55.0f, kLfoBtnH};
    }

    Rect lfoSyncBtnRect() const
    {
        return {430.0f, kLfoRow2, 54.0f, kLfoBtnH};
    }

    Rect lfoDivBtnRect(int d) const
    {
        // 8 buttons × 38px + 7 × 3px gaps, starting at x=524
        return {524.0f + d * 41.0f, kLfoRow2, 38.0f, kLfoBtnH};
    }

    Rect lfoRateSliderRect() const
    {
        return {72.0f, kLfoRow3 + 13.0f, 200.0f, kSliderH};
    }

    Rect lfoDepthSliderRect() const
    {
        return {370.0f, kLfoRow3 + 13.0f, 200.0f, kSliderH};
    }

    // --- Draw helpers ---

    void drawBackground(float W, float H)
    {
        beginPath();
        rect(0.0f, 0.0f, W, H);
        fillColor(15, 30, 48, 255);
        fill();
        closePath();
    }

    void drawTitleBar(float W)
    {
        beginPath();
        rect(0.0f, 0.0f, W, 44.0f);
        fillColor(22, 43, 66, 255);
        fill();
        closePath();

        fontSize(22.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(0, 188, 212, 255);
        text(18.0f, 22.0f, "BUBBLES", nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(130, 178, 200, 255);
        text(118.0f, 22.0f, "Water Sound Generator", nullptr);
    }

    void drawModeBar(float W)
    {
        const int current = static_cast<int>(values_[kParamMode]);

        for (int m = 0; m < 7; ++m) {
            Rect r = modeButtonRect(m, W);
            const bool active = (m == current);

            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 4.0f);
            if (active)
                fillColor(0, 188, 212, 255);
            else
                fillColor(28, 56, 82, 255);
            fill();
            closePath();

            if (active) {
                beginPath();
                roundedRect(r.x, r.y, r.w, r.h, 4.0f);
                strokeColor(0, 220, 240, 255);
                strokeWidth(1.5f);
                stroke();
                closePath();
            }

            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            if (active)
                fillColor(15, 30, 48, 255);
            else
                fillColor(130, 170, 195, 255);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, kModeNames[m], nullptr);
        }
    }

    void drawSliderGrid(float /*W*/, float /*H*/)
    {
        for (int s = 0; s < 12; ++s) {
            const SliderDef& def = kSliders[s];
            const float val  = values_[def.index];
            const float norm = (val - def.min) / (def.max - def.min);

            const int col  = s % 4;
            const int row  = s / 4;
            const float cx = kGridLeft + col * kColW;
            const float cy = kGridTop  + row * kRowH;

            beginPath();
            roundedRect(cx + 4.0f, cy + 4.0f, kColW - 8.0f, kRowH - 8.0f, 6.0f);
            fillColor(22, 43, 66, 255);
            fill();
            closePath();

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(0, 160, 180, 255);
            text(cx + 16.0f, cy + 14.0f, def.label, nullptr);

            char buf[24];
            std::snprintf(buf, sizeof(buf), "%.2f", val);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(220, 240, 250, 255);
            text(cx + kColW - 10.0f, cy + 14.0f, buf, nullptr);

            Rect track = sliderTrackRect(s);
            beginPath();
            roundedRect(track.x, track.y, track.w, track.h, track.h * 0.5f);
            fillColor(25, 55, 80, 255);
            fill();
            closePath();

            if (norm > 0.002f) {
                beginPath();
                roundedRect(track.x, track.y, track.w * norm, track.h, track.h * 0.5f);
                fillColor(25, 140, 190, 255);
                fill();
                closePath();
            }

            const float thumbX = track.x + track.w * norm;
            const float thumbY = track.y + track.h * 0.5f;
            beginPath();
            circle(thumbX, thumbY, track.h * 0.80f);
            fillColor(0, 210, 230, 255);
            fill();
            closePath();
        }
    }

    void drawLfoStrip(float /*W*/)
    {
        const bool syncOn   = values_[kParamLfoSync] >= 0.5f;
        const int  target   = static_cast<int>(values_[kParamLfoTarget] + 0.5f);
        const int  shape    = static_cast<int>(values_[kParamLfoShape]  + 0.5f);
        const int  division = static_cast<int>(values_[kParamLfoDivision] + 0.5f);

        // Panel background
        beginPath();
        roundedRect(10.0f, kLfoTop, 860.0f, kLfoH, 6.0f);
        fillColor(22, 43, 66, 255);
        fill();
        closePath();

        // Section title "LFO"
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(0, 188, 212, 255);
        text(20.0f, kLfoRow1 + kLfoBtnH * 0.5f, "LFO", nullptr);

        // Row 1: Target buttons
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(80, 120, 150, 255);
        text(76.0f, kLfoRow1 + kLfoBtnH * 0.5f, "Target", nullptr);

        for (int t = 0; t < 7; ++t) {
            Rect r = lfoTargetBtnRect(t);
            const bool active = (t == target);
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 3.0f);
            if (active)
                fillColor(0, 160, 200, 255);
            else
                fillColor(28, 56, 82, 255);
            fill();
            closePath();
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(active ? 15 : 130, active ? 30 : 170, active ? 48 : 195, 255);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, kLfoTargetNames[t], nullptr);
        }

        // Row 2: Shape buttons
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(80, 120, 150, 255);
        text(76.0f, kLfoRow2 + kLfoBtnH * 0.5f, "Shape", nullptr);

        for (int s = 0; s < 5; ++s) {
            Rect r = lfoShapeBtnRect(s);
            const bool active = (s == shape);
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 3.0f);
            if (active)
                fillColor(0, 160, 200, 255);
            else
                fillColor(28, 56, 82, 255);
            fill();
            closePath();
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(active ? 15 : 130, active ? 30 : 170, active ? 48 : 195, 255);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, kLfoShapeNames[s], nullptr);
        }

        // Sync toggle button
        {
            Rect r = lfoSyncBtnRect();
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 3.0f);
            if (syncOn)
                fillColor(0, 188, 90, 255);
            else
                fillColor(28, 56, 82, 255);
            fill();
            closePath();
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(syncOn ? 240 : 100, syncOn ? 255 : 140, syncOn ? 240 : 160, 255);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, "SYNC", nullptr);
        }

        // Division buttons (dimmed when sync is off)
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(syncOn ? 80 : 50, syncOn ? 120 : 80, syncOn ? 150 : 100, 255);
        text(494.0f, kLfoRow2 + kLfoBtnH * 0.5f, "Div", nullptr);

        for (int d = 0; d < 8; ++d) {
            Rect r = lfoDivBtnRect(d);
            const bool active = syncOn && (d == division);
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 3.0f);
            if (active)
                fillColor(0, 160, 200, 255);
            else if (syncOn)
                fillColor(28, 56, 82, 255);
            else
                fillColor(20, 40, 58, 255);
            fill();
            closePath();
            fontSize(9.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            if (active)
                fillColor(15, 30, 48, 255);
            else if (syncOn)
                fillColor(130, 170, 195, 255);
            else
                fillColor(60, 90, 110, 255);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, kLfoDivNames[d], nullptr);
        }

        // Row 3: Rate slider (dimmed when sync is on)
        {
            const float labelAlpha = syncOn ? 80.0f : 200.0f;
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(static_cast<uint8_t>(labelAlpha * 0.4f),
                      static_cast<uint8_t>(labelAlpha * 0.6f),
                      static_cast<uint8_t>(labelAlpha * 0.75f), 255);
            text(18.0f, kLfoRow3 + 7.0f, "Rate", nullptr);

            Rect tr = lfoRateSliderRect();
            const float norm = (values_[kParamLfoRate] - kLfoRateMin) / (kLfoRateMax - kLfoRateMin);
            drawLfoSlider(tr, norm, syncOn ? 0.35f : 1.0f);

            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.2f Hz", values_[kParamLfoRate]);
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(static_cast<uint8_t>(syncOn ? 60 : 180),
                      static_cast<uint8_t>(syncOn ? 90 : 210),
                      static_cast<uint8_t>(syncOn ? 110 : 230), 255);
            text(tr.x + tr.w + 6.0f, kLfoRow3 + 7.0f, buf, nullptr);
        }

        // Depth slider
        {
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(80, 120, 150, 255);
            text(310.0f, kLfoRow3 + 7.0f, "Depth", nullptr);

            Rect tr = lfoDepthSliderRect();
            drawLfoSlider(tr, values_[kParamLfoDepth], 1.0f);

            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", values_[kParamLfoDepth]);
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(180, 210, 230, 255);
            text(tr.x + tr.w + 6.0f, kLfoRow3 + 7.0f, buf, nullptr);
        }
    }

    void drawLfoSlider(const Rect& tr, float norm, float brightness)
    {
        const uint8_t trackR = static_cast<uint8_t>(25 * brightness);
        const uint8_t trackG = static_cast<uint8_t>(55 * brightness);
        const uint8_t trackB = static_cast<uint8_t>(80 * brightness);
        beginPath();
        roundedRect(tr.x, tr.y, tr.w, tr.h, tr.h * 0.5f);
        fillColor(trackR, trackG, trackB, 255);
        fill();
        closePath();

        if (norm > 0.002f) {
            const uint8_t fillR = static_cast<uint8_t>(25  * brightness);
            const uint8_t fillG = static_cast<uint8_t>(140 * brightness);
            const uint8_t fillB = static_cast<uint8_t>(190 * brightness);
            beginPath();
            roundedRect(tr.x, tr.y, tr.w * norm, tr.h, tr.h * 0.5f);
            fillColor(fillR, fillG, fillB, 255);
            fill();
            closePath();
        }

        const float thumbX = tr.x + tr.w * norm;
        const float thumbY = tr.y + tr.h * 0.5f;
        beginPath();
        circle(thumbX, thumbY, tr.h * 0.80f);
        fillColor(static_cast<uint8_t>(0   * brightness),
                  static_cast<uint8_t>(210 * brightness),
                  static_cast<uint8_t>(230 * brightness), 255);
        fill();
        closePath();
    }

    void drawWaveDecoration(float W, float H)
    {
        beginPath();
        moveTo(0.0f, H - 14.0f);
        const int steps = 40;
        for (int i = 1; i <= steps; ++i) {
            const float x  = W * static_cast<float>(i) / static_cast<float>(steps);
            const float ph = static_cast<float>(i) / static_cast<float>(steps)
                           * 4.0f * 3.14159f;
            const float y  = H - 14.0f + std::sin(ph) * 4.0f;
            lineTo(x, y);
        }
        lineTo(W, H);
        lineTo(0.0f, H);
        closePath();
        fillColor(0, 188, 212, 46);
        fill();
    }

    // --- Interaction helpers ---

    void updateSlider(int s, float mx, const Rect& r)
    {
        const SliderDef& def = kSliders[s];
        float norm = clampf((mx - r.x) / r.w, 0.0f, 1.0f);
        float v    = def.min + norm * (def.max - def.min);
        values_[def.index] = v;
        setParameterValue(def.index, v);
        repaint();
    }

    void updateLfoSlider(int which, float mx)
    {
        if (which == 12) {
            Rect r = lfoRateSliderRect();
            const float norm = clampf((mx - r.x) / r.w, 0.0f, 1.0f);
            const float v    = kLfoRateMin + norm * (kLfoRateMax - kLfoRateMin);
            values_[kParamLfoRate] = v;
            setParameterValue(kParamLfoRate, v);
        } else {
            Rect r = lfoDepthSliderRect();
            const float v = clampf((mx - r.x) / r.w, 0.0f, 1.0f);
            values_[kParamLfoDepth] = v;
            setParameterValue(kParamLfoDepth, v);
        }
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblesUI)
};

UI* createUI()
{
    return new BubblesUI();
}

END_NAMESPACE_DISTRHO
