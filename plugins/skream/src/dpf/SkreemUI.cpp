#include "DistrhoUI.hpp"

#include "skream_core.hpp"
#include "skream_presets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Must match SkreemPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamInputGain = 0,
    kParamCutoff,
    kParamScream,
    kParamResonance,
    kParamMix,
    kParamOutputGain,
    kParamTrack,
    kParamCCCutoff,
    kParamCCScream,
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
    const char* unit;
    float       min, max;
    bool        integer;
    const char* stateKey;
};

constexpr std::array<SliderDef, 7> kMainSliders = {{
    { kParamInputGain,  "Input Gain",  "dB",  -24.0f,  24.0f, false, "input_gain"  },
    { kParamCutoff,     "Cutoff",      "%",     0.0f, 100.0f, false, "cutoff"      },
    { kParamScream,     "Scream",      "%",     0.0f, 100.0f, false, "scream"      },
    { kParamResonance,  "Resonance",   "%",     0.0f, 100.0f, false, "resonance"   },
    { kParamMix,        "Mix",         "%",     0.0f, 100.0f, false, "mix"         },
    { kParamOutputGain, "Output Gain", "dB",  -24.0f,   0.0f, false, "output_gain" },
    { kParamTrack,      "Track",       "%",     0.0f, 100.0f, false, "track"       },
}};

constexpr std::array<SliderDef, 3> kCCSliders = {{
    { kParamCCCutoff,  "CC Cutoff",  "", 0.0f, 127.0f, true, "cc_cutoff"  },
    { kParamCCScream,  "CC Scream",  "", 0.0f, 127.0f, true, "cc_scream"  },
    { kParamCCChannel, "CC Channel", "", 1.0f,  16.0f, true, "cc_channel" },
}};

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] std::string formatSliderValue(const SliderDef& def, float v)
{
    char buf[32];
    if ((def.index == kParamCCCutoff || def.index == kParamCCScream)
        && static_cast<int>(std::lround(v)) == 0)
        return "off";
    if (def.index == kParamInputGain || def.index == kParamOutputGain)
        std::snprintf(buf, sizeof(buf), "%+.1fdB", v);
    else if (def.integer)
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(v)));
    else
        std::snprintf(buf, sizeof(buf), "%.1f%%", v);
    return buf;
}

}  // namespace

class SkreemUI : public UI
{
public:
    SkreemUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        // Default values matching plugin defaults
        values_[kParamInputGain]  = 0.0f;
        values_[kParamCutoff]     = 85.0f;
        values_[kParamScream]     = 46.5f;
        values_[kParamResonance]  = 100.0f;
        values_[kParamMix]        = 100.0f;
        values_[kParamOutputGain] = -6.0f;
        values_[kParamTrack]      = 0.0f;
        values_[kParamCCCutoff]   = 1.0f;
        values_[kParamCCScream]   = 2.0f;
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
        // Values arrive via stateChanged; ignore host automation replay.
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (!value) return;
        const float f = static_cast<float>(std::atof(value));
        if      (std::strcmp(key, "input_gain")  == 0) { values_[kParamInputGain]  = f; }
        else if (std::strcmp(key, "cutoff")      == 0) { values_[kParamCutoff]     = f; }
        else if (std::strcmp(key, "scream")      == 0) { values_[kParamScream]     = f; }
        else if (std::strcmp(key, "resonance")   == 0) { values_[kParamResonance]  = f; }
        else if (std::strcmp(key, "mix")         == 0) { values_[kParamMix]        = f; }
        else if (std::strcmp(key, "output_gain") == 0) { values_[kParamOutputGain] = f; }
        else if (std::strcmp(key, "track")       == 0) { values_[kParamTrack]      = f; }
        else if (std::strcmp(key, "cc_cutoff")   == 0) { values_[kParamCCCutoff]   = f; }
        else if (std::strcmp(key, "cc_scream")   == 0) { values_[kParamCCScream]   = f; }
        else if (std::strcmp(key, "cc_channel")  == 0) { values_[kParamCCChannel]  = f; }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        drawBackground(W, H);
        drawHeader(W);
        drawMainSliders(W);
        drawCCSection(W);
        if (dropdownOpen_) drawDropdown(W);  // overlay on top
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        const float W  = static_cast<float>(getWidth());

        if (!ev.press) {
            dragSection_ = -1;
            dragSlot_    = -1;
            return false;
        }

