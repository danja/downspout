#include "generative_panel_ui.hpp"
#include "downspout/look_and_feel.hpp"
#include "resonance_garden_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

class ResonanceGardenUI : public GenerativePanelUI {
public:
    ResonanceGardenUI()
        : GenerativePanelUI(
            "Resonance Garden",
            "Tune a twelve-voice resonator bank from MIDI or an internal musical fallback",
            downspout::resonance_garden::kParameterSpecs.data(),
            downspout::resonance_garden::kParameterCount, 102, 190, 126) {}

private:
    const laf::Theme& t_ { laf::defaultTheme() };
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    void onNanoDisplay() override
    {
        using namespace downspout::resonance_garden;
        beginPanel();

        drawSection(24, 104, 430, 226, "RESONANCE", "decay, colour, and stability");
        drawSlider(kDecay, 38, 137, 194, "Decay", " sec", "Resonator decay time.", 2);
        drawPercentSlider(kDamping, 240, 137, 200, "Damping", "Reduce high-frequency resonance.");
        drawPercentSlider(kInharmonicity, 38, 193, 194, "Inharmonicity", "Detune resonators away from exact harmonics.");
        drawPercentSlider(kFeedback, 240, 193, 200, "Resonance feedback", "Bounded feedback amount.");
        drawToggle(kFreeze, 38, 249, 402, "Freeze resonator energy", "Hold the current resonator energy.");

        drawSection(24, 342, 430, 289, "TUNING & OUTPUT", "MIDI notes override the fallback scale");
        drawPitchClassSlider(kRoot, 38, 375, 194, "Internal root", "Fallback scale root.");
        drawScaleDropdown(240, 375, 200);
        drawSlider(kVoiceLimit, 38, 434, 194, "Voice limit", "", "Maximum simultaneous resonators.", 0);
        drawPercentSlider(kExcitation, 240, 434, 200, "Excitation", "How strongly input excites the resonators.");
        drawPercentSlider(kMix, 38, 490, 402, "Wet / dry", "Blend resonated and original audio.");
        drawMeter(kStatusPeak, 38, 546, 194, "Output peak");
        drawMeter(kStatusVoices, 240, 546, 200, "Active voices", "", 0);

        drawSection(468, 104, 468, 527, "RESONATOR BANK", "live voice energy and pitch map");
        drawGarden(487, 139, 430, 455);

        if (menuOpen_)
            drawScaleMenu();
        endPanel();
    }

    // The scale list has 23 names, too many for a segmented choice, so it is a dropdown (as in Qune).
    static constexpr float kItemW = 160.0f, kItemH = 24.0f;
    static constexpr int kMenuColumns = 3;

    bool menuOpen_ = false;
    Box scaleBox_ {};

    static int menuRows() { return (downspout::resonance_garden::kScaleCount + kMenuColumns - 1) / kMenuColumns; }

    Box menuRect() const
    {
        const float w = kItemW * kMenuColumns, h = kItemH * static_cast<float>(menuRows());
        const float margin = 14.0f;
        const float x = std::clamp(scaleBox_.x, margin, std::max(margin, static_cast<float>(getWidth()) - margin - w));
        float y = scaleBox_.y + scaleBox_.h + 6.0f;
        if (y + h > static_cast<float>(getHeight()) - margin)
            y = scaleBox_.y - h - 6.0f;
        return {x, std::max(margin, y), w, h};
    }

    void drawScaleDropdown(const float x, const float y, const float w)
    {
        using namespace downspout::resonance_garden;
        beginPath();
        fillColor(38, 45, 52, 255);
        roundedRect(x, y, w, 51.0f, 5.0f);
        fill();
        fillColor(202, 211, 207, 255);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x + 10.0f, y + 7.0f, "Internal scale", nullptr);

