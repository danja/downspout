#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

// Must match SpliffPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDepth,
    kParamSensitivity,
    kParamSharpness,
    kParamDecay,
    kParamDecayTilt,
    kParamSplitLow,
    kParamSplitHigh,
    kParamMix,
    kParamTrim,
    kParamBypass,
    kParamDelta,
    kParamCCDepth,
    kParamCCSensitivity,
    kParamCCDecay,
    kParamCCMix,
    kParamCCChannel,
    kParameterCount
};

struct Rect {
    float x, y, w, h;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    float       min, max;
    bool        integer;
    const char* stateKey;
};

constexpr std::array<SliderDef, 12> kMainSliders = {{
    { kParamMode,        "Mode", 0.0f,    1.0f,    true,  "mode"        },
    { kParamDepth,       "Depth",        0.0f,    1.0f,    false, "depth"       },
    { kParamSensitivity, "Sensitivity",  0.0f,    1.0f,    false, "sensitivity" },
    { kParamSharpness,   "Sharpness",    0.0f,    1.0f,    false, "sharpness"   },
    { kParamDecay,       "Decay",        0.0f,    1.0f,    false, "decay"       },
    { kParamDecayTilt,   "Decay LF/HF",  -1.0f,   1.0f,    false, "decay_tilt"  },
    { kParamSplitLow,    "Split Low",    20.0f,   2000.0f, false, "split_low"   },
    { kParamSplitHigh,   "Split High",   500.0f,  12000.0f,false, "split_high"  },
    { kParamMix,         "Mix",          0.0f,    100.0f,  false, "mix"         },
    { kParamTrim,        "Trim",         -12.0f,  12.0f,  false, "trim"        },
    { kParamBypass,      "Bypass",       0.0f,    1.0f,    true,  "bypass"      },
    { kParamDelta,       "Delta",        0.0f,    1.0f,    true,  "delta"       },
}};

