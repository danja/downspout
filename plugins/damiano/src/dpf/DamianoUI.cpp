#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamMode       = 0,
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
    float       min;
    float       max;
    bool        integer;
    const char* unit;
};

constexpr std::array<SliderDef, 6> kSliders = {{
    {kParamDrive,      "Drive",       1.0f, 10.0f,  false, ""},
    {kParamTone,       "Tone",        0.0f, 100.0f, false, "%"},
    {kParamFoldCount,  "Folds",       1.0f,  8.0f,  true,  ""},
    {kParamMix,        "Mix",         0.0f, 100.0f, false, "%"},
    {kParamOutputGain, "Output Gain", -24.0f, 24.0f, false, "dB"},
    {kParamCCDrive,    "CC Number",   0.0f, 127.0f, true,  ""},
}};

[[nodiscard]] float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

[[nodiscard]] std::string formatValue(const SliderDef& def, float v)
{
    char buf[32];
    if (def.integer) {
        std::snprintf(buf, sizeof(buf), "%d%s", static_cast<int>(std::lround(v)), def.unit);
    } else if (def.index == kParamOutputGain) {
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f%s", v, def.unit);
    }
    return buf;
}

// Colour palette
struct Col { float r, g, b, a = 1.0f; };
constexpr Col kBg        {0.10f, 0.10f, 0.12f};
constexpr Col kPanel     {0.16f, 0.16f, 0.20f};
constexpr Col kAccent    {0.90f, 0.45f, 0.10f};
constexpr Col kAccentDim {0.50f, 0.25f, 0.06f};
constexpr Col kText      {0.92f, 0.92f, 0.88f};
constexpr Col kSubtext   {0.55f, 0.55f, 0.52f};
constexpr Col kTrack     {0.25f, 0.25f, 0.30f};

}  // namespace

class DamianoUI : public UI
{
public:
    DamianoUI() : UI(780, 500)
    {
        values_[kParamMode]       = 1.0f;   // Tanh
        values_[kParamDrive]      = 2.0f;
        values_[kParamTone]       = 50.0f;
        values_[kParamFoldCount]  = 2.0f;
        values_[kParamMix]        = 100.0f;
        values_[kParamOutputGain] = 0.0f;
        values_[kParamCCDrive]    = 0.0f;
        values_[kParamCCChannel]  = 1.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
        createFontFromFile("sans-bold", "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
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
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        // Background
        beginPath();
        fillColor(kBg.r, kBg.g, kBg.b);
        rect(0, 0, W, H);
        fill();

        drawTitle(W);
        drawModeButtons();
        drawSliders();
        drawCCSection();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.press && ev.button == 1) {
            // Mode buttons
            for (int m = 0; m < 6; ++m) {
                if (modeRect(m).contains(ev.pos.getX(), ev.pos.getY())) {
                    setParameterValue(kParamMode, static_cast<float>(m));
                    values_[kParamMode] = static_cast<float>(m);
                    repaint();
                    return true;
                }
            }

            // Sliders — begin drag
            for (std::size_t s = 0; s < kSliders.size(); ++s) {
                if (sliderRect(s).contains(ev.pos.getX(), ev.pos.getY())) {
                    dragSlider_    = static_cast<int>(s);
                    dragStartX_    = ev.pos.getX();
                    dragStartVal_  = values_[kSliders[s].index];
                    return true;
                }
            }

            // CC channel spinner arrows
            Rect leftArrow  = {kCCSection + 10.0f, 410.0f, 20.0f, 24.0f};
            Rect rightArrow = {kCCSection + 80.0f, 410.0f, 20.0f, 24.0f};
            if (leftArrow.contains(ev.pos.getX(), ev.pos.getY())) {
                float v = std::max(1.0f, values_[kParamCCChannel] - 1.0f);
                setParameterValue(kParamCCChannel, v);
                values_[kParamCCChannel] = v;
                repaint();
                return true;
            }
            if (rightArrow.contains(ev.pos.getX(), ev.pos.getY())) {
                float v = std::min(16.0f, values_[kParamCCChannel] + 1.0f);
                setParameterValue(kParamCCChannel, v);
                values_[kParamCCChannel] = v;
                repaint();
                return true;
            }
        }

        if (!ev.press && ev.button == 1 && dragSlider_ >= 0) {
            dragSlider_ = -1;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragSlider_ < 0) return false;

        const SliderDef& def = kSliders[static_cast<std::size_t>(dragSlider_)];
        const float range    = def.max - def.min;
        const float delta    = (ev.pos.getX() - dragStartX_) / sliderWidth() * range;
        float newVal         = clampf(dragStartVal_ + delta, def.min, def.max);

        if (def.integer)
            newVal = std::round(newVal);

        setParameterValue(def.index, newVal);
        values_[def.index] = newVal;
        repaint();
        return true;
    }

private:
    static constexpr float kPad      = 18.0f;
    static constexpr float kModePad  = 20.0f;
    static constexpr float kCCSection = 570.0f;

