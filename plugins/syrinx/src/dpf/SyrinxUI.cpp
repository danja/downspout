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

constexpr std::size_t kVoiceControlCount  = 9;
constexpr std::size_t kMasterControlCount = 2;

// Layout constants
constexpr float kPad        = 22.0f;
constexpr float kHeaderH    = 62.0f;
constexpr float kSliderGapY = 16.0f;  // gap between header and slider area
constexpr float kLabelAreaH = 60.0f;  // below track: label + value
constexpr float kSliderW    = 90.0f;
constexpr float kSliderGap  = 8.0f;
constexpr float kMasterGap  = 30.0f;  // extra gap before master sliders
constexpr float kTrackW        = 14.0f;
constexpr float kThumbW        = 46.0f;
constexpr float kThumbH        = 14.0f;
constexpr float kDropdownItemH = 30.0f;

struct Rect {
    float x=0, y=0, w=0, h=0;
    [[nodiscard]] bool contains(float px, float py) const noexcept
    { return px>=x && px<=x+w && py>=y && py<=y+h; }
};

struct Color { int r, g, b; };
struct ControlDef { std::uint32_t parameter; const char* label; };

constexpr std::array<Color, kPresetCount> kPresetColors = {{
    {165, 200, 110}, // Wren
    {120, 170, 210}, // Thrush
    {215, 185,  90}, // Warbler
    {205, 115,  75}, // Finch
    {150, 200, 135}, // Robin
    { 90,  90, 120}, // Nightjar
    {175, 150, 195}, // Pigeon
    {100, 200, 175}, // Hummingbird
    {205, 165,  90}, // Starling
    {155, 155, 155}, // Custom
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
        std::snprintf(buf, sizeof(buf), "%.1fHz", value * 20.0f);
    } else if (pp == kPresetParamVibDepth) {
        std::snprintf(buf, sizeof(buf), "%.0fct", value * 300.0f);
    } else if (pp == kPresetParamAMRate) {
        std::snprintf(buf, sizeof(buf), "%.1fHz", value * 30.0f);
    } else if (pp == kPresetParamBend) {
        std::snprintf(buf, sizeof(buf), "%+.2f", value);
    } else if (spec.maximum <= 1.0f && spec.minimum >= 0.0f) {
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    } else {
        std::snprintf(buf, sizeof(buf), "%.2f", value);
    }
    return buf;
}

