#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Must match DamianoPlugin.cpp exactly — indices are stable across saves
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDrive,
    kParamTone,
    kParamFoldCount,
    kParamMix,
    kParamOutputGain,
    kParamCCDrive,
    kParamCCChannel,
    kParameterCount
};

constexpr std::array<const char*, 6> kModeNames = {{
    "Soft", "Tanh", "Fuzz", "Overdrive", "Tube", "Wavefold"
}};

struct Rect {
    float x, y, w, h;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    const char* unit;
    float       min, max;
    bool        integer;
    const char* stateKey;
};

constexpr std::array<SliderDef, 7> kSliders = {{
    {kParamDrive,      "Drive",       "",   1.0f,   10.0f, false, "drive"       },
    {kParamTone,       "Tone",        "%",  0.0f,  100.0f, false, "tone"        },
    {kParamFoldCount,  "Folds",       "",   1.0f,    8.0f, true,  "fold_count"  },
    {kParamMix,        "Mix",         "%",  0.0f,  100.0f, false, "mix"         },
    {kParamOutputGain, "Output Gain", "dB", -24.0f, 24.0f, false, "output_gain" },
    {kParamCCDrive,    "CC Number",   "",   0.0f,  127.0f, true,  "cc_drive"    },
    {kParamCCChannel,  "CC Channel",  "",   1.0f,   16.0f, true,  "cc_channel"  },
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::string formatValue(const SliderDef& def, float v)
{
    char buf[32];
    if (def.index == kParamCCDrive && static_cast<int>(std::lround(v)) == 0) {
        return "off";
    }
    if (def.index == kParamOutputGain) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
    } else if (def.integer) {
        std::snprintf(buf, sizeof(buf), "%d%s", static_cast<int>(std::lround(v)), def.unit);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f%s", v, def.unit);
    }
    return buf;
}

}  // namespace

class DamianoUI : public UI
{
public:
    DamianoUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamMode]       = 1.0f;
        values_[kParamDrive]      = 2.0f;
        values_[kParamTone]       = 50.0f;
        values_[kParamFoldCount]  = 2.0f;
        values_[kParamMix]        = 100.0f;
        values_[kParamOutputGain] = 0.0f;
        values_[kParamCCDrive]    = 0.0f;
        values_[kParamCCChannel]  = 1.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t /*index*/, float /*value*/) override
    {
        // All slider values arrive via stateChanged; ignore host parameter
        // notifications so REAPER automation replay cannot override the display.
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (!value) return;
        if (std::strcmp(key, "mode") == 0) {
            currentMode_ = std::atoi(value);
        } else if (std::strcmp(key, "drive") == 0) {
            values_[kParamDrive]      = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "tone") == 0) {
            values_[kParamTone]       = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "fold_count") == 0) {
            values_[kParamFoldCount]  = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "mix") == 0) {
            values_[kParamMix]        = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "output_gain") == 0) {
            values_[kParamOutputGain] = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "cc_drive") == 0) {
            values_[kParamCCDrive]    = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "cc_channel") == 0) {
            values_[kParamCCChannel]  = static_cast<float>(std::atof(value));
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        drawBackground(W, H);
        drawHeader(W);
        drawModeButtons();
        drawSliders(W);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;

        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            dragSlider_ = -1;
            return false;
        }

        // Mode buttons — sent as state (bypasses host automation)
        for (int m = 0; m < 6; ++m) {
            if (modeRect(m).contains(mx, my)) {
                char buf[4];
                std::snprintf(buf, sizeof(buf), "%d", m);
                setState("mode", buf);
                currentMode_ = m;
                repaint();
                return true;
            }
        }

        // Sliders
        for (int s = 0; s < static_cast<int>(kSliders.size()); ++s) {
            if (sliderTrackRect(s).contains(mx, my)) {
                dragSlider_   = s;
                updateSliderFromX(s, mx);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragSlider_ < 0) return false;
        updateSliderFromX(dragSlider_, static_cast<float>(ev.pos.getX()));
        return true;
    }

