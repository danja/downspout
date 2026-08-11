#include "DistrhoUI.hpp"

#include "flues_synth_driver_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using namespace downspout::flues_synth_driver;

struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

float clampf(float v, float lo, float hi) noexcept { return std::max(lo, std::min(v, hi)); }

std::string fmtValue(uint32_t param, float value)
{
    char buf[32];
    switch (static_cast<Param>(param)) {
    case kParamF1: case kParamF2: case kParamF3: case kParamF4: case kParamFilterFreq:
        if (value >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.1fkHz", value/1000.0f);
        else                  std::snprintf(buf, sizeof(buf), "%.0fHz", value);
        return buf;
    case kParamAttack: case kParamRelease:
        if (value < 0.1f) std::snprintf(buf, sizeof(buf), "%.0fms", value*1000.0f);
        else              std::snprintf(buf, sizeof(buf), "%.2fs", value);
        return buf;
    case kParamLfoFreq:
        std::snprintf(buf, sizeof(buf), "%.1fHz", value); return buf;
    case kParamTuning:
        std::snprintf(buf, sizeof(buf), "%+.0fst", value); return buf;
    case kParamDelayRatio:
        std::snprintf(buf, sizeof(buf), "%.2fx", value); return buf;
    case kParamAmFmDepth:
        std::snprintf(buf, sizeof(buf), "%+.0f%%", value*100.0f); return buf;
    case kParamFilterQ:
        std::snprintf(buf, sizeof(buf), "%.1f", value); return buf;
    case kParamTrajSides:
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(value)); return buf;
    case kParamTrajStartPos: case kParamTrajStartAngle:
        std::snprintf(buf, sizeof(buf), "%.0f°", value); return buf;
    case kParamTrajJitter:
        std::snprintf(buf, sizeof(buf), "%.1f°", value); return buf;
    case kParamAlgorithm:
        return kAlgorithmNames[std::clamp(static_cast<int>(value), 0, 17)];
    case kParamInterfaceType:
        return kInterfaceTypeNames[std::clamp(static_cast<int>(value), 0, 11)];
    default: {
        const std::size_t si = static_cast<std::size_t>(param) >= static_cast<std::size_t>(kParamAlgorithm)
            ? static_cast<std::size_t>(param) - static_cast<std::size_t>(kParamAlgorithm) : kSynthParamCount;
        const float lo = si < kSynthParamCount ? kSynthCCs[si].minimum : 0.0f;
        const float hi = si < kSynthParamCount ? kSynthCCs[si].maximum : 1.0f;
        const float n  = clampf((value - lo) / (hi - lo + 1e-6f), 0.0f, 1.0f);
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(n * 100.0f)));
        return buf;
    }
    }
}