        // Dropdown toggle (button in header)
        const Rect dropBtn = presetButtonRect(W);
        if (dropBtn.contains(mx, my)) {
            dropdownOpen_ = !dropdownOpen_;
            repaint();
            return true;
        }

        // Dropdown item selection
        if (dropdownOpen_) {
            for (int i = 0; i < downspout::skream::kPresetCount; ++i) {
                if (dropdownItemRect(W, i).contains(mx, my)) {
                    loadPreset(i);
                    dropdownOpen_ = false;
                    repaint();
                    return true;
                }
            }
            // Click outside closes dropdown
            dropdownOpen_ = false;
            repaint();
        }

        // Main sliders (section 0)
        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            if (mainSliderTrack(s, W).contains(mx, my)) {
                dragSection_ = 0;
                dragSlot_    = s;
                updateSlider(0, s, mx, W);
                return true;
            }
        }

        // CC sliders (section 1)
        for (int s = 0; s < static_cast<int>(kCCSliders.size()); ++s) {
            if (ccSliderTrack(s, W).contains(mx, my)) {
                dragSection_ = 1;
                dragSlot_    = s;
                updateSlider(1, s, mx, W);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragSection_ < 0) return false;
        updateSlider(dragSection_, dragSlot_,
                     static_cast<float>(ev.pos.getX()),
                     static_cast<float>(getWidth()));
        return true;
    }

