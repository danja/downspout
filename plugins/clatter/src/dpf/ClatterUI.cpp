#include "downspout/dpf/MagnetoKit.hpp"

#include "clatter_params.hpp"
#include "clatter_patches.hpp"

#include <array>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

// Same numbering as ClatterPlugin.cpp: Sound, Note Map, Tune/Decay/Tone per family, Level, Width.
constexpr std::uint32_t kSound = 0;
constexpr std::uint32_t kNoteMap = 1;
constexpr std::uint32_t kFirstTrim = 2;
constexpr std::uint32_t kLevel = kFirstTrim + clatter::kFamilyCount * 3;
constexpr std::uint32_t kWidth = kLevel + 1;
constexpr std::uint32_t kCount = kWidth + 1;

constexpr kit::Accent kFamilyAccent[clatter::kFamilyCount] = {
    {198, 120, 64},   // membrane: copper
    {92, 140, 156},   // metal: cold steel
    {120, 132, 88},   // wood: olive
    {198, 132, 58},   // shaker: amber
    {150, 100, 130},  // clap and scrape: plum
};
constexpr kit::Accent kPlotAccent {198, 132, 58};
const char* const kFamilyTitle[clatter::kFamilyCount] = {"MEMBRANE", "METAL", "WOOD", "SHAKER", "CLAP + GUIRO"};
const char* const kNoteMapNames[2] = {"Selected sound", "General MIDI"};

// Short button names, in clatter::kPatches order.
const char* const kShortNames[clatter::kSoundCount] = {
    "Lo Floor", "Hi Floor", "Low Tom", "Lo-Mid Tom", "Hi-Mid Tom", "High Tom", "Hi Bongo", "Lo Bongo",
    "Mute Conga", "Open Conga", "Low Conga", "Hi Timbale", "Lo Timbale", "Mute Surdo", "Open Surdo",
    "Hi Agogo", "Lo Agogo", "Mute Triang", "Jingle Bell", "Bell Tree",
    "Claves", "Hi Block", "Lo Block", "Castanets",
    "Cabasa", "Maracas", "Shaker", "Vibraslap",
    "Hand Clap", "Short Guiro", "Long Guiro"};