    std::array<float, kParameterCount> values_ {};
    int   dragSlider_   = -1;
    float dragStartX_   = 0.0f;
    float dragStartVal_ = 0.0f;

    [[nodiscard]] float sliderWidth() const { return 340.0f; }

    [[nodiscard]] Rect modeRect(int m) const
    {
        constexpr float bw = 120.0f, bh = 36.0f, gap = 8.0f;
        const int col = m % 2;
        const int row = m / 2;
        return {kModePad + col * (bw + gap),
                70.0f + row * (bh + gap),
                bw, bh};
    }

    [[nodiscard]] Rect sliderRect(std::size_t s) const
    {
        const float top = 80.0f + static_cast<float>(s) * 58.0f;
        return {270.0f, top, sliderWidth(), 28.0f};
    }

    void drawTitle(float W)
    {
        fontSize(26.0f);
        fontFace("sans-bold");
        fillColor(kAccent.r, kAccent.g, kAccent.b);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(kPad, 28.0f, "DAMIANO", nullptr);

        fontSize(12.0f);
        fontFace("sans");
        fillColor(kSubtext.r, kSubtext.g, kSubtext.b);
        text(kPad + 140.0f, 28.0f, "stereo distortion  |  MIDI CC via Drift", nullptr);

        // Divider
        beginPath();
        strokeColor(kAccentDim.r, kAccentDim.g, kAccentDim.b);
        strokeWidth(1.0f);
        moveTo(kPad, 50.0f);
        lineTo(W - kPad, 50.0f);
        stroke();
    }

    void drawModeButtons()
    {
        const int activeMode = static_cast<int>(std::round(values_[kParamMode]));

        fontSize(12.0f);
        fontFace("sans-bold");
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);

        for (int m = 0; m < 6; ++m) {
            Rect r = modeRect(m);
            const bool active = (m == activeMode);

            // Background
            beginPath();
            roundedRect(r.x, r.y, r.w, r.h, 5.0f);
            fillColor(active ? kAccent.r : kPanel.r,
                      active ? kAccent.g : kPanel.g,
                      active ? kAccent.b : kPanel.b);
            fill();

            fillColor(active ? 0.05f : kText.r,
                      active ? 0.05f : kText.g,
                      active ? 0.05f : kText.b);
            text(r.x + r.w * 0.5f, r.y + r.h * 0.5f, kModeNames[static_cast<std::size_t>(m)], nullptr);
        }