private:
    std::array<float, kParameterCount> values_{};
    int  currentPreset_ = 0;
    bool dropdownOpen_  = false;
    int  dragSection_   = -1;
    int  dragSlot_      = -1;

    static constexpr float kPad      = 20.0f;
    static constexpr float kSliderX  = 200.0f;
    static constexpr float kMainTop  = 62.0f;
    static constexpr float kMainStep = 48.0f;
    static constexpr float kCCTop    = 412.0f;
    static constexpr float kCCStep   = 42.0f;
    static constexpr float kTrackH   = 14.0f;

    // ---- Geometry helpers --------------------------------------------------

    [[nodiscard]] Rect presetButtonRect(float W) const noexcept
    {
        return { W - kPad - 210.0f, 10.0f, 210.0f, 30.0f };
    }

    [[nodiscard]] Rect dropdownItemRect(float W, int i) const noexcept
    {
        const Rect btn = presetButtonRect(W);
        return { btn.x, btn.y + btn.h + i * 24.0f, btn.w, 24.0f };
    }

    [[nodiscard]] Rect mainSliderTrack(int s, float W) const noexcept
    {
        const float top = kMainTop + s * kMainStep;
        return { kSliderX, top + 20.0f, W - kSliderX - kPad, kTrackH };
    }

    [[nodiscard]] Rect ccSliderTrack(int s, float W) const noexcept
    {
        const float top = kCCTop + s * kCCStep;
        return { kSliderX, top + 20.0f, W - kSliderX - kPad, kTrackH };
    }

    // ---- Slider update -----------------------------------------------------

    void updateSlider(int section, int slot, float mx, float W)
    {
        const SliderDef& def = (section == 0) ? kMainSliders[static_cast<std::size_t>(slot)]
                                               : kCCSliders[static_cast<std::size_t>(slot)];
        const Rect tr = (section == 0) ? mainSliderTrack(slot, W) : ccSliderTrack(slot, W);
        const float t = clampf((mx - tr.x) / tr.w, 0.0f, 1.0f);
        float v = def.min + t * (def.max - def.min);
        if (def.integer) v = std::round(v);
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        setState(def.stateKey, buf);
        values_[def.index] = v;
        repaint();
    }

    // ---- Preset loading ----------------------------------------------------

    void loadPreset(int i)
    {
        using namespace downspout::skream;
        currentPreset_ = i;
        const Parameters& p = kPresets[i].params;

        auto send = [&](const char* key, float v) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.6g", v);
            setState(key, buf);
        };
        send("input_gain",  p.inputGain);
        send("cutoff",      p.cutoff);
        send("scream",      p.scream);
        send("resonance",   p.resonance);
        send("mix",         p.mix);
        send("output_gain", p.outputGain);
        send("track",       p.track);

        values_[kParamInputGain]  = p.inputGain;
        values_[kParamCutoff]     = p.cutoff;
        values_[kParamScream]     = p.scream;
        values_[kParamResonance]  = p.resonance;
        values_[kParamMix]        = p.mix;
        values_[kParamOutputGain] = p.outputGain;
        values_[kParamTrack]      = p.track;
    }

    // ---- Drawing -----------------------------------------------------------

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
        fillColor(220, 80, 40, 255);
        text(kPad, 25.0f, "SKREAM", nullptr);

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(130, 130, 120, 255);
        text(kPad + 110.0f, 25.0f, "scream filter  |  MIDI CC via Drift", nullptr);

        beginPath();
        strokeColor(60, 60, 70, 255);
        strokeWidth(1.0f);
        moveTo(0, 50.0f); lineTo(W, 50.0f);
        stroke();
        closePath();

        // Section labels
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(100, 100, 95, 255);
        text(kPad, 54.0f, "PARAMETER", nullptr);
        text(kSliderX, 54.0f, "VALUE", nullptr);

        drawPresetButton(W);
    }

    void drawPresetButton(float W)
    {
        const Rect r = presetButtonRect(W);

        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 5.0f);
        fillColor(38, 38, 52, 255);
        fill();
        closePath();

        beginPath();
        strokeColor(80, 80, 100, 255);
        strokeWidth(1.0f);
        roundedRect(r.x, r.y, r.w, r.h, 5.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(200, 200, 190, 255);
        text(r.x + 8.0f, r.y + r.h * 0.5f,
             downspout::skream::kPresets[currentPreset_].name, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(160, 160, 150, 255);
        text(r.x + r.w - 8.0f, r.y + r.h * 0.5f, dropdownOpen_ ? "\xe2\x96\xb2" : "\xe2\x96\xbc", nullptr);
    }

    void drawDropdown(float W)
    {
        using namespace downspout::skream;
        const Rect btn = presetButtonRect(W);
        const float listTop = btn.y + btn.h;
        const float listH   = kPresetCount * 24.0f;

        beginPath();
        rect(btn.x, listTop, btn.w, listH);
        fillColor(28, 28, 40, 245);
        fill();
        closePath();

        beginPath();
        strokeColor(80, 80, 100, 255);
        strokeWidth(1.0f);
        rect(btn.x, listTop, btn.w, listH);
        stroke();
        closePath();

        for (int i = 0; i < kPresetCount; ++i) {
            const float iy = listTop + i * 24.0f;
            const bool active = (i == currentPreset_);

            if (active) {
                beginPath();
                rect(btn.x, iy, btn.w, 24.0f);
                fillColor(200, 80, 35, 200);
                fill();
                closePath();
            }

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(active ? 255 : 200, active ? 255 : 200, active ? 250 : 190, 255);
            text(btn.x + 8.0f, iy + 12.0f, kPresets[i].name, nullptr);
        }
    }

    void drawMainSliders(float W)
    {
        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect        tr = mainSliderTrack(s, W);
            const float    labelY = tr.y - 18.0f;

            if (s > 0) {
                beginPath();
                strokeColor(35, 35, 42, 255);
                strokeWidth(1.0f);
                moveTo(kPad, tr.y - 24.0f); lineTo(W - kPad, tr.y - 24.0f);
                stroke();
                closePath();
            }

            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(190, 190, 180, 255);
            text(kPad, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(220, 160, 60, 255);
            text(W - kPad, labelY, formatSliderValue(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fillColor(40, 40, 50, 255);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(220, 80, 35, 255);
                fill();
                closePath();
            }
        }
    }

    void drawCCSection(float W)
    {
        // Divider
        const float divY = kCCTop - 16.0f;
        beginPath();
        strokeColor(50, 50, 62, 255);
        strokeWidth(1.0f);
        moveTo(kPad, divY); lineTo(W - kPad, divY);
        stroke();
        closePath();

        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(100, 100, 95, 255);
        text(kPad, divY + 3.0f, "MIDI CC  (0 = off, controlled by Drift)", nullptr);

        for (int s = 0; s < static_cast<int>(kCCSliders.size()); ++s) {
            const SliderDef& def = kCCSliders[static_cast<std::size_t>(s)];
            const Rect        tr = ccSliderTrack(s, W);
            const float    labelY = tr.y - 16.0f;

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(160, 160, 150, 255);
            text(kPad, labelY, def.label, nullptr);

            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fillColor(180, 140, 60, 255);
            text(W - kPad, labelY, formatSliderValue(def, values_[def.index]).c_str(), nullptr);

            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fillColor(32, 32, 42, 255);
            fill();
            closePath();

            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(160, 80, 30, 255);
                fill();
                closePath();
            }
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkreemUI)
};

UI* createUI()
{
    return new SkreemUI();
}

END_NAMESPACE_DISTRHO
