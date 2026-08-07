#include "DistrhoUI.hpp"

#include "syrinx_params.hpp"
#include "syrinx_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::syrinx::kParameterCount;
using downspout::syrinx::kPresetCount;
using downspout::syrinx::kParamsPerPreset;
using downspout::syrinx::kPresetParamLevel;
using downspout::syrinx::kPresetParamNoise;
using downspout::syrinx::kPresetParamRoughness;
using downspout::syrinx::kPresetParamTimbre;
using downspout::syrinx::kPresetParamVibRate;
using downspout::syrinx::kPresetParamVibDepth;
using downspout::syrinx::kPresetParamBend;
using downspout::syrinx::kPresetParamHarmonic;
using downspout::syrinx::kPresetParamAMRate;
using downspout::syrinx::kPresetParamMute;
using downspout::syrinx::kParamDistance;
using downspout::syrinx::kParamMasterGain;
using downspout::syrinx::kParamSelectedPreset;
using downspout::syrinx::presetParam;
using downspout::syrinx::getParameterSpec;
using downspout::syrinx::kPresetNames;

constexpr std::size_t kVoiceControlCount = 9; // params per preset (excluding mute)
constexpr std::size_t kMasterControlCount = 2;

struct Rect {
    float x=0, y=0, w=0, h=0;
    [[nodiscard]] bool contains(float px, float py) const noexcept
    { return px>=x && px<=x+w && py>=y && py<=y+h; }
};

struct Color { int r, g, b; };

struct ControlDef { std::uint32_t parameter; const char* label; };

constexpr std::array<Color, kPresetCount> kPresetColors = {{
    {165, 200, 110}, // Wren        - olive green
    {120, 170, 210}, // Thrush      - slate blue
    {215, 185,  90}, // Warbler     - warm yellow
    {205, 115,  75}, // Finch       - burnt orange
    {150, 200, 135}, // Robin       - soft green
    { 90,  90, 120}, // Nightjar    - dark blue-grey
    {175, 150, 195}, // Pigeon      - mauve
    {100, 200, 175}, // Hummingbird - teal
    {205, 165,  90}, // Starling    - golden
    {155, 155, 155}, // Custom      - grey
}};

[[nodiscard]] float clampf(float v, float lo, float hi) { return std::max(lo, std::min(v, hi)); }

