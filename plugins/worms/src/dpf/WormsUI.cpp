#include "generative_panel_ui.hpp"
#include "worms_params.hpp"

#include <cmath>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace {
using namespace downspout::worms;

constexpr const char* kStepSizeNames[]  = {"1/4", "1/8", "1/16", "1/32"};
constexpr const char* kPatLenNames[]    = {"16", "32", "64", "128"};
constexpr const char* kTurnNames[]      = {"L120", "L60", "Fwd", "R60", "R120"};
constexpr const char* kCondChNames[]    = {
    "Off",
    "Ch 1",  "Ch 2",  "Ch 3",  "Ch 4",  "Ch 5",  "Ch 6",  "Ch 7",  "Ch 8",
    "Ch 9",  "Ch 10", "Ch 11", "Ch 12", "Ch 13", "Ch 14", "Ch 15", "Ch 16"
};
constexpr const char* kScaleNames[] = {
    "Major", "Ionian", "Minor", "Harm Min", "Mel Min",
    "Dorian", "Phrygian", "Lydian", "Mixo", "Locrian",
    "Phryg Dom", "Neo Maj", "Neo Min",
    "Pent Maj", "Pent Min", "Blues",
    "Whole Tone", "Altered", "H-W Dim", "W-H Dim",
    "Bebop Dom", "Bebop Maj", "Bebop Min"
};
constexpr const char* kMidiChNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};
constexpr const char* kRootNames[] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};
constexpr const char* kRegNames[] = { "Oct -2", "Oct -1", "Oct 0", "Oct +1", "Oct +2" };

const char* ruleRowLabel(int d)
{
    static const char* const labels[] = {
        "Rule E\xE2\x86\x92",   // → (E incoming)
        "Rule NE\xE2\x86\x92",
        "Rule NW\xE2\x86\x92",
        "Rule W\xE2\x86\x92",
        "Rule SW\xE2\x86\x92",
        "Rule SE\xE2\x86\x92",
    };
    return (d >= 0 && d < 6) ? labels[d] : "Rule";
}

}  // namespace

class WormsUI : public GenerativePanelUI
{
public:
    WormsUI()
        : GenerativePanelUI(
            "ToneWorm",
            "Paterson's Worm navigates a Tonnetz lattice of pitch classes",
            downspout::worms::kParamSpecs.data(),
            downspout::worms::kParameterCount,
            78, 180, 130)
    {}

private:
    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());

        beginPanel();

        // ── Top row: navigation parameters ─────────────────────────────
        drawSection(24, 100, W - 48, 62, "NAVIGATION");
        drawChoice(kParamRoot,     38, 122, 160, "Root", kRootNames, 12,
                   "Root pitch class of the Tonnetz lattice.");
        drawChoice(kParamReg,     206, 122, 130, "Register", kRegNames, 5,
                   "Octave register relative to root.");
        drawChoice(kParamStepSize,344, 122, 160, "Step", kStepSizeNames, 4,
                   "Note grid resolution.");
        drawChoice(kParamPatLen,  512, 122, 160, "Length", kPatLenNames, 4,
                   "Pattern length in steps before looping.");
        drawChoice(kParamMidiCh,  680, 122, 196, "MIDI Ch", kMidiChNames, 16,
                   "Output MIDI channel.");

        // ── Worm rule section ───────────────────────────────────────────
        drawSection(24, 178, W - 48, 210, "WORM RULES",
                    "Given incoming direction, choose turn (L120/L60/Fwd/R60/R120)");

        for (int d = 0; d < 6; ++d) {
            const float ry = 200.0f + d * 31.0f;
            drawChoice(static_cast<uint32_t>(kParamRule0 + d),
                       38, ry, W - 76, ruleRowLabel(d), kTurnNames, 5,
                       "Turn rule for this incoming direction.");
        }

        // ── Action buttons ──────────────────────────────────────────────
        drawAction(kParamActionRandomize, 38, 398, 140, 32,
                   "Randomize", "Randomize all six worm rules.");
        drawAction(kParamActionMutate,   188, 398, 140, 32,
                   "Mutate",    "Mutate one random worm rule.");

        // ── Bottom row: generation controls ────────────────────────────
        drawSection(24, 446, W - 48, 90, "GENERATION");

        drawPercentSlider(kParamDensity,  38, 468, 112, "Density",
                          "Probability that each step produces a note (vs rest).");
        drawPercentSlider(kParamVelocity,158, 468, 112, "Velocity",
                          "Base MIDI velocity.");
        drawPercentSlider(kParamVary,    278, 468, 112, "Vary",
                          "Rule mutation rate at loop boundaries.");
        drawSlider(kParamSeed,           398, 468, 112, "Seed", nullptr,
                   "Random seed for deterministic generation.");
        drawChoice(kParamCondCh,         518, 468, 180, "Cond. Ch",
                   kCondChNames, 17,
                   "Conductor MIDI channel (CC21=density, CC22=velocity, CC23=vary, CC24=mutate).");

        // ── Scale quantization ──────────────────────────────────────────
        drawSection(706, 446, W - 730, 90, "QUANTIZE");
        drawToggle(kParamQuantize, 720, 468, W - 744, "Scale",
                   "Quantize Tonnetz pitch classes to the selected scale.");
        const bool quantizeOn = value(kParamQuantize) >= 0.5f;
        if (quantizeOn) {
            drawChoice(kParamScale, 720, 502, W - 744, "Scale", kScaleNames, 23,
                       "Scale for quantization.");
        }

        endPanel();
    }

    bool onMouse(const MouseEvent& ev) override { return handleMouse(ev); }
    bool onMotion(const MotionEvent& ev) override { return handleMotion(ev); }
    bool onScroll(const ScrollEvent& ev) override { return handleScroll(ev); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WormsUI)
};

UI* createUI() { return new WormsUI(); }

END_NAMESPACE_DISTRHO