constexpr std::array<ControlDef, kVoiceControlCount> kVoiceControls = {{
    { kPresetParamLevel,     "Level"    },
    { kPresetParamNoise,     "Noise"    },
    { kPresetParamRoughness, "Rough"    },
    { kPresetParamTimbre,    "Timbre"   },
    { kPresetParamVibRate,   "Vib Rate" },
    { kPresetParamVibDepth,  "Vib Dep"  },
    { kPresetParamBend,      "Bend"     },
    { kPresetParamHarmonic,  "Harmonic" },
    { kPresetParamAMRate,    "AM Rate"  },
}};

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

        drawBackground(W, H);
        drawHeader(kPad, kPad, W - kPad * 2.0f, kHeaderH);

        const float sliderY = kPad + kHeaderH + kSliderGapY;
        drawSliders(kPad, sliderY, W - kPad * 2.0f, H - sliderY - kPad);

        // Dropdown drawn last so it overlays sliders
        if (dropdownOpen_)
            drawDropdownList();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            activeControlParam_ = -1;
            activeControlRect_  = {};
            return false;
        }

        if (dropdownOpen_) {
            for (std::size_t i = 0; i < kPresetCount; ++i) {
                if (dropdownItemRects_[i].contains(x, y)) {
                    selectedPreset_ = static_cast<int>(i);
                    commitParameter(kParamSelectedPreset, static_cast<float>(selectedPreset_));
                    dropdownOpen_ = false;
                    repaint();
                    return true;
                }
            }
            dropdownOpen_ = false;
            repaint();
            return true;
        }

        if (dropdownRect_.contains(x, y)) {
            dropdownOpen_ = true;
            repaint();
            return true;
        }

        if (muteRect_.contains(x, y)) {
            const std::uint32_t p = presetParam(static_cast<std::uint32_t>(selectedPreset_), kPresetParamMute);
            commitParameter(p, values_[p] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }

        if (randomizeRect_.contains(x, y)) {
            randomizeSelectedPreset(static_cast<std::uint32_t>(std::time(nullptr)));
            return true;
        }

        for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
            if (controlRects_[i].contains(x, y)) {
                activeControlParam_ = static_cast<int>(
                    presetParam(static_cast<std::uint32_t>(selectedPreset_),
                                kVoiceControls[i].parameter));
                activeControlRect_ = controlRects_[i];
                updateControlFromY(activeControlParam_, y, activeControlRect_);
                return true;
            }
        }

        for (std::size_t i = 0; i < kMasterControlCount; ++i) {
            if (masterRects_[i].contains(x, y)) {
                activeControlParam_ = static_cast<int>(kMasterControls[i].parameter);
                activeControlRect_  = masterRects_[i];
                updateControlFromY(activeControlParam_, y, activeControlRect_);
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeControlParam_ >= 0) {
            updateControlFromY(activeControlParam_, static_cast<float>(ev.pos.getY()), activeControlRect_);
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (dropdownOpen_) return false;
        const float x   = static_cast<float>(ev.pos.getX());
        const float y   = static_cast<float>(ev.pos.getY());
        const float dir = ev.delta.getY() > 0.0f ? 1.0f : -1.0f;

        for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
            if (controlRects_[i].contains(x, y)) {
                nudgeParameter(presetParam(static_cast<std::uint32_t>(selectedPreset_),
                                           kVoiceControls[i].parameter), dir * 0.02f);
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
    std::array<float, kParameterCount>    values_{};
    std::array<Rect, kVoiceControlCount>  controlRects_{};
    std::array<Rect, kMasterControlCount> masterRects_{};
    std::array<Rect, kPresetCount>        dropdownItemRects_{};
    Rect dropdownRect_{};
    Rect muteRect_{};
    Rect randomizeRect_{};
    Rect activeControlRect_{};

    int  selectedPreset_    = 0;
    int  activeControlParam_= -1;
    bool dropdownOpen_      = false;

    // ---- Drawing ----

    void drawBackground(float W, float H)
    {
        beginPath(); fillColor(13, 15, 17, 255); rect(0, 0, W, H); fill(); closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath(); roundedRect(x, y, w, h, 7.0f);
        fillColor(18, 22, 25, 245); fill();
        strokeColor(42, 52, 55, 255); strokeWidth(1.0f); stroke();
        closePath();

        // Plugin name
        fontSize(30.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(238, 241, 239, 255);
        text(x + 22.0f, y + h * 0.5f, "Syrinx", nullptr);

        // Subtitle
        fontSize(12.0f); fillColor(90, 120, 118, 200);
        text(x + 150.0f, y + h * 0.5f, "Mindlin-Laje avian vocal model · chromatic MIDI", nullptr);

        const Color& col  = kPresetColors[static_cast<std::size_t>(selectedPreset_)];
        const bool   muted = values_[presetParam(static_cast<std::uint32_t>(selectedPreset_),
                                                  kPresetParamMute)] >= 0.5f;

        // Preset dropdown button
        dropdownRect_ = { x + w - 484.0f, y + 10.0f, 210.0f, h - 20.0f };
        beginPath(); roundedRect(dropdownRect_.x, dropdownRect_.y, dropdownRect_.w, dropdownRect_.h, 6.0f);
        fillColor(col.r / 4, col.g / 4, col.b / 4, 255);
        strokeColor(col.r, col.g, col.b, dropdownOpen_ ? 255 : 160);
        strokeWidth(dropdownOpen_ ? 2.0f : 1.0f);
        fill(); stroke(); closePath();

        fontSize(14.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(col.r, col.g, col.b, 255);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s  \xe2\x96\xbe", kPresetNames[selectedPreset_]);
        text(dropdownRect_.x + 14.0f, dropdownRect_.y + dropdownRect_.h * 0.5f, buf, nullptr);

        // Mute button
        muteRect_ = { x + w - 266.0f, y + 10.0f, 70.0f, h - 20.0f };
        beginPath(); roundedRect(muteRect_.x, muteRect_.y, muteRect_.w, muteRect_.h, 6.0f);
        fillColor(muted ? col.r : 35, muted ? col.g : 40, muted ? col.b : 44, 255);
        fill(); closePath();
        fontSize(13.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(muted ? 12 : 175, muted ? 14 : 190, muted ? 15 : 190, 255);
        text(muteRect_.x + muteRect_.w * 0.5f, muteRect_.y + muteRect_.h * 0.5f, "MUTE", nullptr);

        // Randomize button
        randomizeRect_ = { x + w - 188.0f, y + 10.0f, 166.0f, h - 20.0f };
        beginPath(); roundedRect(randomizeRect_.x, randomizeRect_.y, randomizeRect_.w, randomizeRect_.h, 6.0f);
        fillColor(38, 86, 66, 255); fill(); closePath();
        fontSize(13.0f); textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(175, 225, 195, 255);
        text(randomizeRect_.x + randomizeRect_.w * 0.5f, randomizeRect_.y + randomizeRect_.h * 0.5f,
             "RANDOMIZE", nullptr);
    }

    void drawDropdownList()
    {
        const Color& selCol = kPresetColors[static_cast<std::size_t>(selectedPreset_)];
        const float listW = dropdownRect_.w;
        const float listX = dropdownRect_.x;
        const float listY = dropdownRect_.y + dropdownRect_.h + 6.0f;
        const float listH = static_cast<float>(kPresetCount) * kDropdownItemH + 6.0f;

        beginPath(); roundedRect(listX, listY, listW, listH, 7.0f);
        fillColor(18, 22, 25, 252); fill();
        strokeColor(selCol.r, selCol.g, selCol.b, 180); strokeWidth(1.5f); stroke(); closePath();

        for (std::size_t i = 0; i < kPresetCount; ++i) {
            const Rect item{ listX + 3.0f, listY + 3.0f + static_cast<float>(i) * kDropdownItemH,
                             listW - 6.0f, kDropdownItemH };
            dropdownItemRects_[i] = item;
            const bool active = static_cast<int>(i) == selectedPreset_;
            const Color& pc  = kPresetColors[i];

            if (active) {
                beginPath(); roundedRect(item.x, item.y, item.w, item.h, 4.0f);
                fillColor(pc.r / 3, pc.g / 3, pc.b / 3, 255); fill(); closePath();
            }

            // Color swatch
            beginPath(); roundedRect(item.x + 8.0f, item.y + item.h * 0.5f - 6.0f, 12.0f, 12.0f, 3.0f);
            fillColor(pc.r, pc.g, pc.b, active ? 255 : 190); fill(); closePath();

            fontSize(13.0f); textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(active ? pc.r : 205, active ? pc.g : 212, active ? pc.b : 210, 255);
            text(item.x + 28.0f, item.y + item.h * 0.5f, kPresetNames[i], nullptr);
        }
    }

    void drawSliders(float x, float y, float w, float h)
    {
        // Total width of all sliders+gaps: 9*(sliderW+gap) + masterGap + 2*(sliderW+gap) - gap
        // = 11*kSliderW + 10*kSliderGap + kMasterGap - kSliderGap
        // = 11*90 + 9*8 + 30 = 990 + 72 + 30 = 1092
        const float totalW = static_cast<float>(kVoiceControlCount + kMasterControlCount) * kSliderW
                           + static_cast<float>(kVoiceControlCount + kMasterControlCount - 1) * kSliderGap
                           + kMasterGap;
        const float startX = x + (w - totalW) * 0.5f;

        const Color& col   = kPresetColors[static_cast<std::size_t>(selectedPreset_)];
        const bool   muted = values_[presetParam(static_cast<std::uint32_t>(selectedPreset_),
                                                  kPresetParamMute)] >= 0.5f;
        const float  trackH = h - kLabelAreaH;

        // Voice sliders
        for (std::size_t i = 0; i < kVoiceControlCount; ++i) {
            const float cx = startX + static_cast<float>(i) * (kSliderW + kSliderGap);
            controlRects_[i] = { cx, y, kSliderW, h };
            const std::uint32_t paramIdx = presetParam(static_cast<std::uint32_t>(selectedPreset_),
                                                        kVoiceControls[i].parameter);
            drawVerticalSlider(controlRects_[i], trackH, paramIdx, kVoiceControls[i].label, col, muted);
        }

        // Divider
        const float divX = startX + static_cast<float>(kVoiceControlCount) * (kSliderW + kSliderGap)
                         - kSliderGap + kMasterGap * 0.5f;
        beginPath();
        moveTo(divX, y + 12.0f);
        lineTo(divX, y + trackH - 12.0f);
        strokeColor(52, 62, 66, 200); strokeWidth(1.0f); stroke(); closePath();

        // "Master" label above divider area
        const float masterStartX = startX + static_cast<float>(kVoiceControlCount) * (kSliderW + kSliderGap)
                                 - kSliderGap + kMasterGap;
        constexpr Color masterCol{ 88, 165, 158 };

        for (std::size_t i = 0; i < kMasterControlCount; ++i) {
            const float cx = masterStartX + static_cast<float>(i) * (kSliderW + kSliderGap);
            masterRects_[i] = { cx, y, kSliderW, h };
            drawVerticalSlider(masterRects_[i], trackH, kMasterControls[i].parameter,
                               kMasterControls[i].label, masterCol, false);
        }

        // Section labels below sliders
        const float labelBaseY = y + trackH + kLabelAreaH - 8.0f;
        fontSize(10.0f); textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fillColor(col.r, col.g, col.b, muted ? 80 : 150);
        text(startX, labelBaseY, kPresetNames[static_cast<std::size_t>(selectedPreset_)], nullptr);

        fillColor(masterCol.r, masterCol.g, masterCol.b, 130);
        text(masterStartX, labelBaseY, "Master", nullptr);
    }

    void drawVerticalSlider(const Rect& colRect, float trackH, std::uint32_t param,
                            const char* label, const Color& col, bool muted)
    {
        const float value = values_[param];
        const float norm  = normalizedValue(param, value);
        const std::string txt = formatValue(param, value);

        const float cx     = colRect.x + colRect.w * 0.5f;
        const float trackX = cx - kTrackW * 0.5f;
        const float trackY = colRect.y + 4.0f;
        const float tH     = trackH - 8.0f;

        const float thumbCY = trackY + tH * (1.0f - norm);  // y-center of thumb
        const float thumbY  = thumbCY - kThumbH * 0.5f;
        const float fillH   = tH - (thumbCY - trackY);

        // Track background
        beginPath(); roundedRect(trackX, trackY, kTrackW, tH, 6.0f);
        fillColor(28, 33, 36, 255); fill(); closePath();

        // Filled portion (bottom up to thumb)
        if (fillH > 3.0f) {
            beginPath();
            roundedRect(trackX + 2.0f, thumbCY, kTrackW - 4.0f, fillH, 4.0f);
            fillColor(col.r, col.g, col.b, muted ? 70 : 175); fill(); closePath();
        }

        // Thumb
        const float tx = cx - kThumbW * 0.5f;
        beginPath(); roundedRect(tx, thumbY, kThumbW, kThumbH, kThumbH * 0.5f);
        fillColor(225, 232, 228, muted ? 100 : 228); fill(); closePath();

        // Label
        const float labelY = colRect.y + trackH + 10.0f;
        fontSize(11.0f); textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(185, 195, 192, muted ? 120 : 220);
        text(cx, labelY, label, nullptr);

        // Value
        fontSize(10.0f);
        fillColor(col.r, col.g, col.b, muted ? 80 : 195);
        text(cx, labelY + 17.0f, txt.c_str(), nullptr);
    }

    // ---- Interaction helpers ----

    void updateControlFromY(int paramIndex, float y, const Rect& rect)
    {
        if (paramIndex < 0 || paramIndex >= static_cast<int>(kParameterCount)) return;
        const std::uint32_t p = static_cast<std::uint32_t>(paramIndex);
        const auto   spec   = getParameterSpec(p);
        const float  trackY = rect.y + 4.0f;
        const float  tH     = (rect.h - kLabelAreaH) - 8.0f;
        const float  norm   = clampf(1.0f - (y - trackY) / tH, 0.0f, 1.0f);
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
        const auto  spec    = getParameterSpec(p);
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
