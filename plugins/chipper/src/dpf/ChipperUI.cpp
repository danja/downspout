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

// Must match ChipperPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamBitDepth = 0,
    kParamRateDiv,
    kParamJitter,
    kParamMix,
    kParamOutputGain,
    kParamCCBitDepth,
    kParamCCRateDiv,
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

constexpr std::array<SliderDef, 5> kMainSliders = {{
    { kParamBitDepth,   "Bit Depth",   1.0f,  16.0f, true,  "bit_depth"   },
    { kParamRateDiv,    "Rate Div",    1.0f,  64.0f, true,  "rate_div"    },
    { kParamJitter,     "Jitter",      0.0f,   1.0f, false, "jitter"      },
    { kParamMix,        "Mix",         0.0f, 100.0f, false, "mix"         },
    { kParamOutputGain, "Output Gain",-12.0f, 12.0f, false, "output_gain" },
}};

constexpr std::array<SliderDef, 3> kCCSliders = {{
    { kParamCCBitDepth, "CC Bit Depth", 0.0f, 127.0f, true, "cc_bit_depth" },
    { kParamCCRateDiv,  "CC Rate Div",  0.0f, 127.0f, true, "cc_rate_div"  },
    { kParamCCChannel,  "CC Channel",   1.0f,  16.0f, true, "cc_channel"   },
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::string formatMain(const SliderDef& def, float v)
{
    char buf[40];
    switch (def.index) {
    case kParamBitDepth: {
        const int bits   = static_cast<int>(std::lround(v));
        const int levels = (bits >= 16) ? 32767 : (1 << (bits - 1));
        std::snprintf(buf, sizeof(buf), "%d-bit (%d levels)", bits, levels);
        break;
    }
    case kParamRateDiv:
        std::snprintf(buf, sizeof(buf), "sr\xc3\xb7%d", static_cast<int>(std::lround(v)));
        break;
    case kParamJitter:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
        break;
    case kParamOutputGain:
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
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

// Layout constants
constexpr float kPad      = 16.0f;
constexpr float kDivX     = 448.0f;   // x where CC column starts
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kRowH     = 62.0f;
constexpr float kSlidersY = 76.0f;

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
        values_[kParamCCBitDepth] = 1.0f;
        values_[kParamCCRateDiv]  = 2.0f;
        values_[kParamCCChannel]  = 1.0f;

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
        if      (std::strcmp(key, "bit_depth")   == 0) { values_[kParamBitDepth]   = fv; }
        else if (std::strcmp(key, "rate_div")    == 0) { values_[kParamRateDiv]    = fv; }
        else if (std::strcmp(key, "jitter")      == 0) { values_[kParamJitter]     = fv; }
        else if (std::strcmp(key, "mix")         == 0) { values_[kParamMix]        = fv; }
        else if (std::strcmp(key, "output_gain") == 0) { values_[kParamOutputGain] = fv; }
        else if (std::strcmp(key, "cc_bit_depth")== 0) { values_[kParamCCBitDepth] = fv; }
        else if (std::strcmp(key, "cc_rate_div") == 0) { values_[kParamCCRateDiv]  = fv; }
        else if (std::strcmp(key, "cc_channel")  == 0) { values_[kParamCCChannel]  = fv; }
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
    std::array<float, kParameterCount> values_ {};
    int dragMain_ = -1;
    int dragCC_   = -1;

    // Main slider track: left column, full width up to divider
    [[nodiscard]] Rect mainTrack(int s) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, kSlidersY + s * kRowH + 22.0f, trackW, 14.0f };
    }

    // CC slider track: right column
    [[nodiscard]] Rect ccTrack(int s) const noexcept
    {
        const float W      = static_cast<float>(getWidth());
        const float trackW = W - kCCX - kPad;
        return { kCCX, kSlidersY + s * kRowH + 22.0f, trackW, 14.0f };
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
        text(kPad + 148.0f, 27.0f,
             "1980s video game \xe2\x80\x94 bit crush \xc2\xb7 rate reduction \xc2\xb7 jitter \xc2\xb7 Drift CC", nullptr);

        beginPath();
        strokeColor(40, 60, 40, 255);
        strokeWidth(1.0f);
        moveTo(0, 58.0f);
        lineTo(W, 58.0f);
        stroke();
        closePath();

        // Column headers
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(70, 100, 70, 255);
        text(kMainX, 62.0f, "PARAMETERS", nullptr);
        text(kCCX, 62.0f, "DRIFT CC ROUTING", nullptr);
    }

    void drawDivider(float H)
    {
        beginPath();
        strokeColor(32, 44, 32, 255);
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
            const float labelY   = tr.y - 18.0f;

            if (s > 0) {
                beginPath();
                strokeColor(28, 36, 28, 255);
                strokeWidth(1.0f);
                moveTo(kMainX, tr.y - 26.0f);
                lineTo(kMainX + trackW, tr.y - 26.0f);
                stroke();
                closePath();
            }

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(180, 210, 170, 255);
            text(kMainX, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(80, 210, 100, 255);
            text(kMainX + trackW, labelY, formatMain(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fillColor(30, 38, 30, 255);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(50, 180, 70, 255);
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
            const float labelY   = tr.y - 18.0f;

            if (s > 0) {
                beginPath();
                strokeColor(28, 36, 28, 255);
                strokeWidth(1.0f);
                moveTo(kCCX, tr.y - 26.0f);
                lineTo(kCCX + trackW, tr.y - 26.0f);
                stroke();
                closePath();
            }

            const bool isOff = (def.index != kParamCCChannel)
                             && (static_cast<int>(std::lround(values_[def.index])) == 0);
            const uint8_t dimAlpha = isOff ? 80 : 255;

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(160, 190, 150, dimAlpha);
            text(kCCX, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(isOff ? 80 : 60, isOff ? 100 : 200, isOff ? 70 : 90, dimAlpha);
            text(kCCX + trackW, labelY, formatCC(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fillColor(26, 34, 26, 255);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f && !isOff) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(40, 150, 60, 255);
                fill();
                closePath();
            }
        }

        // Usage hint below CC sliders
        const float hintY = kSlidersY + kCCSliders.size() * kRowH + 8.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(60, 80, 60, 255);
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 Chipper MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "CC 0 = off. Default: CC 1 = Bit Depth, CC 2 = Rate Div.", nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipperUI)
};

UI* createUI()
{
    return new ChipperUI();
}

END_NAMESPACE_DISTRHO