struct Knob {
    uint32_t    param;
    const char* label;
    float       min, max;
    bool        integer;
};

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
class FlueSynthDriverUI : public UI
{
public:
    FlueSynthDriverUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        for (std::size_t i = 0; i < kSynthParamCount; ++i)
            values_[kParamAlgorithm + i] = kSynthCCs[i].defaultValue;
        values_[kParamOutputChannel] = 1.0f;
        values_[kParamPassInput]     = 1.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < static_cast<uint32_t>(kParamCount)) {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        clearWidgets();

        const float W   = static_cast<float>(getWidth());
        const float H   = static_cast<float>(getHeight());
        const float pad = 18.0f;

        drawBackground(W, H);

        const float headerY  = pad;
        const float routingY = headerY + 72.0f + 8.0f;
        const float mainY    = routingY + 48.0f + 8.0f;

        drawHeader(pad, headerY, W - pad*2.0f, 72.0f);
        drawRoutingBar(pad, routingY, W - pad*2.0f, 48.0f);
        drawMainArea(pad, mainY, W - pad*2.0f, H - mainY - pad);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) { activeKnob_ = -1; return false; }

        for (std::size_t i = 0; i < toggleRects_.size(); ++i) {
            if (toggleRects_[i].contains(x, y)) {
                const uint32_t p = toggleParams_[i];
                setVal(p, values_[p] >= 0.5f ? 0.0f : 1.0f);
                return true;
            }
        }
        for (std::size_t i = 0; i < selectorRects_.size(); ++i) {
            if (selectorRects_[i].contains(x, y)) {
                cycleSelector(i, 1);
                return true;
            }
        }
        for (std::size_t i = 0; i < knobRects_.size(); ++i) {
            if (knobRects_[i].contains(x, y)) {
                activeKnob_   = static_cast<int>(i);
                dragStartY_   = y;
                dragStartVal_ = values_[knobs_[i].param];
                return true;
            }
        }
        if (panicRect_.contains(x, y)) { trigVal(kParamPanic); return true; }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeKnob_ < 0) return false;
        const float dy  = dragStartY_ - static_cast<float>(ev.pos.getY());
        const Knob& k   = knobs_[activeKnob_];
        float newVal = clampf(dragStartVal_ + dy / 200.0f * (k.max - k.min), k.min, k.max);
        if (k.integer) newVal = std::round(newVal);
        setVal(k.param, newVal);
        return true;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x  = static_cast<float>(ev.pos.getX());
        const float y  = static_cast<float>(ev.pos.getY());
        const int delta = ev.delta.getY() > 0.0f ? 1 : -1;

        for (std::size_t i = 0; i < selectorRects_.size(); ++i) {
            if (selectorRects_[i].contains(x, y)) { cycleSelector(i, delta); return true; }
        }
        for (std::size_t i = 0; i < knobRects_.size(); ++i) {
            if (knobRects_[i].contains(x, y)) {
                const Knob& k  = knobs_[i];
                const float step = k.integer ? 1.0f : (k.max - k.min) * 0.01f;
                setVal(k.param, clampf(values_[k.param] + delta * step, k.min, k.max));
                return true;
            }
        }
        return false;
    }

