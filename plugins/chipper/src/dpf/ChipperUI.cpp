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

enum ParameterIndex : uint32_t {
    kParamBitDepth = 0,
    kParamRateDiv,
    kParamJitter,
    kParamMix,
    kParamOutputGain,
    kParameterCount
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    const char* unit;
    float       min, max;
    bool        integer;
    const char* stateKey;
};

constexpr std::array<SliderDef, 5> kSliders = {{
    { kParamBitDepth,   "Bit Depth",   "bit", 1.0f,   16.0f, true,  "bit_depth"   },
    { kParamRateDiv,    "Rate Div",    "×",   1.0f,   64.0f, true,  "rate_div"    },
    { kParamJitter,     "Jitter",      "%",   0.0f,    1.0f, false, "jitter"      },
    { kParamMix,        "Mix",         "%",   0.0f,  100.0f, false, "mix"         },
    { kParamOutputGain, "Output Gain", "dB", -12.0f,  12.0f, false, "output_gain" },
}};

struct Rect {
    float x, y, w, h;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::string formatValue(const SliderDef& def, float v)
{
    char buf[32];
    if (def.index == kParamBitDepth) {
        const int bits   = static_cast<int>(std::lround(v));
        const int levels = (bits >= 16) ? 32767 : (1 << (bits - 1));
        std::snprintf(buf, sizeof(buf), "%d-bit  (%d levels)", bits, levels);
    } else if (def.index == kParamRateDiv) {
        std::snprintf(buf, sizeof(buf), "sr/%d", static_cast<int>(std::lround(v)));
    } else if (def.index == kParamJitter) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
    } else if (def.index == kParamOutputGain) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
    }
    return buf;
}

}  // namespace

class ChipperUI : public UI
{
public:
    ChipperUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamBitDepth]   = 8.0f;
        values_[kParamRateDiv]    = 8.0f;
        values_[kParamJitter]     = 0.0f;
        values_[kParamMix]        = 100.0f;
        values_[kParamOutputGain] = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t, float) override {}  // state-driven; ignore

    void stateChanged(const char* key, const char* value) override
    {
        if (!value) return;
        if (std::strcmp(key, "bit_depth") == 0) {
            values_[kParamBitDepth]   = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "rate_div") == 0) {
            values_[kParamRateDiv]    = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "jitter") == 0) {
            values_[kParamJitter]     = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "mix") == 0) {
            values_[kParamMix]        = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, "output_gain") == 0) {
            values_[kParamOutputGain] = static_cast<float>(std::atof(value));
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        drawBackground(W, H);
        drawHeader(W);
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
        for (int s = 0; s < static_cast<int>(kSliders.size()); ++s) {
            if (sliderTrackRect(s).contains(mx, my)) {
                dragSlider_ = s;
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
    int dragSlider_ = -1;

    static constexpr float kPad     = 20.0f;
    static constexpr float kSliderX = 30.0f;
    static constexpr float kRowH    = 62.0f;
    static constexpr float kSliderY0 = 74.0f;

    [[nodiscard]] Rect sliderTrackRect(int s) const noexcept
    {
        const float W = static_cast<float>(getWidth());
        const float top = kSliderY0 + s * kRowH;
        return {kSliderX, top + 22.0f, W - kSliderX - kPad, 14.0f};
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
        fillColor(14, 16, 22, 255);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawHeader(float W)
    {
        beginPath();
        fillColor(22, 24, 34, 255);
        rect(0, 0, W, 58.0f);
        fill();
        closePath();

        fontSize(24.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(60, 220, 80, 255);
        text(kPad, 27.0f, "CHIPPER", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(110, 130, 100, 255);
        text(kPad + 145.0f, 27.0f, "1980s video game \xe2\x80\x94 bit crush \xc2\xb7 sample-rate reduction \xc2\xb7 clock jitter", nullptr);

        beginPath();
        strokeColor(40, 60, 40, 255);
        strokeWidth(1.0f);
        moveTo(0, 58.0f);
        lineTo(W, 58.0f);
        stroke();
        closePath();

        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(70, 90, 70, 255);
        text(kPad, 62.0f, "PARAMETERS", nullptr);
    }

    void drawSliders(float W)
    {
        for (int s = 0; s < static_cast<int>(kSliders.size()); ++s) {
            const SliderDef& def = kSliders[static_cast<std::size_t>(s)];
            const Rect tr        = sliderTrackRect(s);
            const float labelY   = tr.y - 18.0f;

            if (s > 0) {
                beginPath();
                strokeColor(28, 34, 28, 255);
                strokeWidth(1.0f);
                moveTo(kSliderX, tr.y - 26.0f);
                lineTo(W - kPad, tr.y - 26.0f);
                stroke();
                closePath();
            }

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(180, 210, 170, 255);
            text(kSliderX, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(80, 210, 100, 255);
            text(W - kPad, labelY, formatValue(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fillColor(30, 38, 30, 255);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min),
                                      0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(50, 180, 70, 255);
                fill();
                closePath();
            }
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipperUI)
};

UI* createUI()
{
    return new ChipperUI();
}

END_NAMESPACE_DISTRHO