        scaleBox_ = {x + 9.0f, y + 24.0f, w - 18.0f, 20.0f};
        beginPath();
        fillColor(20, 25, 30, 255);
        roundedRect(scaleBox_.x, scaleBox_.y, scaleBox_.w, scaleBox_.h, 3.0f);
        fill();
        fillColor(220, 226, 222, 255);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(scaleBox_.x + 8.0f, scaleBox_.y + 10.0f,
             scaleName(static_cast<int>(std::lround(value(kScale)))), nullptr);
        fc(t_.textDim);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(scaleBox_.x + scaleBox_.w - 8.0f, scaleBox_.y + 10.0f, "v", nullptr);
    }

    void drawScaleMenu()
    {
        using namespace downspout::resonance_garden;
        const Box menu = menuRect();
        const int rows = menuRows();
        const int selected = std::clamp(static_cast<int>(std::lround(value(kScale))), 0, kScaleCount - 1);

        beginPath();
        fillColor(24, 29, 35, 250);
        roundedRect(menu.x, menu.y, menu.w, menu.h, 6.0f);
        fill();
        strokeColor(79, 88, 95, 255);
        strokeWidth(1.0f);
        stroke();

        fontSize(11.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        for (int i = 0; i < kScaleCount; ++i) {
            const float ix = menu.x + static_cast<float>(i / rows) * kItemW;
            const float iy = menu.y + static_cast<float>(i % rows) * kItemH;
            if (i == selected) {
                beginPath();
                fillColor(accentR(), accentG(), accentB(), 215);
                roundedRect(ix + 4.0f, iy + 2.0f, kItemW - 8.0f, kItemH - 4.0f, 3.0f);
                fill();
            }
            fillColor(i == selected ? 12 : 214, i == selected ? 17 : 221, i == selected ? 20 : 217, 255);
            text(ix + 14.0f, iy + kItemH * 0.5f, scaleName(i), nullptr);
        }
    }

    bool onMouse(const MouseEvent& event) override
    {
        using namespace downspout::resonance_garden;
        if (event.press && event.button == 1) {
            const float mx = static_cast<float>(event.pos.getX());
            const float my = static_cast<float>(event.pos.getY());
            if (menuOpen_) {
                const Box menu = menuRect();
                if (menu.contains(mx, my)) {
                    const int rows = menuRows();
                    const int item = static_cast<int>((mx - menu.x) / kItemW) * rows
                                   + static_cast<int>((my - menu.y) / kItemH);
                    if (item >= 0 && item < kScaleCount)
                        commitParameter(kScale, static_cast<float>(item));
                }
                menuOpen_ = false;
                repaint();
                return true;
            }
            if (scaleBox_.contains(mx, my)) {
                menuOpen_ = true;
                repaint();
                return true;
            }
        }
        return handleMouse(event);
    }

    void drawGarden(const float x, const float y, const float w, const float h)
    {
        using namespace downspout::resonance_garden;
        const int active = std::clamp(static_cast<int>(std::lround(value(kStatusVoices))), 0, kMaxVoices);
        const int limit = std::clamp(static_cast<int>(std::lround(value(kVoiceLimit))), 1, kMaxVoices);
        const float peak = std::clamp(value(kStatusPeak) / 1.5f, 0.0f, 1.0f);
        const int root = static_cast<int>(std::lround(value(kRoot)));
        const int scale = static_cast<int>(std::lround(value(kScale)));
        constexpr int columns = 4;
        const float cellW = w / columns;
        const float cellH = (h - 30.0f) / (kMaxVoices / columns);
        for (int i = 0; i < kMaxVoices; ++i) {
            const float cx = x + (static_cast<float>(i % columns) + 0.5f) * cellW;
            const float cy = y + (static_cast<float>(i / columns) + 0.5f) * cellH;
            const bool usable = i < limit;
            const bool on = usable && i < active;
            const float radius = 22.0f + (on ? 14.0f * peak : 0.0f);
            beginPath();
            if (on)
                fillColor(accentR(), accentG(), accentB(), 190);
            else
                fillColor(usable ? 43 : 30, usable ? 50 : 35, usable ? 57 : 41, 255);
            circle(cx, cy, radius);
            fill();
            beginPath();
            if (on)
                strokeColor(218, 229, 220, 190);
            else
                strokeColor(usable ? 79 : 52, usable ? 88 : 58, usable ? 95 : 64, 190);
            strokeWidth(1.5f);
            circle(cx, cy, radius + 6);
            stroke();
            if (on)
                fillColor(18, 22, 24, 255);
            else
                fillColor(usable ? 130 : 84, usable ? 140 : 91, usable ? 147 : 97, 255);
            fontSize(11);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            char name[16] {};
            formatMidiNote(fallbackNote(root, scale, i), name, sizeof(name));
            text(cx, cy, name, nullptr);
        }
        fc(t_.textDim);
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
        text(x + w * 0.5f, y + h - 8, active > 0 ? "MIDI / fallback voices active" : "Waiting for audio excitation", nullptr);
    }
};

UI* createUI() { return new ResonanceGardenUI(); }

END_NAMESPACE_DISTRHO
