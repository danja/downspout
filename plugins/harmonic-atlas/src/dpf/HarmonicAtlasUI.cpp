#include "generative_panel_ui.hpp"
#include "harmonic_atlas_core.hpp"

#include <cmath>

START_NAMESPACE_DISTRHO

class HarmonicAtlasUI : public GenerativePanelUI {
public:
    HarmonicAtlasUI()
        : GenerativePanelUI(
            "Harmonic Atlas",
            "Choose a harmonic language, shape its voicing, then follow the current root",
            downspout::harmonic_atlas::kParameterSpecs.data(),
            downspout::harmonic_atlas::kParameterCount, 104, 188, 206) {}

private:
    void onNanoDisplay() override
    {
        using namespace downspout::harmonic_atlas;
        static constexpr const char* movements[] {
            "Tonal", "Modal", "Chromatic", "Neo-Riemannian"
        };
        static constexpr const char* inversions[] {
            "Close", "Spread", "Drop", "Open"
        };
        static constexpr const char* scaleNames[] {
            "Major", "Ionian", "Minor", "Harm Minor", "Mel Minor",
            "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
            "Phryg Dom", "Neo Major", "Neo Minor",
            "Pent Major", "Pent Minor", "Blues",
            "Whole Tone", "Altered", "Half-Whole Dim",
            "Whole-Half Dim", "Bebop Dom", "Bebop Major", "Bebop Minor"
        };

        beginPanel();

        drawSection(24, 104, 576, 240, "HARMONY", "what changes, and how often");
        drawChoice(kStyle, 38, 136, 548, "Movement language", movements, 4,
                   "Select the harmonic movement family.");
        drawChoice(kScale, 38, 193, 548, "Colour-note scale", scaleNames, 23,
                   "Scale used to select colour notes added above the chord.");
        drawSlider(kRhythmBars, 38, 253, 264, "Chord every", " bars",
                   "How many bars each chord lasts.", 0);
        drawSlider(kCadenceBars, 310, 253, 276, "Cadence every", " bars",
                   "How often the progression aims for a cadence.", 0);

        drawSection(24, 358, 576, 282, "VOICING", "shape and density");
        drawPercentSlider(kTension, 38, 391, 264, "Tension", "Harmonic color and instability.");
        drawPercentSlider(kVoiceLeading, 310, 391, 276, "Voice-leading", "Prefer smaller movements between chords.");
        drawChoice(kInversionRange, 38, 447, 548, "Voicing spread", inversions, 4,
                   "Select how widely chord notes may be distributed.");
        drawSlider(kVoiceCount, 38, 506, 264, "Chord voices", "", "Maximum chord-note count.", 0);
        drawPercentSlider(kScaleNotes, 310, 506, 276, "Colour-note chance", "Chance of adding an in-scale colour note.");

        drawSection(614, 104, 322, 300, "INPUT & ROUTING", "optional MIDI guidance");
        drawToggle(kFollowInput, 628, 137, 294, "Follow incoming pitch classes",
                   "Use the most recent incoming note as the harmonic root.");
        drawPitchClassSlider(kRoot, 628, 187, 294, "Fallback root",
                             "Root used when input following is disabled or no note has arrived.");
        drawChannelSlider(kChannel, 628, 243, 142, "Output", "MIDI output channel.");
        drawSlider(kSeed, 778, 243, 144, "Seed", "", "Right-click to restore the default seed.", 0);
        {
            static constexpr const char* kCondChNames[] = {
                "Off", "1", "2", "3", "4", "5", "6", "7", "8",
                "9", "10", "11", "12", "13", "14", "15", "16"
            };
            drawChoice(kConductorChannel, 628, 301, 294, "Conductor ch",
                       kCondChNames, 17,
                       "Receive Conductor CC 20 (scene\xe2\x86\x92style), 22 (energy\xe2\x86\x92tension), "
                       "23 (mutation\xe2\x86\x92inversion), 24 (reset) on this MIDI channel.");
        }

        drawSection(614, 418, 322, 234, "NOW PLAYING", "read-only processor state");
        const int root = static_cast<int>(std::lround(value(kStatusRoot)));
        char rootText[32] {};
        std::snprintf(rootText, sizeof(rootText), "%s  ·  step %d",
                      pitchClassName(root), static_cast<int>(std::lround(value(kStatusChord))));
        drawReadout(628, 452, 294, 58, "Current harmonic position", rootText);
        const std::int64_t chord = static_cast<std::int64_t>(std::llround(value(kStatusChord)));
        const bool minor = static_cast<int>(std::lround(value(kStyle))) == 1
            || downspout::generative::randomUnit(
                static_cast<std::uint64_t>(std::lround(value(kSeed))), chord + 71)
                < value(kTension) * 0.45f;
        drawKeyboard(628, 528, 294, 94, root, minor,
                     static_cast<int>(std::lround(value(kVoiceCount))),
                     value(kTension));

        endPanel();
    }

    void drawKeyboard(const float x,
                      const float y,
                      const float w,
                      const float h,
                      const int root,
                      const bool minor,
                      const int voices,
                      const float tension)
    {
        static constexpr int whitePitch[] {0, 2, 4, 5, 7, 9, 11};
        const int intervals[] {0, minor ? 3 : 4, 7, tension > 0.45f ? 10 : 11, 14, 17};
        const float whiteW = w / 7.0f;
        for (int i = 0; i < 7; ++i) {
            bool active = false;
            for (int voice = 0; voice < std::clamp(voices, 0, 6); ++voice)
                active = active || whitePitch[i] == (root + intervals[voice]) % 12;
            beginPath();
            fillColor(active ? accentR() : 222, active ? accentG() : 226,
                      active ? accentB() : 222, 255);
            roundedRect(x + i * whiteW + 1.0f, y, whiteW - 2.0f, h, 2.0f);
            fill();
        }
        static constexpr int blackAfter[] {0, 1, 3, 4, 5};
        static constexpr int blackPitch[] {1, 3, 6, 8, 10};
        for (int i = 0; i < 5; ++i) {
            bool active = false;
            for (int voice = 0; voice < std::clamp(voices, 0, 6); ++voice)
                active = active || blackPitch[i] == (root + intervals[voice]) % 12;
            beginPath();
            fillColor(active ? accentR() : 25, active ? accentG() : 30,
                      active ? accentB() : 35, 255);
            roundedRect(x + (blackAfter[i] + 0.68f) * whiteW, y, whiteW * 0.62f, h * 0.62f, 2.0f);
            fill();
        }
    }
};

UI* createUI() { return new HarmonicAtlasUI(); }

END_NAMESPACE_DISTRHO
