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

static float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

}  // namespace

class BubblesUI : public UI
{
public:
    BubblesUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        values_.fill(0.0f);
        values_[kParamMode]       = 6.0f;
        values_[kParamFlow]       = 0.50f;
        values_[kParamTurbulence] = 0.40f;
        values_[kParamSize]       = 0.45f;
        values_[kParamDensity]    = 0.50f;
        values_[kParamHeat]       = 0.30f;
        values_[kParamDepth]      = 0.20f;
        values_[kParamBrightness] = 0.55f;
        values_[kParamResonance]  = 0.55f;
        values_[kParamRandomness] = 0.40f;
        values_[kParamSpace]      = 0.35f;
        values_[kParamDrive]      = 0.35f;
        values_[kParamOutput]     = 0.80f;
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

        for (int m = 0; m < 7; ++m) {
            Rect r = modeButtonRect(m, static_cast<float>(getWidth()));
            if (r.contains(mx, my)) {
                setParameterValue(kParamMode, static_cast<float>(m));
                values_[kParamMode] = static_cast<float>(m);
                repaint();
                return true;
            }
        }

        for (int s = 0; s < 12; ++s) {
            Rect r = sliderTrackRect(s);
            if (r.contains(mx, my)) {
                dragIndex_ = s;
                updateSlider(s, mx, r);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragIndex_ < 0) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        Rect r = sliderTrackRect(dragIndex_);
        updateSlider(dragIndex_, mx, r);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());

        for (int s = 0; s < 12; ++s) {
            Rect r = sliderTrackRect(s);
            if (r.contains(mx, my)) {
                const SliderDef& def = kSliders[s];
                float v = values_[def.index] + static_cast<float>(ev.delta.getY()) * 0.02f;
                v = clampf(v, def.min, def.max);
                values_[def.index] = v;
                setParameterValue(def.index, v);
                repaint();
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    int dragIndex_ = -1;

    static constexpr float kModeBarY  = 50.0f;
    static constexpr float kModeBarH  = 28.0f;
    static constexpr float kGridTop   = 92.0f;
    static constexpr float kGridLeft  = 14.0f;
    static constexpr float kSliderW   = 196.0f;
    static constexpr float kSliderH   = 13.0f;
    static constexpr float kRowH      = 110.0f;
    static constexpr float kColW      = 218.0f;

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

            // Cell background
            beginPath();
            roundedRect(cx + 4.0f, cy + 4.0f, kColW - 8.0f, kRowH - 8.0f, 6.0f);
            fillColor(22, 43, 66, 255);
            fill();
            closePath();

            // Label
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(0, 160, 180, 255);
            text(cx + 16.0f, cy + 14.0f, def.label, nullptr);

            // Value
            char buf[24];
            std::snprintf(buf, sizeof(buf), "%.2f", val);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(220, 240, 250, 255);
            text(cx + kColW - 10.0f, cy + 14.0f, buf, nullptr);

            // Track background
            Rect track = sliderTrackRect(s);
            beginPath();
            roundedRect(track.x, track.y, track.w, track.h, track.h * 0.5f);
            fillColor(25, 55, 80, 255);
            fill();
            closePath();

            // Track fill
            if (norm > 0.002f) {
                beginPath();
                roundedRect(track.x, track.y, track.w * norm, track.h, track.h * 0.5f);
                fillColor(25, 140, 190, 255);
                fill();
                closePath();
            }

            // Thumb
            const float thumbX = track.x + track.w * norm;
            const float thumbY = track.y + track.h * 0.5f;
            beginPath();
            circle(thumbX, thumbY, track.h * 0.80f);
            fillColor(0, 210, 230, 255);
            fill();
            closePath();
        }
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

    void updateSlider(int s, float mx, const Rect& r)
    {
        const SliderDef& def = kSliders[s];
        float norm = clampf((mx - r.x) / r.w, 0.0f, 1.0f);
        float v    = def.min + norm * (def.max - def.min);
        values_[def.index] = v;
        setParameterValue(def.index, v);
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblesUI)
};

UI* createUI()
{
    return new BubblesUI();
}

END_NAMESPACE_DISTRHO