constexpr std::array<SliderDef, 5> kCCSliders = {{
    { kParamCCDepth,       "CC Depth", 0.0f, 127.0f, true, "cc_depth"       },
    { kParamCCSensitivity, "CC Sens",  0.0f, 127.0f, true, "cc_sensitivity" },
    { kParamCCDecay,       "CC Decay", 0.0f, 127.0f, true, "cc_decay"       },
    { kParamCCMix,         "CC Mix",   0.0f, 127.0f, true, "cc_mix"         },
    { kParamCCChannel,     "CC Channel", 1.0f, 16.0f, true, "cc_channel"    },
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::string formatMain(const SliderDef& def, float v)
{
    char buf[40];
    switch (def.index) {
    case kParamMode:
        std::snprintf(buf, sizeof(buf), "%s", v < 0.5f ? "Cut" : "Boost");
        break;
    case kParamDepth:
    case kParamSensitivity:
    case kParamSharpness:
    case kParamDecay:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
        break;
    case kParamDecayTilt:
        if (std::fabs(v) < 0.01f) std::snprintf(buf, sizeof(buf), "equal");
        else std::snprintf(buf, sizeof(buf), "%s%.0f%%", v < 0 ? "LF " : "HF ", std::fabs(v) * 100.0f);
        break;
    case kParamSplitLow:
    case kParamSplitHigh:
        if (v >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
        else std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
        break;
    case kParamMix:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
        break;
    case kParamTrim:
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
        break;
    case kParamBypass:
        std::snprintf(buf, sizeof(buf), "%s", v < 0.5f ? "active" : "bypassed");
        break;
    case kParamDelta:
        std::snprintf(buf, sizeof(buf), "%s", v < 0.5f ? "off" : "on");
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        break;
    }
    return buf;
}

[[nodiscard]] std::string formatCC(const SliderDef& def, float v)
{
    char buf[24];
    if (def.index != kParamCCChannel && static_cast<int>(std::lround(v)) == 0)
        return "off";
    std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
    return buf;
}

[[nodiscard]] bool isCCOff(const SliderDef& def, float v)
{
    return def.index != kParamCCChannel && static_cast<int>(std::lround(v)) == 0;
}

constexpr float kPad      = 16.0f;
constexpr float kDivX     = 430.0f;
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kRowH     = 44.0f;
constexpr float kSlidersY = 76.0f;

}  // namespace

class SpliffUI : public UI
{
public:
    SpliffUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamMode] = 0.0f;
        values_[kParamDepth] = 0.5f;
        values_[kParamSensitivity] = 0.5f;
        values_[kParamSharpness] = 0.3f;
        values_[kParamDecay] = 0.25f;
        values_[kParamDecayTilt] = 0.0f;
        values_[kParamSplitLow] = 250.0f;
        values_[kParamSplitHigh] = 4000.0f;
        values_[kParamMix] = 100.0f;
        values_[kParamTrim] = 0.0f;
        values_[kParamBypass] = 0.0f;
        values_[kParamDelta] = 0.0f;
        values_[kParamCCDepth] = 1.0f;
        values_[kParamCCSensitivity] = 2.0f;
        values_[kParamCCDecay] = 3.0f;
        values_[kParamCCMix] = 4.0f;
        values_[kParamCCChannel] = 1.0f;

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
        const float fv = static_cast<float>(std::atof(value));
        if      (std::strcmp(key, "mode")           == 0) { values_[kParamMode] = fv; }
        else if (std::strcmp(key, "depth")          == 0) { values_[kParamDepth] = fv; }
        else if (std::strcmp(key, "sensitivity")    == 0) { values_[kParamSensitivity] = fv; }
        else if (std::strcmp(key, "sharpness")      == 0) { values_[kParamSharpness] = fv; }
        else if (std::strcmp(key, "decay")          == 0) { values_[kParamDecay] = fv; }
        else if (std::strcmp(key, "decay_tilt")     == 0) { values_[kParamDecayTilt] = fv; }
        else if (std::strcmp(key, "split_low")      == 0) { values_[kParamSplitLow] = fv; }
        else if (std::strcmp(key, "split_high")     == 0) { values_[kParamSplitHigh] = fv; }
        else if (std::strcmp(key, "mix")            == 0) { values_[kParamMix] = fv; }
        else if (std::strcmp(key, "trim")           == 0) { values_[kParamTrim] = fv; }
        else if (std::strcmp(key, "bypass")         == 0) { values_[kParamBypass] = fv; }
        else if (std::strcmp(key, "delta")          == 0) { values_[kParamDelta] = fv; }
        else if (std::strcmp(key, "cc_depth")       == 0) { values_[kParamCCDepth] = fv; }
        else if (std::strcmp(key, "cc_sensitivity") == 0) { values_[kParamCCSensitivity] = fv; }
        else if (std::strcmp(key, "cc_decay")       == 0) { values_[kParamCCDecay] = fv; }
        else if (std::strcmp(key, "cc_mix")         == 0) { values_[kParamCCMix] = fv; }
        else if (std::strcmp(key, "cc_channel")     == 0) { values_[kParamCCChannel] = fv; }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        drawBackground(W, H);
        drawHeader(W);
        drawDivider(H);
        drawMainSliders();
        drawCCSection(W);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (!ev.press) {
            dragMain_ = -1;
            dragCC_   = -1;
            return false;
        }
        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            if (mainTrack(s).contains(mx, my)) {
                dragMain_ = s;
                updateMain(s, mx);
                return true;
            }
        }
        for (int s = 0; s < static_cast<int>(kCCSliders.size()); ++s) {
            if (ccTrack(s).contains(mx, my)) {
                dragCC_ = s;
                updateCC(s, mx);
                return true;
            }
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        if (dragMain_ >= 0) { updateMain(dragMain_, mx); return true; }
        if (dragCC_   >= 0) { updateCC(dragCC_,   mx); return true; }
        return false;
    }

private:
    const laf::Theme& t_ { laf::defaultTheme() };
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    std::array<float, kParameterCount> values_ {};
    int dragMain_ = -1;
    int dragCC_   = -1;

    [[nodiscard]] Rect mainTrack(int s) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, kSlidersY + s * kRowH + 20.0f, trackW, 12.0f };
    }

    [[nodiscard]] Rect ccTrack(int s) const noexcept
    {
        const float W      = static_cast<float>(getWidth());
        const float trackW = W - kCCX - kPad;
        return { kCCX, kSlidersY + s * kRowH + 20.0f, trackW, 12.0f };
    }

    void updateMain(int s, float mx)
    {
        const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
        const Rect tr        = mainTrack(s);
        const float t        = clampf((mx - tr.x) / tr.w, 0.0f, 1.0f);
        float v              = def.min + t * (def.max - def.min);
        if (def.integer) v   = std::round(v);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        setState(def.stateKey, buf);
        values_[def.index] = v;
        repaint();
    }

    void updateCC(int s, float mx)
    {
        const SliderDef& def = kCCSliders[static_cast<std::size_t>(s)];
        const Rect tr        = ccTrack(s);
        const float t        = clampf((mx - tr.x) / tr.w, 0.0f, 1.0f);
        float v              = std::round(def.min + t * (def.max - def.min));
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        setState(def.stateKey, buf);
        values_[def.index] = v;
        repaint();
    }

    void drawBackground(float W, float H)
    {
        beginPath();
        fc(t_.background);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawHeader(float W)
    {
        beginPath();
        fc(t_.panel);
        rect(0, 0, W, 58.0f);
        fill();
        closePath();

        fontSize(24.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(90, 170, 200, 255);
        text(kPad, 27.0f, "SPLIFF", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(110, 130, 140, 255);
        text(kPad + 110.0f, 27.0f,
             "adaptive transient processor \xc2\xb7 cut / boost \xc2\xb7 3-band \xc2\xb7 Drift CC", nullptr);

        beginPath();
        sc(t_.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, 58.0f);
        lineTo(W, 58.0f);
        stroke();
        closePath();

        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t_.border);
        text(kMainX, 62.0f, "PARAMETERS", nullptr);
        text(kCCX, 62.0f, "DRIFT CC ROUTING", nullptr);
    }

    void drawDivider(float H)
    {
        beginPath();
        sc(t_.panel);
        strokeWidth(1.0f);
        moveTo(kDivX, 58.0f);
        lineTo(kDivX, H);
        stroke();
        closePath();
    }

    void drawMainSliders()
    {
        const float trackW = kDivX - kMainX - kPad;

        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            const float labelY   = tr.y - 17.0f;

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(t_.textDim);
            text(kMainX, labelY, def.label, nullptr);

            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(90, 170, 200, 255);
            text(kMainX + trackW, labelY, formatMain(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 6.0f);
            fc(t_.panel);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 6.0f);
                fillColor(50, 140, 170, 255);
                fill();
                closePath();
            }
        }
    }

    void drawCCSection(float W)
    {
        const float trackW = W - kCCX - kPad;

        for (int s = 0; s < static_cast<int>(kCCSliders.size()); ++s) {
            const SliderDef& def = kCCSliders[static_cast<std::size_t>(s)];
            const Rect tr        = ccTrack(s);
            const float labelY   = tr.y - 17.0f;

            const bool isOff = isCCOff(def, values_[def.index]);
            const uint8_t dimAlpha = isOff ? 80 : 255;

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(160, 170, 175, dimAlpha);
            text(kCCX, labelY, def.label, nullptr);

            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(isOff ? 80 : 60, isOff ? 100 : 160, isOff ? 70 : 190, dimAlpha);
            text(kCCX + trackW, labelY, formatCC(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 6.0f);
            fc(t_.panel);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f && !isOff) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 6.0f);
                fillColor(40, 120, 150, 255);
                fill();
                closePath();
            }
        }

        const float hintY = kSlidersY + 5 * kRowH + 8.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t_.textDisabled);
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 Spliff MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "CC 0 = off. Defaults: 1=Depth  2=Sens  3=Decay  4=Mix", nullptr);
        text(kCCX, hintY + 38.0f, "Workflow: Delta on, raise Depth, tune Sens+Decay,", nullptr);
        text(kCCX, hintY + 52.0f, "then Delta off and set Mix. Bypass compares.", nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpliffUI)
};

UI* createUI()
{
    return new SpliffUI();
}

END_NAMESPACE_DISTRHO