private:
    std::array<float, static_cast<std::size_t>(kParamCount)> values_ {};

    std::vector<Rect>     knobRects_ {};
    std::vector<Knob>     knobs_ {};
    std::vector<Rect>     toggleRects_ {};
    std::vector<uint32_t> toggleParams_ {};
    std::vector<Rect>     selectorRects_ {};
    std::vector<uint32_t> selectorParams_ {};
    std::vector<int>      selectorCounts_ {};

    Rect  panicRect_    {};
    int   activeKnob_   = -1;
    float dragStartY_   = 0.0f;
    float dragStartVal_ = 0.0f;

    void clearWidgets()
    {
        knobRects_.clear(); knobs_.clear();
        toggleRects_.clear(); toggleParams_.clear();
        selectorRects_.clear(); selectorParams_.clear(); selectorCounts_.clear();
    }

    // ── Interaction ───────────────────────────────────────────────────────────

    void setVal(uint32_t param, float v)
    {
        editParameter(param, true);
        setParameterValue(param, v);
        editParameter(param, false);
        values_[param] = v;
        repaint();
    }

    void trigVal(uint32_t param)
    {
        editParameter(param, true);
        setParameterValue(param, 1.0f);
        setParameterValue(param, 0.0f);
        editParameter(param, false);
        repaint();
    }

    void cycleSelector(std::size_t i, int delta)
    {
        const uint32_t p     = selectorParams_[i];
        const int      count = selectorCounts_[i];
        int cur = static_cast<int>(std::lround(values_[p]));
        cur     = ((cur + delta) % count + count) % count;
        setVal(p, static_cast<float>(cur));
    }

    // ── Draw primitives ───────────────────────────────────────────────────────

    void drawBackground(float W, float H)
    {
        beginPath(); fillColor(8, 12, 18, 255);
        rect(0, 0, W, H); fill(); closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 14.0f);
        fillColor(16, 21, 30, 245); fill(); closePath();
        beginPath(); roundedRect(x, y, w, 3.0f, 2.0f);
        fillColor(78, 155, 220, 255); fill(); closePath();

        fontSize(24.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(235, 240, 248, 255);
        text(x + 18.0f, y + h * 0.5f - 8.0f, "FlueSynth Driver", nullptr);

        fontSize(11.5f); fillColor(120, 138, 160, 255);
        text(x + 18.0f, y + h * 0.5f + 14.0f,
             "MIDI controller and UI for an external flues-synth instance. "
             "Pass input → synth notes. UI parameters → MIDI CCs.", nullptr);

        // MIDI activity pill
        const float act = values_[kParamMidiActivity];
        const int   ar  = static_cast<int>(78 + act * 100);
        beginPath(); roundedRect(x + w - 132.0f, y + 16.0f, 114.0f, 26.0f, 13.0f);
        fillColor(ar, 190, 120, static_cast<int>(30 + act * 60)); fill();
        strokeColor(ar, 190, 130, static_cast<int>(140 + act * 80));
        strokeWidth(1.0f); stroke(); closePath();
        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(ar, 210, 160, 255);
        text(x + w - 75.0f, y + 29.0f,
             act > 0.05f ? "MIDI Active" : "MIDI Idle", nullptr);
    }

    void drawRoutingBar(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 12.0f);
        fillColor(16, 21, 30, 240); fill(); closePath();

        float cx = x + 12.0f;

        const int outCh = static_cast<int>(values_[kParamOutputChannel]);
        const std::string outLabel = "Out Ch " + std::to_string(outCh);
        addSelector({cx, y + 8.0f, 120.0f, 32.0f}, kParamOutputChannel, 16,
                    outLabel.c_str(), 78, 155, 220);
        cx += 132.0f;

        const int condCh = static_cast<int>(values_[kParamConductorCh]);
        const std::string condLabel = condCh == 0
            ? "Cond: Off" : ("Cond Ch " + std::to_string(condCh));
        addSelector({cx, y + 8.0f, 120.0f, 32.0f}, kParamConductorCh, 17,
                    condLabel.c_str(), 185, 140, 220);
        cx += 132.0f;

        const bool pass = values_[kParamPassInput] >= 0.5f;
        addToggle({cx, y + 8.0f, 106.0f, 32.0f}, kParamPassInput,
                  pass ? "Pass In" : "Block In", pass, 110, 185, 140);

        panicRect_ = {x + w - 104.0f, y + 8.0f, 92.0f, 32.0f};
        beginPath(); roundedRect(panicRect_.x, panicRect_.y, panicRect_.w, panicRect_.h, 10.0f);
        fillColor(200, 70, 70, 44); fill();
        strokeColor(200, 90, 90, 180); strokeWidth(1.2f); stroke(); closePath();
        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(240, 180, 180, 255);
        text(panicRect_.x + panicRect_.w * 0.5f, panicRect_.y + panicRect_.h * 0.5f + 1.0f,
             "Panic", nullptr);
    }

    void drawMainArea(float x, float y, float w, float h)
    {
        const float rowH = (h - 10.0f) * 0.5f;
        const float y2   = y + rowH + 10.0f;

        // Top row: Oscillator | Model | Formants | Vocal+Env
        const float cw0 = w * 0.25f - 5.0f;
        const float cw1 = w * 0.16f - 5.0f;
        const float cw2 = w * 0.24f - 5.0f;
        const float cw3 = w - cw0 - cw1 - cw2 - 15.0f;

        drawGroupOscillator(x,                           y, cw0, rowH);
        drawGroupModel     (x + cw0 + 5.0f,             y, cw1, rowH);
        drawGroupFormants  (x + cw0 + cw1 + 10.0f,      y, cw2, rowH);
        drawGroupVocalEnv  (x + cw0 + cw1 + cw2 + 15.0f,y, cw3, rowH);

        // Bottom row: FX | Filter | LFO | Trajectory
        const float bw0 = w * 0.30f - 5.0f;
        const float bw1 = w * 0.16f - 5.0f;
        const float bw2 = w * 0.14f - 5.0f;
        const float bw3 = w - bw0 - bw1 - bw2 - 15.0f;

        drawGroupFX        (x,                           y2, bw0, rowH);
        drawGroupFilter    (x + bw0 + 5.0f,             y2, bw1, rowH);
        drawGroupLFO       (x + bw0 + bw1 + 10.0f,     y2, bw2, rowH);
        drawGroupTrajectory(x + bw0 + bw1 + bw2 + 15.0f,y2, bw3, rowH);
    }

    // ── Group panels ──────────────────────────────────────────────────────────

    void drawGroupOscillator(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Oscillator", 78, 140, 200);
        const float ky = y + 36.0f;

        const int alg = std::clamp(static_cast<int>(values_[kParamAlgorithm]), 0, 17);
        addSelector({x + 8.0f, ky, w - 16.0f, 28.0f},
                    kParamAlgorithm, 18, kAlgorithmNames[alg], 78, 140, 200);

        // P1 P2 P3 Gain — four equal knobs
        const float kh = h - 80.0f;
        const float kw = (w - 38.0f) / 4.0f;
        const float py = ky + 36.0f;
        addKnob({x + 8.0f,               py, kw, kh}, {kParamDisynP1,"P1",  0.0f,1.0f,false}, 78,155,220);
        addKnob({x + 8.0f + (kw+6.0f),   py, kw, kh}, {kParamDisynP2,"P2",  0.0f,1.0f,false}, 78,155,220);
        addKnob({x + 8.0f + (kw+6.0f)*2, py, kw, kh}, {kParamDisynP3,"P3",  0.0f,1.0f,false}, 78,155,220);
        addKnob({x + 8.0f + (kw+6.0f)*3, py, kw, kh}, {kParamGain,  "Gain",0.0f,1.0f,false}, 210,170,90);
    }

    void drawGroupModel(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Model", 140, 100, 200);
        const float ky = y + 36.0f;

        const int it = std::clamp(static_cast<int>(values_[kParamInterfaceType]), 0, 11);
        addSelector({x + 8.0f, ky, w - 16.0f, 28.0f},
                    kParamInterfaceType, 12, kInterfaceTypeNames[it], 140, 100, 200);

        const float kh = h - 80.0f;
        const float kw = (w - 22.0f) * 0.5f;
        const float py = ky + 36.0f;
        addKnob({x + 6.0f,       py, kw, kh}, {kParamIntensity,"Intensity",0.0f,1.0f,false}, 155,120,210);
        addKnob({x + 6.0f+kw+10.f,py, kw, kh}, {kParamTuning,  "Tuning", -12.0f,12.0f,true},  180,160,100);
    }

    void drawGroupFormants(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Formants", 80, 190, 150);
        const float ky = y + 32.0f;
        const float kw = (w - 30.0f) / 4.0f;
        const float kh = h - 42.0f;
        const Knob ks[] = {
            {kParamF1,"F1 Jaw",  200.0f,1000.0f,false},
            {kParamF2,"F2 Tng",  500.0f,3000.0f,false},
            {kParamF3,"F3 Lips",1500.0f,4000.0f,false},
            {kParamF4,"F4 Qual",2500.0f,4500.0f,false},
        };
        for (int i = 0; i < 4; ++i)
            addKnob({x + 8.0f + i*(kw+6.0f), ky, kw, kh}, ks[i], 80,190,150);
    }

    void drawGroupVocalEnv(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Vocal / Env", 210, 155, 80);
        const float ky = y + 34.0f;
        const float tw = (w - 24.0f) * 0.5f;

        const struct { uint32_t p; const char* lbl; } vt[] = {
            {kParamNasal,"Nasal"},{kParamSing, "Sing"},
            {kParamShout,"Shout"},{kParamFry,  "Fry"},
        };
        for (int i = 0; i < 4; ++i) {
            addToggle({x + 8.0f + (i%2)*(tw+8.0f), ky + (i/2)*38.0f, tw, 30.0f},
                      vt[i].p, vt[i].lbl, values_[vt[i].p] >= 0.5f, 210,155,80);
        }

        const float envY = ky + 86.0f;
        const float kw   = (w - 24.0f) * 0.5f;
        const float kh   = h - (envY - y) - 10.0f;
        addKnob({x + 8.0f,        envY, kw, kh}, {kParamAttack, "Attack", 0.001f,1.0f,false}, 210,155,80);
        addKnob({x + 8.0f+kw+8.0f,envY, kw, kh}, {kParamRelease,"Release",0.01f, 3.0f,false}, 210,155,80);
    }

    void drawGroupFX(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Effects", 100, 175, 200);
        const float ky = y + 32.0f;
        const float kw = (w - 50.0f) / 4.0f;
        const float kh = (h - 44.0f) * 0.5f - 8.0f;
        const Knob top[] = {
            {kParamDelayRatio,"D.Ratio",0.5f,2.0f,false},
            {kParamDelay2Fb,  "D2 FB", 0.0f,1.0f,false},
            {kParamFilterFreq,"Flt Frq",20.0f,20000.0f,false},
            {kParamFilterQ,   "Flt Q", 0.1f,10.0f,false},
        };
        const Knob bot[] = {
            {kParamDelay1Fb,  "D1 FB",  0.0f, 1.0f, false},
            {kParamFilterFb,  "Flt FB", 0.0f, 1.0f, false},
            {kParamNoiseLevel,"Noise",  0.0f, 1.0f, false},
            {kParamDcLevel,   "DC",     0.0f, 1.0f, false},
        };
        for (int i = 0; i < 4; ++i) {
            addKnob({x + 8.0f + i*(kw+6.0f), ky,          kw, kh}, top[i], 100,175,200);
            addKnob({x + 8.0f + i*(kw+6.0f), ky+kh+12.0f, kw, kh}, bot[i], 100,175,200);
        }
    }

    void drawGroupFilter(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Filter", 150, 205, 120);
        const float ky = y + 32.0f;
        const float kw = w - 16.0f;
        const float kh = (h - 44.0f - 16.0f) / 3.0f;
        const Knob ks[] = {
            {kParamFilterShape,"Shape",  0.0f, 1.0f,    false},
            {kParamFilterFreq, "Freq",   20.0f,20000.0f,false},
            {kParamFilterQ,    "Q",      0.1f, 10.0f,   false},
        };
        for (int i = 0; i < 3; ++i)
            addKnob({x + 8.0f, ky + i*(kh+8.0f), kw, kh-4.0f}, ks[i], 150,205,120);
    }

    void drawGroupLFO(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "LFO", 200, 130, 185);
        const float ky = y + 32.0f;
        const float kw = w - 16.0f;
        const float kh = (h - 44.0f - 8.0f) * 0.5f;
        addKnob({x + 8.0f, ky,          kw, kh}, {kParamLfoFreq,  "Rate", 0.1f, 20.0f,false}, 200,130,185);
        addKnob({x + 8.0f, ky+kh+8.0f,  kw, kh}, {kParamAmFmDepth,"AM/FM",-1.0f,1.0f, false}, 200,130,185);
    }

    void drawGroupTrajectory(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "Trajectory", 185, 180, 80);

        fontSize(9.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(150, 140, 80, 200);
        text(x + 8.0f, y + 20.0f,
             "Prog 2 only — shares CC with Env/F1/F3/Intensity", nullptr);

        const float ky = y + 36.0f;
        const float kh = (h - 48.0f) * 0.5f - 6.0f;
        const float kw = (w - 8.0f * 5.0f) / 4.0f;

        const Knob top[] = {
            {kParamTrajSides,      "Sides",  3.0f, 24.0f,true},
            {kParamTrajStartPos,   "Start°", 0.0f,360.0f,false},
            {kParamTrajStartAngle, "Angle°", 0.0f,360.0f,false},
            {kParamTrajJitter,     "Jitter", 0.0f,10.0f, false},
        };
        const Knob bot[] = {
            {kParamTrajClip, "Clip",  0.0f,1.0f,false},
            {kParamTrajMixX, "Mix X", 0.0f,1.0f,false},
            {kParamTrajMixY, "Mix Y", 0.0f,1.0f,false},
        };
        for (int i = 0; i < 4; ++i)
            addKnob({x + 8.0f + i*(kw+8.0f), ky,           kw, kh}, top[i], 185,180,80);
        for (int i = 0; i < 3; ++i)
            addKnob({x + 8.0f + i*(kw+8.0f), ky+kh+10.0f,  kw, kh}, bot[i], 185,180,80);
    }

    // ── Widget factories ──────────────────────────────────────────────────────

    void drawPanel(float x, float y, float w, float h,
                   const char* title, int r, int g, int b)
    {
        beginPath(); roundedRect(x, y, w, h, 14.0f);
        fillColor(16, 21, 30, 238); fill();
        strokeColor(r, g, b, 38); strokeWidth(1.0f); stroke(); closePath();
        beginPath(); roundedRect(x, y, w, 3.0f, 2.0f);
        fillColor(r, g, b, 195); fill(); closePath();
        fontSize(11.0f); textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(r, g, b, 215);
        text(x + 8.0f, y + 7.0f, title, nullptr);
    }

    void addKnob(Rect rect, Knob k, int r, int g, int b)
    {
        knobRects_.push_back(rect);
        knobs_.push_back(k);

        const float val  = values_[k.param];
        const float norm = clampf((val - k.min) / (k.max - k.min + 1e-6f), 0.0f, 1.0f);

        beginPath(); roundedRect(rect.x, rect.y, rect.w, rect.h, 9.0f);
        fillColor(22, 28, 38, 255); fill(); closePath();

        // Track
        const float tx = rect.x + rect.w * 0.35f;
        const float tw = rect.w * 0.30f;
        const float ty = rect.y + 18.0f;
        const float th = rect.h - 34.0f;
        beginPath(); roundedRect(tx, ty, tw, th, 3.0f);
        fillColor(r, g, b, 40); fill(); closePath();

        // Fill
        const float fh = th * norm;
        beginPath(); roundedRect(tx, ty + th - fh, tw, fh, 3.0f);
        fillColor(r, g, b, 200); fill(); closePath();

        fontSize(9.5f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(200, 210, 220, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 4.0f, k.label, nullptr);

        fontSize(9.0f); textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        fillColor(155, 168, 182, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h - 2.0f,
             fmtValue(k.param, val).c_str(), nullptr);
    }

    void addToggle(Rect rect, uint32_t param, const char* label,
                   bool active, int r, int g, int b)
    {
        toggleRects_.push_back(rect);
        toggleParams_.push_back(param);

        beginPath(); roundedRect(rect.x, rect.y, rect.w, rect.h, 9.0f);
        fillColor(r, g, b, active ? 55 : 18); fill();
        strokeColor(r, g, b, active ? 195 : 95);
        strokeWidth(active ? 1.5f : 1.0f); stroke(); closePath();

        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(active ? 248 : 185, active ? 248 : 192, active ? 248 : 200, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    void addSelector(Rect rect, uint32_t param, int count,
                     const char* currentLabel, int r, int g, int b)
    {
        selectorRects_.push_back(rect);
        selectorParams_.push_back(param);
        selectorCounts_.push_back(count);

        beginPath(); roundedRect(rect.x, rect.y, rect.w, rect.h, 9.0f);
        fillColor(r, g, b, 26); fill();
        strokeColor(r, g, b, 135); strokeWidth(1.0f); stroke(); closePath();

        fontSize(10.5f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(228, 232, 240, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f,
             currentLabel, nullptr);

        fontSize(9.0f); fillColor(r, g, b, 170);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(rect.x + 6.0f, rect.y + rect.h * 0.5f + 1.0f, "<", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(rect.x + rect.w - 6.0f, rect.y + rect.h * 0.5f + 1.0f, ">", nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlueSynthDriverUI)
};

UI* createUI()
{
    return new FlueSynthDriverUI();
}

END_NAMESPACE_DISTRHO