struct Tables {
    std::array<kit::Range, kCount> ranges {};
    std::array<float, kCount> defaults {};
    Tables()
    {
        ranges[kSound] = {0, clatter::kSoundCount - 1, true};  defaults[kSound] = 0;
        ranges[kNoteMap] = {0, 1, true};                       defaults[kNoteMap] = 1;
        for (int f = 0; f < clatter::kFamilyCount; ++f) {
            const std::uint32_t i = kFirstTrim + static_cast<std::uint32_t>(f) * 3;
            ranges[i] = {-12, 12, false};              defaults[i] = 0;
            ranges[i + 1] = {0.25f, 4, false, true};   defaults[i + 1] = 1;
            ranges[i + 2] = {0, 1, false};             defaults[i + 2] = 0.5f;
        }
        ranges[kLevel] = {0, 1, false};  defaults[kLevel] = 0.8f;
        ranges[kWidth] = {0, 1, false};  defaults[kWidth] = 0.6f;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class ClatterUI : public MagnetoUI
{
public:
    ClatterUI() : MagnetoUI(tables().ranges.data(), kCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    // The sound grid has more buttons than the kit tracks, so it does its own hit-testing.
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1 && ev.press) {
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
            for (int i = 0; i < clatter::kSoundCount; ++i) {
                if (cells_[static_cast<std::size_t>(i)].contains(mx, my)) {
                    commit(kSound, static_cast<float>(i));
                    return true;
                }
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("CLATTER", "General MIDI percussion",
                   "Toms, hand drums, bells, wood, shakers and clap on their General MIDI notes; velocity sets level. "
                   "Trim each family below.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawSoundPanel({24.0f, 108.0f, full, 284.0f});
        drawTrimPanel({24.0f, 402.0f, full, 176.0f});
        drawOutputPanel({24.0f, 588.0f, full, 84.0f});
    }

private:
    void drawSoundPanel(const Rect b)
    {
        drawPanel(b, "SOUNDS", kFamilyAccent[0],
                  "General MIDI note under each name; click a sound to select it for the Selected note map");
        const auto& t = theme();
        constexpr float kLabelW = 96.0f, kCellW = 80.0f, kCellH = 34.0f, kGap = 4.0f;
        const int perRow = static_cast<int>((b.w - 28.0f - kLabelW) / (kCellW + kGap));
        float y = b.y + 42.0f;
        int index = 0;
        for (int family = 0; family < clatter::kFamilyCount; ++family) {
            const kit::Accent accent = kFamilyAccent[family];
            label(b.x + 14.0f, y + kCellH * 0.5f, kFamilyTitle[family], false, 12.0f, ALIGN_LEFT | ALIGN_MIDDLE);
            int col = 0;
            while (index < clatter::kSoundCount && static_cast<int>(clatter::kPatches[index].family) == family) {
                if (col == perRow) {
                    col = 0;
                    y += kCellH + kGap;
                }
                const Rect cell {b.x + 14.0f + kLabelW + static_cast<float>(col) * (kCellW + kGap), y, kCellW, kCellH};
                cells_[static_cast<std::size_t>(index)] = cell;
                const bool selected = intValue(kSound) == index;
                beginPath();
                if (selected) fillColor(accent.r, accent.g, accent.b, 255); else fc(t.buttonFace);
                roundedRect(cell.x, cell.y, cell.w, cell.h, downspout::laf::kRadiusSmall);
                fill();
                if (!selected) {
                    beginPath();
                    strokeColor(accent.r, accent.g, accent.b, 150);
                    strokeWidth(1.0f);
                    roundedRect(cell.x, cell.y, cell.w, cell.h, downspout::laf::kRadiusSmall);
                    stroke();
                }
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                if (selected) fillColor(250, 248, 242, 255); else fc(t.textPrimary);
                fontSize(11.0f);
                text(cell.x + cell.w * 0.5f, cell.y + 12.0f, kShortNames[index], nullptr);
                if (selected) fillColor(250, 248, 242, 200); else fc(t.textDim);
                fontSize(10.0f);
                char note[8];
                std::snprintf(note, sizeof(note), "%d", clatter::kPatches[index].note);
                text(cell.x + cell.w * 0.5f, cell.y + 26.0f, note, nullptr);
                ++col;
                ++index;
            }
            y += kCellH + kGap;
        }
    }

    void drawTrimPanel(const Rect b)
    {
        drawPanel(b, "FAMILY TRIMS", kPlotAccent,
                  "Tune in semitones, Decay as a multiplier, Tone is brightness (neutral at 50 %; for shakers and clap it moves the noise band)");
        const float gap = 20.0f;
        const float w = (b.w - 28.0f - gap * static_cast<float>(clatter::kFamilyCount - 1)) / static_cast<float>(clatter::kFamilyCount);
        const char* const kinds[3] = {"Tune", "Decay", "Tone"};
        for (int family = 0; family < clatter::kFamilyCount; ++family) {
            const float x = b.x + 14.0f + static_cast<float>(family) * (w + gap);
            label(x, b.y + 40.0f, kFamilyTitle[family], false, 12.0f);
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t parameter = kFirstTrim + static_cast<std::uint32_t>(family) * 3 + static_cast<std::uint32_t>(k);
                drawSlider(parameter, kinds[k], format(parameter).c_str(), {x, b.y + 58.0f + 40.0f * static_cast<float>(k), w, 34.0f},
                           kFamilyAccent[family]);
            }
        }
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT AND MIDI", kFamilyAccent[2], "General MIDI: only the listed notes play; Selected: any note plays the selected sound");
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        float x = b.x + 14.0f;
        drawSlider(kLevel, "Level", format(kLevel).c_str(), {x, b.y + 42.0f, w, 34.0f}, kFamilyAccent[2]);
        x += w + gap;
        drawSlider(kWidth, "Width (stereo spread)", format(kWidth).c_str(), {x, b.y + 42.0f, w, 34.0f}, kFamilyAccent[2]);
        x += w + gap;
        label(x, b.y + 40.0f, "Note Map");
        drawSegments(kNoteMap, kNoteMapNames, 2, {x, b.y + 56.0f, w, 24.0f}, kFamilyAccent[2]);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const double v = static_cast<double>(value(parameter));
        if (parameter >= kFirstTrim && parameter < kLevel) {
            switch ((parameter - kFirstTrim) % 3) {
            case 0: std::snprintf(buffer, sizeof(buffer), "%+.1f st", v); break;
            case 1: std::snprintf(buffer, sizeof(buffer), "x%.2f", v); break;
            default: std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0); break;
            }
        } else {
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0);
        }
        return buffer;
    }

    std::array<Rect, clatter::kSoundCount> cells_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ClatterUI)
};

UI* createUI()
{
    return new ClatterUI();
}

END_NAMESPACE_DISTRHO