private:
    std::array<float, kParameterCount> values_ {};
    int   currentMode_ = 1;  // default kModeTanh; updated via stateChanged
    int   dragSlider_  = -1;

    static constexpr float kPad     = 20.0f;
    static constexpr float kModeTop = 62.0f;
    static constexpr float kSliderX = 310.0f;

    [[nodiscard]] Rect modeRect(int m) const noexcept
    {
        constexpr float bw = 118.0f, bh = 34.0f, gapX = 8.0f, gapY = 7.0f;
        const int col = m % 2;
        const int row = m / 2;
        return {kPad + col * (bw + gapX),
                kModeTop + row * (bh + gapY),
                bw, bh};
    }

    [[nodiscard]] Rect sliderTrackRect(int s) const noexcept
    {
        constexpr float trackH = 14.0f;
        const float top = 62.0f + s * 52.0f;
        const float W   = static_cast<float>(getWidth());
        return {kSliderX, top + 20.0f, W - kSliderX - kPad, trackH};
    }

    void updateSliderFromX(int s, float mx)
    {
        const SliderDef& def = kSliders[static_cast<std::size_t>(s)];
        const Rect tr        = sliderTrackRect(s);
        const float t        = clampf((mx - tr.x) / tr.w, 0.0f, 1.0f);
        float v              = def.min + t * (def.max - def.min);
        if (def.integer) v   = std::round(v);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        setState(def.stateKey, buf);
        values_[def.index] = v;
        repaint();
    }

    void drawBackground(float W, float H)
    {
        beginPath();
        fillColor(18, 18, 22, 255);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawHeader(float W)
    {
        beginPath();
        fillColor(28, 28, 36, 255);
        rect(0, 0, W, 50.0f);
        fill();
        closePath();

        fontSize(22.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(230, 115, 26, 255);
        text(kPad, 25.0f, "DAMIANO", nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(130, 130, 120, 255);
        text(kPad + 140.0f, 25.0f, "stereo distortion  |  MIDI CC via Drift", nullptr);

        beginPath();
        strokeColor(60, 60, 70, 255);
        strokeWidth(1.0f);
        moveTo(0, 50.0f);
        lineTo(W, 50.0f);
        stroke();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(100, 100, 95, 255);
        text(kPad, 54.0f, "MODE", nullptr);
        text(kSliderX, 54.0f, "PARAMETERS", nullptr);
    }

    void drawModeButtons()
    {
        for (int m = 0; m < 6; ++m) {
            const Rect  r      = modeRect(m);
            const bool  active = (m == currentMode_);

            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 5.0f);
            if (active)
                fillColor(200, 100, 24, 255);
            else
                fillColor(38, 38, 48, 255);
            fill();
            closePath();

            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            if (active)
                fillColor(15, 15, 18, 255);
            else
                fillColor(200, 200, 190, 255);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f,
                 kModeNames[static_cast<std::size_t>(m)], nullptr);
        }
    }

    void drawSliders(float W)
    {
        constexpr int kWavefold = 5;

        for (int s = 0; s < static_cast<int>(kSliders.size()); ++s) {
            const SliderDef& def = kSliders[static_cast<std::size_t>(s)];
            const Rect        tr = sliderTrackRect(s);
            const float       labelY = tr.y - 18.0f;

            const bool dimmed = (def.index == kParamFoldCount && currentMode_ != kWavefold);
            const uint8_t alpha = dimmed ? 70 : 255;

            if (s > 0) {
                beginPath();
                strokeColor(35, 35, 42, 255);
                strokeWidth(1.0f);
                moveTo(kSliderX, tr.y - 26.0f);
                lineTo(W - kPad, tr.y - 26.0f);
                stroke();
                closePath();
            }

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(190, 190, 180, alpha);
            text(kSliderX, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(dimmed ? 80 : 220, dimmed ? 80 : 160, dimmed ? 75 : 60, alpha);
            text(W - kPad, labelY, formatValue(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fillColor(40, 40, 50, alpha);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min),
                                      0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(200, 100, 24, alpha);
                fill();
                closePath();
            }
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamianoUI)
};

UI* createUI()
{
    return new DamianoUI();
}

END_NAMESPACE_DISTRHO
