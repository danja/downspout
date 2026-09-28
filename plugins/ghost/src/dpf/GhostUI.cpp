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

// Must match GhostPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamSensitivity = 0,
    kParamDensity,
    kParamVelocity,
    kParamDrag,
    kParamMode,
    kParamChannel,
    kParamBaseNote,
    kParamPassInput,
    kParamSeed,
    kParamCCSensitivity,
    kParamCCDensity,
    kParamCCVelocity,
    kParamCCDrag,
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

constexpr std::array<SliderDef, 9> kMainSliders = {{
    { kParamSensitivity, "Sensitivity", 0.0f,   1.0f,   false, "sensitivity" },
    { kParamDensity,     "Density",     0.0f,   1.0f,   false, "density"     },
    { kParamVelocity,    "Velocity",    1.0f,   127.0f, true,  "velocity"    },
    { kParamDrag,        "Drag",        0.0f,   1.0f,   false, "drag"        },
    { kParamMode,        "Mode",  0.0f,   1.0f,   true,  "mode"        },
    { kParamChannel,     "Channel",     1.0f,   16.0f,  true,  "channel"     },
    { kParamBaseNote,    "Base Note",   0.0f,   127.0f, true,  "base_note"   },
    { kParamPassInput,   "Pass MIDI",   0.0f,   1.0f,   true,  "pass_input"  },
    { kParamSeed,        "Seed",        1.0f,   65535.0f, true,"seed"        },
}};

constexpr std::array<SliderDef, 5> kCCSliders = {{
    { kParamCCSensitivity, "CC Sensitivity", 0.0f, 127.0f, true, "cc_sensitivity" },
    { kParamCCDensity,     "CC Density",     0.0f, 127.0f, true, "cc_density"     },
    { kParamCCVelocity,    "CC Velocity",    0.0f, 127.0f, true, "cc_velocity"    },
    { kParamCCDrag,        "CC Drag",        0.0f, 127.0f, true, "cc_drag"        },
    { kParamCCChannel,     "CC Channel",     1.0f, 16.0f,  true, "cc_channel"     },
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::string formatMain(const SliderDef& def, float v)
{
    char buf[40];
    switch (def.index) {
    case kParamSensitivity:
    case kParamDensity:
    case kParamDrag:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
        break;
    case kParamVelocity:
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
        break;
    case kParamMode:
        std::snprintf(buf, sizeof(buf), "%s", v < 0.5f ? "Drums 10" : "Notes");
        break;
    case kParamChannel:
        std::snprintf(buf, sizeof(buf), "Ch %d", static_cast<int>(std::lround(v)));
        break;
    case kParamBaseNote: {
        static constexpr const char* kNames[12] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        const int n = static_cast<int>(std::lround(v));
        std::snprintf(buf, sizeof(buf), "%d %s%d", n, kNames[n % 12], n / 12 - 1);
        break;
    }
    case kParamPassInput:
        std::snprintf(buf, sizeof(buf), "%s", v < 0.5f ? "off" : "on");
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.0f", v);
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
constexpr float kDivX     = 400.0f;
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kRowH     = 52.0f;
constexpr float kSlidersY = 76.0f;

}  // namespace

class GhostUI : public UI
{
public:
    GhostUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamSensitivity] = 0.5f;
        values_[kParamDensity]     = 0.35f;
        values_[kParamVelocity]    = 90.0f;
        values_[kParamDrag]        = 0.15f;
        values_[kParamMode]        = 0.0f;
        values_[kParamChannel]     = 10.0f;
        values_[kParamBaseNote]    = 38.0f;
        values_[kParamPassInput]   = 1.0f;
        values_[kParamSeed]        = 7.0f;
        values_[kParamCCSensitivity] = 1.0f;
        values_[kParamCCDensity]     = 2.0f;
        values_[kParamCCVelocity]    = 3.0f;
        values_[kParamCCDrag]        = 4.0f;
        values_[kParamCCChannel]     = 1.0f;

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
        if      (std::strcmp(key, "sensitivity")   == 0) { values_[kParamSensitivity] = fv; }
        else if (std::strcmp(key, "density")       == 0) { values_[kParamDensity] = fv; }
        else if (std::strcmp(key, "velocity")      == 0) { values_[kParamVelocity] = fv; }
        else if (std::strcmp(key, "drag")          == 0) { values_[kParamDrag] = fv; }
        else if (std::strcmp(key, "mode")          == 0) { values_[kParamMode] = fv; }
        else if (std::strcmp(key, "channel")       == 0) { values_[kParamChannel] = fv; }
        else if (std::strcmp(key, "base_note")     == 0) { values_[kParamBaseNote] = fv; }
        else if (std::strcmp(key, "pass_input")    == 0) { values_[kParamPassInput] = fv; }
        else if (std::strcmp(key, "seed")          == 0) { values_[kParamSeed] = fv; }
        else if (std::strcmp(key, "cc_sensitivity")== 0) { values_[kParamCCSensitivity] = fv; }
        else if (std::strcmp(key, "cc_density")    == 0) { values_[kParamCCDensity] = fv; }
        else if (std::strcmp(key, "cc_velocity")   == 0) { values_[kParamCCVelocity] = fv; }
        else if (std::strcmp(key, "cc_drag")       == 0) { values_[kParamCCDrag] = fv; }
        else if (std::strcmp(key, "cc_channel")    == 0) { values_[kParamCCChannel] = fv; }
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
        return { kMainX, kSlidersY + s * kRowH + 22.0f, trackW, 14.0f };
    }

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
        fillColor(200, 170, 60, 255);
        text(kPad, 27.0f, "GHOST", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(150, 140, 110, 255);
        text(kPad + 110.0f, 27.0f,
             "audio in \xe2\x86\x92 ghost MIDI \xc2\xb7 BBT 16ths \xc2\xb7 drums 10 / notes \xc2\xb7 Drift CC", nullptr);

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
            const float labelY   = tr.y - 18.0f;

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(t_.textDim);
            text(kMainX, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(200, 170, 60, 255);
            text(kMainX + trackW, labelY, formatMain(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fc(t_.panel);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(170, 140, 50, 255);
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

            const bool isOff = isCCOff(def, values_[def.index]);
            const uint8_t dimAlpha = isOff ? 80 : 255;

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(170, 170, 150, dimAlpha);
            text(kCCX, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(isOff ? 80 : 190, isOff ? 100 : 160, isOff ? 70 : 60, dimAlpha);
            text(kCCX + trackW, labelY, formatCC(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fc(t_.panel);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f && !isOff) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(150, 120, 45, 255);
                fill();
                closePath();
            }
        }

        const float hintY = kSlidersY + 5 * kRowH + 8.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t_.textDisabled);
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 Ghost MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "CC 0 = off. Defaults: 1=Sens  2=Dens  3=Vel  4=Drag", nullptr);
        text(kCCX, hintY + 28.0f, "Routing CCs are consumed, other MIDI passes thru.", nullptr);
        text(kCCX, hintY + 52.0f, "Needs running transport: ghosts quantise to 16ths.", nullptr);
        text(kCCX, hintY + 66.0f, "Drag pushes ghosts late inside their slot.", nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostUI)
};

UI* createUI()
{
    return new GhostUI();
}

END_NAMESPACE_DISTRHO