[[nodiscard]] float normalizedValue(std::uint32_t param, float value)
{
    const auto spec = getParameterSpec(param);
    if (spec.maximum <= spec.minimum) return 0.0f;
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

[[nodiscard]] std::string formatValue(std::uint32_t param, float value)
{
    char buf[32];
    const auto spec = getParameterSpec(param);
    const std::uint32_t pp = param % kParamsPerPreset;
    if (spec.boolean) {
        std::snprintf(buf, sizeof(buf), "%s", value >= 0.5f ? "on" : "off");
    } else if (pp == kPresetParamVibRate) {
        std::snprintf(buf, sizeof(buf), "%.1f Hz", value * 15.0f);
    } else if (pp == kPresetParamVibDepth) {
        std::snprintf(buf, sizeof(buf), "%.0f ct", value * 100.0f);
    } else if (pp == kPresetParamAMRate) {
        std::snprintf(buf, sizeof(buf), "%.1f Hz", value * 30.0f);
    } else if (pp == kPresetParamLevel) {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
    } else if (spec.maximum <= 1.0f && spec.minimum >= 0.0f) {
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    } else if (pp == kPresetParamBend) {
        std::snprintf(buf, sizeof(buf), "%+.2f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
    }
    return buf;
}

// Voice editor controls for the preset parameter editor
constexpr std::array<ControlDef, kVoiceControlCount> makeVoiceControls(std::uint32_t preset)
{
    return {{ // preset offset is added at draw time via presetParam()
        { kPresetParamLevel,     "Level"   },
        { kPresetParamNoise,     "Noise"   },
        { kPresetParamRoughness, "Rough"   },
        { kPresetParamTimbre,    "Timbre"  },
        { kPresetParamVibRate,   "Vib Rate"},
        { kPresetParamVibDepth,  "Vib Dep" },
        { kPresetParamBend,      "Bend"    },
        { kPresetParamHarmonic,  "Harmonic"},
        { kPresetParamAMRate,    "AM Rate" },
    }};
    (void)preset;
}

constexpr std::array<ControlDef, kMasterControlCount> kMasterControls = {{
    { kParamDistance,   "Distance" },
    { kParamMasterGain, "Gain"     },
}};

} // namespace

class SyrinxUI : public UI {
public:
    SyrinxUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = getParameterSpec(i).defaultValue;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < kParameterCount) {
            values_[index] = value;
            if (index == kParamSelectedPreset)
                selectedPreset_ = static_cast<int>(std::round(value));
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        const float pad = 22.0f;

        drawBackground(W, H);
        drawHeader(pad, pad, W - pad * 2.0f, 72.0f);
        drawPresetStrips(pad, 112.0f, W - pad * 2.0f, 258.0f);
        drawVoiceEditor(pad, 404.0f, W * 0.62f - pad * 1.5f, H - 426.0f);
        drawMasterPanel(W * 0.62f + pad * 0.5f, 404.0f, W * 0.38f - pad * 1.5f, H - 426.0f);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) { activeControlParam_ = -1; activeLevelStrip_ = -1; return false; }

        // Randomize button
        if (randomizeRect_.contains(x, y)) {
            const auto seed = static_cast<std::uint32_t>(std::time(nullptr));
            randomizeSelectedPreset(seed);
            return true;
        }

        for (std::size_t i = 0; i < kPresetCount; ++i) {
            if (muteRects_[i].contains(x, y)) {
                const std::uint32_t p = presetParam(static_cast<std::uint32_t>(i), kPresetParamMute);
                commitParameter(p, values_[p] >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
            if (levelRects_[i].contains(x, y)) {
                selectedPreset_ = static_cast<int>(i);
                activeLevelStrip_ = selectedPreset_;
                updateLevelFromPosition(selectedPreset_, y);
                commitParameter(kParamSelectedPreset, static_cast<float>(selectedPreset_));
                return true;
            }
            if (stripRects_[i].contains(x, y)) {
                selectedPreset_ = static_cast<int>(i);
                commitParameter(kParamSelectedPreset, static_cast<float>(selectedPreset_));
                return true;
            }
        }

        for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
            if (controlRects_[i].contains(x, y)) {
                activeControlParam_ = static_cast<int>(
                    presetParam(static_cast<std::uint32_t>(selectedPreset_), static_cast<std::uint32_t>(i)));
                updateControlFromPosition(activeControlParam_, x, controlRects_[i]);
                return true;
            }
        }

        for (std::size_t i = 0; i < kMasterControlCount; ++i) {
            if (masterRects_[i].contains(x, y)) {
                activeControlParam_ = static_cast<int>(kMasterControls[i].parameter);
                updateControlFromPosition(activeControlParam_, x, masterRects_[i]);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeLevelStrip_ >= 0) {
            updateLevelFromPosition(activeLevelStrip_, static_cast<float>(ev.pos.getY()));
            return true;
        }
        if (activeControlParam_ >= 0) {
            const std::uint32_t p = static_cast<std::uint32_t>(activeControlParam_);
            for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
                if (presetParam(static_cast<std::uint32_t>(selectedPreset_), static_cast<std::uint32_t>(i)) == p) {
                    updateControlFromPosition(activeControlParam_, static_cast<float>(ev.pos.getX()), controlRects_[i]);
                    return true;
                }
            }
            for (std::size_t i = 0; i < kMasterControlCount; ++i) {
                if (kMasterControls[i].parameter == p) {
                    updateControlFromPosition(activeControlParam_, static_cast<float>(ev.pos.getX()), masterRects_[i]);
                    return true;
                }
            }
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());
        const float dir = ev.delta.getY() > 0.0f ? 1.0f : -1.0f;

        for (std::size_t i = 0; i < kPresetCount; ++i) {
            if (levelRects_[i].contains(x, y)) {
                nudgeParameter(presetParam(static_cast<std::uint32_t>(i), kPresetParamLevel), dir * 0.03f);
                return true;
            }
        }
        for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
            if (controlRects_[i].contains(x, y)) {
                nudgeParameter(presetParam(static_cast<std::uint32_t>(selectedPreset_), static_cast<std::uint32_t>(i)), dir * 0.02f);
                return true;
            }
        }
        for (std::size_t i = 0; i < kMasterControlCount; ++i) {
            if (masterRects_[i].contains(x, y)) {
                nudgeParameter(kMasterControls[i].parameter, dir * 0.02f);
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, kParameterCount> values_{};
    std::array<Rect, kPresetCount>     stripRects_{};
    std::array<Rect, kPresetCount>     muteRects_{};
    std::array<Rect, kPresetCount>     levelRects_{};
    std::array<Rect, kVoiceControlCount>   controlRects_{};
    std::array<Rect, kMasterControlCount>  masterRects_{};
    Rect randomizeRect_{};

    int selectedPreset_     = 0;
    int activeLevelStrip_   = -1;
    int activeControlParam_ = -1;

    // ---- Drawing ----

    void drawBackground(float W, float H)
    {
        beginPath(); fillColor(13,15,17,255); rect(0,0,W,H); fill(); closePath();
        beginPath(); fillColor(31,35,38,255); rect(0,0,W,386.0f); fill(); closePath();
        beginPath(); fillColor(58,100,108,26); rect(0,386.0f,W,H-386.0f); fill(); closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x,y,w,h,7.0f); fillColor(18,22,25,238); fill(); closePath();

        fontSize(31.0f); textAlign(ALIGN_LEFT|ALIGN_TOP);
        fillColor(238,241,239,255);
        text(x+22.0f, y+15.0f, "Syrinx", nullptr);

        fontSize(13.0f); fillColor(140,165,160,255);
        text(x+145.0f, y+24.0f, "Mindlin-Laje syrinx model · chromatic MIDI · 10 presets", nullptr);

        // Randomize button
        const float rx = x + w - 152.0f;
        const float ry = y + 18.0f;
        randomizeRect_ = { rx, ry, 130.0f, 36.0f };
        beginPath(); roundedRect(rx,ry,130.0f,36.0f,7.0f); fillColor(58,110,88,255); fill(); closePath();
        fontSize(14.0f); textAlign(ALIGN_CENTER|ALIGN_MIDDLE); fillColor(200,235,215,255);
        text(rx+65.0f, ry+19.0f, "RANDOMIZE", nullptr);
    }

    void drawPresetStrips(float x, float y, float w, float h)
    {
        const float gap    = 8.0f;
        const float stripW = (w - gap * static_cast<float>(kPresetCount - 1)) / static_cast<float>(kPresetCount);
        for (std::size_t i = 0; i < kPresetCount; ++i) {
            const Rect strip{ x + static_cast<float>(i) * (stripW + gap), y, stripW, h };
            stripRects_[i] = strip;
            drawStrip(i, strip);
        }
    }

    void drawStrip(std::size_t idx, const Rect& rect)
    {
        const bool   selected = static_cast<int>(idx) == selectedPreset_;
        const bool   muted    = values_[presetParam(static_cast<std::uint32_t>(idx), kPresetParamMute)] >= 0.5f;
        const Color& col      = kPresetColors[idx];

        beginPath(); roundedRect(rect.x,rect.y,rect.w,rect.h,7.0f);
        fillColor(selected?34:20, selected?40:24, selected?43:27, 255);
        fill();
        strokeColor(selected?col.r:52, selected?col.g:58, selected?col.b:60, selected?220:180);
        strokeWidth(selected?2.0f:1.0f); stroke(); closePath();

        // Coloured header bar
        beginPath(); roundedRect(rect.x+8.0f, rect.y+8.0f, rect.w-16.0f, 6.0f, 3.0f);
        fillColor(col.r, col.g, col.b, muted?70:220); fill(); closePath();

        // Preset name
        fontSize(10.0f); textAlign(ALIGN_CENTER|ALIGN_TOP);
        fillColor(215,222,218, muted?120:255);
        text(rect.x+rect.w*0.5f, rect.y+24.0f, kPresetNames[idx], nullptr);

        // Pitch sparkline (simple static visual representation of Bend param)
        drawBendSparkline(rect, idx, col, muted);

        // Level fader
        const Rect meter{ rect.x+rect.w*0.5f-8.0f, rect.y+90.0f, 16.0f, 110.0f };
        levelRects_[idx] = meter;
        drawLevelFader(static_cast<std::uint32_t>(idx), meter, col, muted);

        // Mute button
        const Rect mute{ rect.x+10.0f, rect.y+rect.h-42.0f, rect.w-20.0f, 28.0f };
        muteRects_[idx] = mute;
        drawMuteButton(mute, col, muted);
    }

    void drawBendSparkline(const Rect& rect, std::size_t idx, const Color& col, bool muted)
    {
        const float bend = values_[presetParam(static_cast<std::uint32_t>(idx), kPresetParamBend)];
        const float sx = rect.x + 12.0f;
        const float sw = rect.w - 24.0f;
        const float sy = rect.y + 52.0f;
        const float sh = 22.0f;

        // Background
        beginPath(); roundedRect(sx, sy, sw, sh, 3.0f);
        fillColor(10, 12, 14, 200); fill(); closePath();

        // Curve: draw a simple arc representing the bend direction
        const float midX = sx + sw * 0.5f;
        const float midY = sy + sh * 0.5f;
        const float endY  = midY - bend * sh * 0.38f;
        const float startY= midY + bend * sh * 0.38f;

        beginPath();
        moveTo(sx + 4.0f, startY);
        quadTo(midX, midY + bend * sh * 0.1f, sx + sw - 4.0f, endY);
        strokeColor(col.r, col.g, col.b, muted ? 80 : 200);
        strokeWidth(1.5f); stroke(); closePath();
    }

    void drawLevelFader(std::uint32_t idx, const Rect& rect, const Color& col, bool muted)
    {
        const std::uint32_t levelParam = presetParam(idx, kPresetParamLevel);
        const float norm = normalizedValue(levelParam, values_[levelParam]);

        beginPath(); roundedRect(rect.x,rect.y,rect.w,rect.h,6.0f);
        fillColor(9,11,12,255); fill(); closePath();

        const float fillH = rect.h * norm;
        if (fillH > 2.0f) {
            beginPath();
            roundedRect(rect.x+3.0f, rect.y+rect.h-fillH+3.0f, rect.w-6.0f, fillH-6.0f, 4.0f);
            fillColor(col.r, col.g, col.b, muted?60:210); fill(); closePath();
        }
        const float thumbY = rect.y + rect.h - rect.h * norm;
        beginPath(); roundedRect(rect.x-10.0f, thumbY-4.0f, rect.w+20.0f, 8.0f, 4.0f);
        fillColor(228,232,230, muted?80:220); fill(); closePath();
    }

    void drawMuteButton(const Rect& rect, const Color& col, bool muted)
    {
        beginPath(); roundedRect(rect.x,rect.y,rect.w,rect.h,6.0f);
        fillColor(muted?col.r:28, muted?col.g:33, muted?col.b:35, muted?225:255);
        fill(); closePath();
        fontSize(12.0f); textAlign(ALIGN_CENTER|ALIGN_MIDDLE);
        fillColor(muted?12:185, muted?14:195, muted?15:195, 255);
        text(rect.x+rect.w*0.5f, rect.y+rect.h*0.5f+1.0f, "M", nullptr);
    }

    void drawVoiceEditor(float x, float y, float w, float h)
    {
        const Color& col = kPresetColors[static_cast<std::size_t>(selectedPreset_)];
        drawPanel(x, y, w, h);

        fontSize(17.0f); textAlign(ALIGN_LEFT|ALIGN_TOP);
        fillColor(232,237,235,255);
        text(x+18.0f, y+17.0f, kPresetNames[static_cast<std::size_t>(selectedPreset_)], nullptr);

        fontSize(12.0f); fillColor(col.r, col.g, col.b, 255);
        text(x+18.0f, y+42.0f, "play with MIDI keyboard · chromatic pitch tracking", nullptr);

        const float startY = y + 80.0f;
        const float rowH   = 38.0f;
        const float rowGap = 9.0f;

        const auto controls = makeVoiceControls(static_cast<std::uint32_t>(selectedPreset_));
        for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
            const Rect rect{ x+18.0f, startY + static_cast<float>(i)*(rowH+rowGap), w-36.0f, rowH };
            controlRects_[i] = rect;
            const std::uint32_t paramIdx = presetParam(static_cast<std::uint32_t>(selectedPreset_),
                                                       static_cast<std::uint32_t>(controls[i].parameter));
            drawHorizontalControl({ paramIdx, controls[i].label }, rect, col);
        }
    }

    void drawMasterPanel(float x, float y, float w, float h)
    {
        const Color col{ 88, 165, 158 };
        drawPanel(x, y, w, h);

        fontSize(17.0f); textAlign(ALIGN_LEFT|ALIGN_TOP);
        fillColor(232,237,235,255); text(x+18.0f, y+17.0f, "Master", nullptr);

        fontSize(12.0f); fillColor(140,152,152,255);
        text(x+18.0f, y+42.0f, "distance reverb · output gain", nullptr);

        const float startY = y + 82.0f;
        const float rowH   = 42.0f;
        const float rowGap = 14.0f;
        for (std::size_t i = 0; i < kMasterControlCount; ++i) {
            const Rect rect{ x+18.0f, startY+static_cast<float>(i)*(rowH+rowGap), w-36.0f, rowH };
            masterRects_[i] = rect;
            drawHorizontalControl(kMasterControls[i], rect, col);
        }

        // Selected preset info
        fontSize(11.0f); fillColor(100, 130, 128, 255); textAlign(ALIGN_LEFT|ALIGN_TOP);
        char buf[48];
        std::snprintf(buf, sizeof(buf), "Active: %s (preset %d)", kPresetNames[selectedPreset_], selectedPreset_+1);
        text(x+18.0f, startY + 2.0f*(rowH+rowGap) + 16.0f, buf, nullptr);

        fontSize(10.0f); fillColor(80, 105, 103, 255);
        text(x+18.0f, startY + 2.0f*(rowH+rowGap) + 34.0f, "CC1=vib  CC74=timbre  CC71=rough  CC91=dist", nullptr);
    }

    void drawPanel(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x,y,w,h,7.0f);
        fillColor(18,22,24,238); fill();
        strokeColor(45,54,56,255); strokeWidth(1.0f); stroke(); closePath();
    }

    void drawHorizontalControl(const ControlDef& ctrl, const Rect& rect, const Color& col)
    {
        const float value = values_[ctrl.parameter];
        const float norm  = normalizedValue(ctrl.parameter, value);
        const std::string txt = formatValue(ctrl.parameter, value);

        fontSize(12.0f); textAlign(ALIGN_LEFT|ALIGN_TOP);
        fillColor(208,215,213,255);
        text(rect.x, rect.y-2.0f, ctrl.label, nullptr);

        textAlign(ALIGN_RIGHT|ALIGN_TOP); fillColor(col.r,col.g,col.b,255);
        text(rect.x+rect.w, rect.y-2.0f, txt.c_str(), nullptr);

        const float barY = rect.y + 20.0f;
        beginPath(); roundedRect(rect.x, barY, rect.w, 12.0f, 6.0f);
        fillColor(35,41,43,255); fill(); closePath();

        beginPath(); roundedRect(rect.x, barY, std::max(8.0f, rect.w*norm), 12.0f, 6.0f);
        fillColor(col.r,col.g,col.b,210); fill(); closePath();

        beginPath(); roundedRect(rect.x+rect.w*norm-5.0f, barY-4.0f, 10.0f, 20.0f, 5.0f);
        fillColor(234,239,236,255); fill(); closePath();
    }

    // ---- Interaction helpers ----

    void updateLevelFromPosition(int strip, float y)
    {
        if (strip < 0 || strip >= static_cast<int>(kPresetCount)) return;
        const Rect& rect = levelRects_[static_cast<std::size_t>(strip)];
        const float norm = clampf(1.0f - (y - rect.y) / rect.h, 0.0f, 1.0f);
        const std::uint32_t p = presetParam(static_cast<std::uint32_t>(strip), kPresetParamLevel);
        const auto spec = getParameterSpec(p);
        commitParameter(p, spec.minimum + norm * (spec.maximum - spec.minimum));
    }

    void updateControlFromPosition(int paramIndex, float x, const Rect& rect)
    {
        if (paramIndex < 0 || paramIndex >= static_cast<int>(kParameterCount)) return;
        const std::uint32_t p = static_cast<std::uint32_t>(paramIndex);
        const auto spec = getParameterSpec(p);
        const float norm = clampf((x - rect.x) / rect.w, 0.0f, 1.0f);
        commitParameter(p, spec.minimum + norm * (spec.maximum - spec.minimum));
    }

    void nudgeParameter(std::uint32_t p, float delta)
    {
        const auto spec = getParameterSpec(p);
        commitParameter(p, values_[p] + delta * (spec.maximum - spec.minimum));
    }

    void commitParameter(std::uint32_t p, float value)
    {
        if (p >= kParameterCount) return;
        const auto spec = getParameterSpec(p);
        const float clamped = spec.boolean
            ? (value >= 0.5f ? 1.0f : 0.0f)
            : clampf(value, spec.minimum, spec.maximum);
        editParameter(p, true);
        setParameterValue(p, clamped);
        editParameter(p, false);
        values_[p] = clamped;
        repaint();
    }

    void randomizeSelectedPreset(std::uint32_t seed)
    {
        const auto p = static_cast<std::uint32_t>(selectedPreset_);
        auto lcg = [&]() -> float {
            seed = seed * 1664525u + 1013904223u;
            return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
        };
        auto rr = [&](float lo, float hi) { return lo + lcg() * (hi - lo); };

        commitParameter(presetParam(p, kPresetParamLevel),     rr(0.5f,  1.2f));
        commitParameter(presetParam(p, kPresetParamNoise),     rr(0.0f,  0.4f));
        commitParameter(presetParam(p, kPresetParamRoughness), rr(0.0f,  0.3f));
        commitParameter(presetParam(p, kPresetParamTimbre),    rr(0.0f,  0.8f));
        commitParameter(presetParam(p, kPresetParamVibRate),   rr(0.1f,  0.8f));
        commitParameter(presetParam(p, kPresetParamVibDepth),  rr(0.0f,  0.4f));
        commitParameter(presetParam(p, kPresetParamBend),      rr(-0.7f, 0.7f));
        commitParameter(presetParam(p, kPresetParamHarmonic),  rr(0.0f,  1.0f));
        commitParameter(presetParam(p, kPresetParamAMRate),    rr(0.0f,  0.3f));
        // preserve mute
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyrinxUI)
};

UI* createUI() { return new SyrinxUI(); }

END_NAMESPACE_DISTRHO