        // Section label
        fontSize(10.0f);
        fontFace("sans");
        fillColor(kSubtext.r, kSubtext.g, kSubtext.b);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(kModePad, 58.0f, "MODE", nullptr);
    }

    void drawSliders()
    {
        const int activeMode = static_cast<int>(std::round(values_[kParamMode]));
        constexpr int kModeWavefold = 5;

        fontSize(11.0f);
        fontFace("sans");
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);

        for (std::size_t s = 0; s < kSliders.size(); ++s) {
            const SliderDef& def = kSliders[s];
            Rect r               = sliderRect(s);

            // Dim Fold Count when not in wavefold mode
            const bool dimmed = (def.index == kParamFoldCount && activeMode != kModeWavefold);
            const float alpha  = dimmed ? 0.35f : 1.0f;

            // Label
            fillColor(kText.r * alpha, kText.g * alpha, kText.b * alpha, alpha);
            text(r.x - 10.0f, r.y + r.h * 0.5f, def.label, nullptr);

            // Track
            beginPath();
            roundedRect(r.x, r.y + r.h * 0.25f, r.w, r.h * 0.5f, 4.0f);
            fillColor(kTrack.r * alpha, kTrack.g * alpha, kTrack.b * alpha, alpha);
            fill();

            // Fill
            float norm = (values_[def.index] - def.min) / (def.max - def.min);
            norm = clampf(norm, 0.0f, 1.0f);
            beginPath();
            roundedRect(r.x, r.y + r.h * 0.25f, r.w * norm, r.h * 0.5f, 4.0f);
            fillColor(kAccent.r * alpha, kAccent.g * alpha, kAccent.b * alpha, alpha);
            fill();

            // Value label
            fontSize(10.0f);
            fontFace("sans");
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(kSubtext.r * alpha, kSubtext.g * alpha, kSubtext.b * alpha, alpha);
            text(r.x + r.w + 8.0f, r.y + r.h * 0.5f, formatValue(def, values_[def.index]).c_str(), nullptr);
        }
    }

    void drawCCSection()
    {
        const float x = kCCSection;

        // Panel background
        beginPath();
        roundedRect(x - 10.0f, 58.0f, 200.0f, 420.0f, 8.0f);
        fillColor(kPanel.r, kPanel.g, kPanel.b);
        fill();

        fontSize(10.0f);
        fontFace("sans");
        fillColor(kSubtext.r, kSubtext.g, kSubtext.b);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, 66.0f, "DRIFT / MIDI CC ROUTING", nullptr);

        // CC Drive display (from slider kSliders[5])
        fontSize(11.0f);
        fontFace("sans-bold");
        fillColor(kText.r, kText.g, kText.b);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x, 115.0f, "CC for Drive:", nullptr);

        int ccNum = static_cast<int>(std::round(values_[kParamCCDrive]));
        char buf[32];
        if (ccNum == 0) {
            std::snprintf(buf, sizeof(buf), "off");
        } else {
            std::snprintf(buf, sizeof(buf), "CC %d", ccNum);
        }
        fontSize(16.0f);
        fontFace("sans-bold");
        fillColor(ccNum > 0 ? kAccent.r : kSubtext.r,
                  ccNum > 0 ? kAccent.g : kSubtext.g,
                  ccNum > 0 ? kAccent.b : kSubtext.b);
        text(x, 140.0f, buf, nullptr);

        // CC Channel spinner
        fontSize(11.0f);
        fontFace("sans-bold");
        fillColor(kText.r, kText.g, kText.b);
        text(x, 175.0f, "MIDI Channel:", nullptr);

        int ch = static_cast<int>(std::round(values_[kParamCCChannel]));
        // Left arrow
        beginPath();
        fillColor(kAccent.r, kAccent.g, kAccent.b);
        moveTo(x,        410.0f + 12.0f);
        lineTo(x + 14.0f, 410.0f + 4.0f);
        lineTo(x + 14.0f, 410.0f + 20.0f);
        fill();
        // Value
        std::snprintf(buf, sizeof(buf), "Ch %d", ch);
        fontSize(14.0f);
        fontFace("sans-bold");
        fillColor(kText.r, kText.g, kText.b);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(x + 50.0f, 422.0f, buf, nullptr);
        // Right arrow
        beginPath();
        fillColor(kAccent.r, kAccent.g, kAccent.b);
        moveTo(x + 100.0f, 410.0f + 4.0f);
        lineTo(x + 86.0f,  410.0f + 12.0f);
        lineTo(x + 100.0f, 410.0f + 20.0f);
        fill();

        // Help text
        fontSize(9.0f);
        fontFace("sans");
        fillColor(kSubtext.r, kSubtext.g, kSubtext.b);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, 205.0f, "Set CC Number to the CC lane", nullptr);
        text(x, 218.0f, "output by the Drift plugin.", nullptr);
        text(x, 235.0f, "CC 0 = disabled.", nullptr);
        text(x, 260.0f, "Drift default lanes:", nullptr);
        text(x, 273.0f, "  Lane 1 → CC 1", nullptr);
        text(x, 286.0f, "  Lane 2 → CC 2", nullptr);
        text(x, 299.0f, "  Lane 3 → CC 3", nullptr);
        text(x, 312.0f, "  Lane 4 → CC 4", nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamianoUI)
};

UI* createUI()
{
    return new DamianoUI();
}

END_NAMESPACE_DISTRHO
