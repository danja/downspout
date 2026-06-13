#include "DistrhoUI.hpp"

#include "ground_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::ground::kParameterCount;
using downspout::ground::kParamActionMutateCell;
using downspout::ground::kParamActionNewForm;
using downspout::ground::kParamActionNewPhrase;
using downspout::ground::kParamCadence;
using downspout::ground::kParamChannel;
using downspout::ground::kParamClamp;
using downspout::ground::kParamColor;
using downspout::ground::kParamDensity;
using downspout::ground::kParamFormBars;
using downspout::ground::kParamFormShape;
using downspout::ground::kParamMotion;
using downspout::ground::kParamNoteLength;
using downspout::ground::kParamNoteLengthVariation;
using downspout::ground::kParamPhraseBars;
using downspout::ground::kParamPhraseRoleCount;
using downspout::ground::kParamPhraseRoleStart;
using downspout::ground::kParamRegister;
using downspout::ground::kParamRegisterArc;
using downspout::ground::kParamRootNote;
using downspout::ground::kParamScale;
using downspout::ground::kParamSeed;
using downspout::ground::kParamSequence;
using downspout::ground::kParamStatusPhrase;
using downspout::ground::kParamStatusRole;
using downspout::ground::kParamStyle;
using downspout::ground::kParamTension;
using downspout::ground::kParamVary;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= (x + w) && py >= y && py <= (y + h);
    }
};

struct SliderDef {
    uint32_t index;
    const char* label;
    float min;
    float max;
    bool integer;
};

struct SelectorDef {
    uint32_t index;
    const char* label;
    const char* const* items;
    int count;
    const float* values;
};

struct ButtonDef {
    uint32_t index;
    const char* label;
};

constexpr SliderDef kSliders[] = {
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamMotion, "Motion", 0.0f, 1.0f, false},
    {kParamTension, "Tension", 0.0f, 1.0f, false},
    {kParamColor, "Color", 0.0f, 1.0f, false},
    {kParamCadence, "Cadence", 0.0f, 1.0f, false},
    {kParamRegisterArc, "Register Arc", 0.0f, 1.0f, false},
    {kParamClamp, "Clamp", 12.0f, 48.0f, true},
    {kParamSequence, "Sequence", 0.0f, 1.0f, false},
    {kParamNoteLength, "Note Length", 0.0f, 1.0f, false},
    {kParamNoteLengthVariation, "Note Length Variation", 0.0f, 1.0f, false},
    {kParamVary, "Vary", 0.0f, 100.0f, true},
    {kParamSeed, "Seed", 1.0f, 65535.0f, true},
};

constexpr const char* kScaleNames[] = {
    "Minor", "Major", "Dorian", "Phrygian", "Pent Minor", "Blues",
    "Mixolydian", "Harm Minor", "Pent Major", "Locrian", "Phryg Dom",
    "Lydian", "Mel Minor", "Whole Tone", "Altered", "Half-Whole Dim",
    "Whole-Half Dim", "Bebop Dom", "Bebop Major", "Bebop Minor"
};

constexpr const char* kStyleNames[] = {
    "Grounded", "Ostinato", "March", "Pulse", "Drone", "Climb", "Dub", "Jazz", "Rock"
};

constexpr const char* kFormShapeNames[] = {
    "Free", "12 Blues", "12 Blues Quick", "12 Minor Blues", "12 Jazz Blues",
    "16 Classical", "16 Fugue", "32 Jazz AABA", "32 Rhythm", "16 Techno",
    "16 Dub", "8 Ambient", "32 Rondo"
};

constexpr const char* kFormNames[] = {
    "8", "12", "16", "32", "64"
};

constexpr float kFormValues[] = {
    8.0f, 12.0f, 16.0f, 32.0f, 64.0f
};

constexpr const char* kPhraseNames[] = {
    "1", "2", "3", "4", "6", "8", "12"
};

constexpr float kPhraseValues[] = {
    1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f, 12.0f
};

constexpr const char* kRegisterNames[] = {
    "Sub", "Low", "Mid", "High"
};

constexpr float kRegisterValues[] = {
    0.0f, 1.0f, 2.0f, 3.0f
};

constexpr const char* kRoleNames[] = {
    "Statement", "Answer", "Climb", "Pedal", "Breakdown", "Cadence", "Release"
};

constexpr const char* kRoleShortNames[] = {
    "STAT", "ANS", "CLMB", "PED", "BRK", "CAD", "REL"
};

constexpr const char* kRoleTinyNames[] = {
    "S", "A", "C", "P", "B", "K", "R"
};

constexpr const char* kRoleMenuNames[] = {
    "Auto", "Statement", "Answer", "Climb", "Pedal", "Breakdown", "Cadence", "Release"
};

constexpr std::array<const char*, 16> kChannelNames = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr std::array<float, 16> kChannelValues = {
    1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
    9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f
};

constexpr SelectorDef kSelectors[] = {
    {kParamScale, "Scale", kScaleNames, 20, nullptr},
    {kParamStyle, "Style", kStyleNames, static_cast<int>(std::size(kStyleNames)), nullptr},
    {kParamFormShape, "Shape", kFormShapeNames, static_cast<int>(std::size(kFormShapeNames)), nullptr},
    {kParamFormBars, "Form", kFormNames, static_cast<int>(std::size(kFormNames)), kFormValues},
    {kParamPhraseBars, "Phrase", kPhraseNames, static_cast<int>(std::size(kPhraseNames)), kPhraseValues},
    {kParamRegister, "Register", kRegisterNames, 4, kRegisterValues},
    {kParamChannel, "Channel", kChannelNames.data(), 16, kChannelValues.data()},
};

constexpr ButtonDef kButtons[] = {
    {kParamActionNewForm, "New Form"},
    {kParamActionNewPhrase, "New Phrase"},
    {kParamActionMutateCell, "Mutate Cell"},
};

[[nodiscard]] float clampf(const float value, const float minValue, const float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(const int value, const int minValue, const int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const char* noteName(const int value)
{
    static constexpr const char* kNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    return kNames[(value % 12 + 12) % 12];
}

[[nodiscard]] int selectorValueToIndex(const SelectorDef& def, const float value)
{
    if (def.values == nullptr) {
        return clampi(static_cast<int>(std::lround(value)), 0, def.count - 1);
    }

    int bestIndex = 0;
    float bestDistance = std::fabs(value - def.values[0]);
    for (int i = 1; i < def.count; ++i) {
        const float distance = std::fabs(value - def.values[i]);
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }
    return bestIndex;
}

[[nodiscard]] float selectorIndexToValue(const SelectorDef& def, const int index)
{
    const int safeIndex = clampi(index, 0, def.count - 1);
    if (def.values == nullptr) {
        return static_cast<float>(safeIndex);
    }
    return def.values[safeIndex];
}

[[nodiscard]] std::string formatSliderValue(const uint32_t index, const float value)
{
    char buffer[64];
    switch (index) {
    case kParamRootNote: {
        const int midi = static_cast<int>(std::lround(value));
        const int octave = midi / 12 - 1;
        std::snprintf(buffer, sizeof(buffer), "%s%d", noteName(midi), octave);
        return buffer;
    }
    case kParamVary:
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value)));
        return buffer;
    case kParamSeed:
        std::snprintf(buffer, sizeof(buffer), "%u", static_cast<unsigned int>(std::lround(value)));
        return buffer;
    case kParamClamp:
        std::snprintf(buffer, sizeof(buffer), "%d st", static_cast<int>(std::lround(value)));
        return buffer;
    default:
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
        return buffer;
    }
}

[[nodiscard]] std::string formatRootValue(const float value)
{
    char buffer[64];
    const int midi = static_cast<int>(std::lround(value));
    const int octave = midi / 12 - 1;
    std::snprintf(buffer, sizeof(buffer), "%s%d", noteName(midi), octave);
    return buffer;
}

[[nodiscard]] int predictedPeakPhrase(const int phraseCount, const float tension)
{
    return clampi(static_cast<int>(std::lround(static_cast<double>(phraseCount - 1) *
                                               (0.35 + tension * 0.40))),
                  1,
                  std::max(1, phraseCount - 1));
}

[[nodiscard]] int predictedRoleForPhrase(const int phraseIndex,
                                         const int phraseCount,
                                         const float tension,
                                         const float cadence,
                                         const float sequence)
{
    if (phraseIndex <= 0) {
        return 0;
    }
    if (phraseIndex == phraseCount - 1) {
        return cadence >= 0.28f ? 5 : 6;
    }

    const int peak = predictedPeakPhrase(phraseCount, tension);
    if (phraseIndex == peak || phraseIndex + 1 == peak) {
        return 2;
    }

    const float progress = static_cast<float>(phraseIndex) / static_cast<float>(std::max(1, phraseCount - 1));
    if (progress > 0.60f && tension > 0.55f && phraseIndex == phraseCount - 2) {
        return 4;
    }
    if (sequence > 0.45f && (phraseIndex % 2) == 1) {
        return 1;
    }
    if (progress > 0.35f && tension < 0.25f) {
        return 3;
    }
    return (phraseIndex % 2) == 0 ? 0 : 1;
}

[[nodiscard]] uint32_t roleParamForPhrase(const int phraseIndex)
{
    return kParamPhraseRoleStart + static_cast<uint32_t>(clampi(phraseIndex, 0, static_cast<int>(kParamPhraseRoleCount) - 1));
}

[[nodiscard]] int roleOverrideValueToRole(const float value)
{
    return clampi(static_cast<int>(std::lround(value)) - 1, 0, static_cast<int>(std::size(kRoleNames)) - 1);
}

[[nodiscard]] bool bluesShape(const int shape)
{
    return shape >= 1 && shape <= 4;
}

[[nodiscard]] bool structuredShape(const int shape)
{
    return shape >= 1 && shape <= 12;
}

[[nodiscard]] int formBarsForShape(const int shape)
{
    switch (shape) {
    case 11: return 8;
    case 1:
    case 2:
    case 3:
    case 4:
        return 12;
    case 5:
    case 6:
    case 9:
    case 10:
        return 16;
    case 7:
    case 8:
    case 12:
        return 32;
    default:
        break;
    }
    return 16;
}

[[nodiscard]] int phraseBarsForShape(const int shape)
{
    switch (shape) {
    case 1:
    case 2:
    case 3:
    case 4:
        return 1;
    case 6:
    case 11:
        return 2;
    case 5:
    case 7:
    case 8:
    case 9:
    case 10:
    case 12:
        return 4;
    default:
        break;
    }
    return 4;
}

[[nodiscard]] int predictedBluesRoleForPhrase(const int shape, const int phraseIndex)
{
    static constexpr int kStandard[] = {0, 1, 0, 1, 2, 1, 0, 4, 2, 1, 5, 5};
    static constexpr int kJazz[] = {0, 1, 0, 1, 2, 2, 0, 1, 2, 5, 5, 5};
    const int index = clampi(phraseIndex, 0, 11);
    return shape == 4 ? kJazz[index] : kStandard[index];
}

[[nodiscard]] int predictedStructuredRoleForPhrase(const int shape, const int phraseIndex)
{
    static constexpr int kClassical16[] = {0, 1, 2, 5};
    static constexpr int kFugue16[] = {0, 1, 0, 1, 2, 3, 2, 5};
    static constexpr int kJazzAaba32[] = {0, 1, 0, 5, 2, 2, 1, 5};
    static constexpr int kRhythm32[] = {0, 1, 2, 5, 0, 1, 2, 5};
    static constexpr int kTechno16[] = {3, 0, 2, 5};
    static constexpr int kDub16[] = {3, 1, 4, 5};
    static constexpr int kAmbient8[] = {3, 3, 6, 6};
    static constexpr int kRondo32[] = {0, 1, 0, 2, 0, 4, 0, 5};

    switch (shape) {
    case 5: return kClassical16[clampi(phraseIndex, 0, 3)];
    case 6: return kFugue16[clampi(phraseIndex, 0, 7)];
    case 7: return kJazzAaba32[clampi(phraseIndex, 0, 7)];
    case 8: return kRhythm32[clampi(phraseIndex, 0, 7)];
    case 9: return kTechno16[clampi(phraseIndex, 0, 3)];
    case 10: return kDub16[clampi(phraseIndex, 0, 3)];
    case 11: return kAmbient8[clampi(phraseIndex, 0, 3)];
    case 12: return kRondo32[clampi(phraseIndex, 0, 7)];
    default:
        break;
    }
    return 0;
}

struct RoleColor {
    int r;
    int g;
    int b;
};

[[nodiscard]] RoleColor colorForRole(const int role)
{
    switch (role) {
    case 0: return {94, 146, 201};
    case 1: return {80, 176, 154};
    case 2: return {214, 137, 73};
    case 3: return {161, 137, 205};
    case 4: return {199, 98, 116};
    case 5: return {227, 181, 90};
    case 6: return {105, 187, 123};
    default: return {120, 128, 136};
    }
}

}  // namespace

class GroundUI : public UI
{
public:
    GroundUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamRootNote] = 36.0f;
        values_[kParamScale] = 0.0f;
        values_[kParamStyle] = 0.0f;
        values_[kParamFormShape] = 0.0f;
        values_[kParamChannel] = 1.0f;
        values_[kParamFormBars] = 16.0f;
        values_[kParamPhraseBars] = 4.0f;
        values_[kParamDensity] = 0.45f;
        values_[kParamMotion] = 0.55f;
        values_[kParamTension] = 0.45f;
        values_[kParamColor] = 0.0f;
        values_[kParamCadence] = 0.50f;
        values_[kParamRegister] = 1.0f;
        values_[kParamRegisterArc] = 0.40f;
        values_[kParamClamp] = 12.0f;
        values_[kParamSequence] = 0.35f;
        values_[kParamNoteLength] = 0.35f;
        values_[kParamNoteLengthVariation] = 0.35f;
        values_[kParamSeed] = 1.0f;
        values_[kParamVary] = 0.0f;
        values_[kParamStatusPhrase] = 1.0f;
        values_[kParamStatusRole] = 0.0f;
        for (std::size_t i = 0; i < kParamPhraseRoleCount; ++i) {
            values_[kParamPhraseRoleStart + i] = 0.0f;
        }

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index < values_.size()) {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float height = static_cast<float>(getHeight());
        const float pad = 22.0f;
        const float headerH = 94.0f;
        const float structureH = 184.0f;

        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, headerH);
        drawStructurePanel(pad, pad + headerH + 16.0f, width - pad * 2.0f, structureH);

        const float contentY = pad + headerH + structureH + 34.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.60f;
        const float rightW = width - leftW - pad * 3.0f;

        drawSliderPanel(pad, contentY, leftW, contentH);
        drawRightPanel(pad * 2.0f + leftW, contentY, rightW, contentH);

        if (openSelector_ >= 0) {
            drawOpenSelectorMenu(openSelector_);
        }
        if (openRootMenu_) {
            drawOpenRootMenu();
        }
        if (openRoleMenu_ >= 0) {
            drawOpenRoleMenu(openRoleMenu_);
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) {
            return false;
        }

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        if (!ev.press) {
            draggingSlider_ = -1;
            pulsedButton_ = -1;
            repaint();
            return false;
        }

        if (openSelector_ >= 0) {
            if (handleOpenSelectorClick(x, y)) {
                return true;
            }
            openSelector_ = -1;
        }

        if (openRootMenu_) {
            if (handleOpenRootClick(x, y)) {
                return true;
            }
            openRootMenu_ = false;
        }

        if (openRoleMenu_ >= 0) {
            if (handleOpenRoleClick(x, y)) {
                return true;
            }
            openRoleMenu_ = -1;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                draggingSlider_ = static_cast<int>(i);
                updateSliderFromPosition(static_cast<int>(i), x);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i) {
            if (selectorRects_[i].contains(x, y)) {
                openRoleMenu_ = -1;
                openRootMenu_ = false;
                openSelector_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }

        if (rootRect_.contains(x, y)) {
            openSelector_ = -1;
            openRoleMenu_ = -1;
            openRootMenu_ = true;
            repaint();
            return true;
        }

        for (std::size_t i = 0; i < phraseRoleRects_.size(); ++i) {
            if (phraseRoleRects_[i].contains(x, y)) {
                openSelector_ = -1;
                openRootMenu_ = false;
                openRoleMenu_ = static_cast<int>(i);
                repaint();
                return true;
            }
        }

        for (std::size_t i = 0; i < buttonRects_.size(); ++i) {
            if (buttonRects_[i].contains(x, y)) {
                triggerButton(static_cast<int>(i));
                return true;
            }
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingSlider_ >= 0) {
            updateSliderFromPosition(draggingSlider_, static_cast<float>(ev.pos.getX()));
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                nudgeSlider(static_cast<int>(i), ev.delta.getY() > 0.0f ? 1.0f : -1.0f);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectorRects_.size(); ++i) {
            if (selectorRects_[i].contains(x, y)) {
                if (openSelector_ == static_cast<int>(i)) {
                    openSelector_ = -1;
                    repaint();
                } else {
                    cycleSelector(static_cast<int>(i), ev.delta.getY() > 0.0f ? -1 : 1);
                }
                return true;
            }
        }

        if (rootRect_.contains(x, y)) {
            nudgeRoot(ev.delta.getY() > 0.0f ? -1.0f : 1.0f);
            return true;
        }

        for (std::size_t i = 0; i < phraseRoleRects_.size(); ++i) {
            if (phraseRoleRects_[i].contains(x, y)) {
                if (openRoleMenu_ == static_cast<int>(i)) {
                    openRoleMenu_ = -1;
                    repaint();
                } else {
                    cycleRoleSelector(static_cast<int>(i), ev.delta.getY() > 0.0f ? -1 : 1);
                }
                return true;
            }
        }

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, std::size(kSliders)> sliderRects_ {};
    std::array<Rect, std::size(kSelectors)> selectorRects_ {};
    std::array<Rect, std::size(kButtons)> buttonRects_ {};
    std::array<Rect, kParamPhraseRoleCount> phraseRoleRects_ {};
    Rect rootRect_ {};
    int draggingSlider_ = -1;
    int openSelector_ = -1;
    bool openRootMenu_ = false;
    int openRoleMenu_ = -1;
    int pulsedButton_ = -1;

    static constexpr float kSelectorItemHeight = 30.0f;
    static constexpr int kSelectorMenuMaxRows = 10;

    void drawBackground(const float width, const float height)
    {
        beginPath();
        fillColor(15, 19, 25, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(24, 31, 42, 255);
        rect(0.0f, 0.0f, width, height * 0.30f);
        fill();
        closePath();
    }

    void drawHeader(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(28, 38, 52, 238);
        fill();
        closePath();

        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(239, 242, 245, 255);
        text(x + 20.0f, y + 16.0f, "Ground", nullptr);

        fontSize(13.0f);
        fillColor(160, 174, 188, 255);
        text(x + 22.0f, y + 52.0f, "Long-form bass movement across phrases, sections, and cadences", nullptr);

        const int currentPhrase = std::max(1, static_cast<int>(std::lround(values_[kParamStatusPhrase])));
        const int currentRole = clampi(static_cast<int>(std::lround(values_[kParamStatusRole])), 0, 6);

        drawStatusCard(x + w - 266.0f, y + 14.0f, 116.0f, h - 28.0f, "Phrase", std::to_string(currentPhrase), 81, 146, 201);
        drawStatusCard(x + w - 136.0f, y + 14.0f, 116.0f, h - 28.0f, "Role", kRoleNames[currentRole], colorForRole(currentRole).r, colorForRole(currentRole).g, colorForRole(currentRole).b);
    }

    void drawStatusCard(const float x,
                        const float y,
                        const float w,
                        const float h,
                        const char* label,
                        const std::string& value,
                        const int r,
                        const int g,
                        const int b)
    {
        beginPath();
        roundedRect(x, y, w, h, 14.0f);
        fillColor(19, 24, 31, 255);
        fill();
        strokeColor(r, g, b, 148);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(148, 162, 177, 255);
        text(x + 12.0f, y + 10.0f, label, nullptr);

        fontSize(20.0f);
        fillColor(r, g, b, 255);
        text(x + 12.0f, y + 31.0f, value.c_str(), nullptr);
    }

    void drawStructurePanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(23, 28, 37, 248);
        fill();
        closePath();

        phraseRoleRects_.fill({});

        const int formShape = clampi(static_cast<int>(std::lround(values_[kParamFormShape])),
                                     0,
                                     static_cast<int>(std::size(kFormShapeNames)) - 1);
        const int formBars = structuredShape(formShape)
            ? formBarsForShape(formShape)
            : clampi(static_cast<int>(std::lround(values_[kParamFormBars])), 8, 64);
        const int phraseBars = structuredShape(formShape)
            ? phraseBarsForShape(formShape)
            : std::max(1, clampi(static_cast<int>(std::lround(values_[kParamPhraseBars])), 1, 12));
        const int phraseCount = clampi(formBars / phraseBars, 1, static_cast<int>(kParamPhraseRoleCount));
        const float tension = values_[kParamTension];
        const float cadence = values_[kParamCadence];
        const float sequence = values_[kParamSequence];
        const int currentPhrase = clampi(static_cast<int>(std::lround(values_[kParamStatusPhrase])) - 1, 0, phraseCount - 1);

        const float laneX = x + 20.0f;
        const float laneY = y + 36.0f;
        const float laneW = w - 40.0f;
        const float laneH = 64.0f;
        const float cellGap = 10.0f;
        const float cellW = (laneW - cellGap * (phraseCount - 1)) / static_cast<float>(phraseCount);

        drawTensionArc(laneX, laneY - 10.0f, laneW, 34.0f, phraseCount, tension, cadence);

        for (int phraseIndex = 0; phraseIndex < phraseCount; ++phraseIndex) {
            const float cellX = laneX + static_cast<float>(phraseIndex) * (cellW + cellGap);
            const int generatedRole = bluesShape(formShape)
                ? predictedBluesRoleForPhrase(formShape, phraseIndex)
                : structuredShape(formShape)
                    ? predictedStructuredRoleForPhrase(formShape, phraseIndex)
                : predictedRoleForPhrase(phraseIndex, phraseCount, tension, cadence, sequence);
            const float roleOverrideValue = values_[roleParamForPhrase(phraseIndex)];
            const bool roleEdited = roleOverrideValue >= 0.5f;
            const int role = roleEdited ? roleOverrideValueToRole(roleOverrideValue) : generatedRole;
            const RoleColor color = colorForRole(role);
            const bool active = phraseIndex == currentPhrase;
            const bool open = openRoleMenu_ == phraseIndex;

            phraseRoleRects_[static_cast<std::size_t>(phraseIndex)] = {cellX, laneY, cellW, laneH};

            beginPath();
            roundedRect(cellX, laneY, cellW, laneH, 14.0f);
            fillColor(color.r, color.g, color.b, active ? 96 : (roleEdited ? 62 : 42));
            fill();
            strokeColor(color.r, color.g, color.b, active || open ? 240 : 170);
            strokeWidth(active || open ? 2.2f : 1.0f);
            stroke();
            closePath();

            if (cellW >= 54.0f) {
                fontSize(11.0f);
                textAlign(ALIGN_LEFT | ALIGN_TOP);
                fillColor(150, 162, 176, 255);
                const std::string topLabel = "P" + std::to_string(phraseIndex + 1);
                text(cellX + 10.0f, laneY + 10.0f, topLabel.c_str(), nullptr);

                fontSize(cellW < 68.0f ? 13.0f : 16.0f);
                fillColor(238, 242, 245, 255);
                text(cellX + 10.0f, laneY + 28.0f, kRoleShortNames[role], nullptr);

                fontSize(16.0f);
                textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
                fillColor(210, 220, 228, roleEdited ? 230 : 150);
                text(cellX + cellW - 10.0f, laneY + 36.0f, open ? "˄" : "˅", nullptr);

                if (cellW >= 70.0f) {
                    fontSize(11.0f);
                    textAlign(ALIGN_LEFT | ALIGN_TOP);
                    fillColor(168, 181, 194, 255);
                    const std::string bottomLabel = std::to_string(phraseBars) + (phraseBars == 1 ? " bar" : " bars");
                    text(cellX + 10.0f, laneY + 49.0f, bottomLabel.c_str(), nullptr);
                }
            } else {
                fontSize(15.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                fillColor(238, 242, 245, 255);
                text(cellX + cellW * 0.5f, laneY + laneH * 0.5f + 1.0f, kRoleTinyNames[role], nullptr);
            }
        }

    }

    void drawTensionArc(const float x,
                        const float y,
                        const float w,
                        const float h,
                        const int phraseCount,
                        const float tension,
                        const float cadence)
    {
        beginPath();
        moveTo(x, y + h);
        for (int i = 0; i < phraseCount; ++i) {
            const float t = phraseCount > 1 ? static_cast<float>(i) / static_cast<float>(phraseCount - 1) : 0.0f;
            const float peak = 0.35f + tension * 0.40f;
            const float distance = std::fabs(t - peak);
            const float heightNorm = clampf(1.0f - distance * 2.6f, 0.16f, 1.0f);
            const float lift = h * (0.18f + heightNorm * (0.55f + cadence * 0.18f));
            lineTo(x + w * t, y + h - lift);
        }
        strokeColor(225, 178, 86, 190);
        strokeWidth(2.0f);
        stroke();
        closePath();
    }

    void drawSliderPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(23, 28, 37, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 230, 234, 255);
        text(x + 20.0f, y + 18.0f, "Motion", nullptr);

        const float innerX = x + 20.0f;
        const float innerY = y + 52.0f;
        const float innerW = w - 40.0f;
        const float rowGap = 10.0f;
        const float rowH = 38.0f;
        const float colGap = 16.0f;
        const float colW = (innerW - colGap) * 0.5f;

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const float rx = innerX + static_cast<float>(col) * (colW + colGap);
            const float ry = innerY + static_cast<float>(row) * (rowH + rowGap);
            sliderRects_[i] = {rx, ry + 16.0f, colW, 18.0f};
            drawSlider(kSliders[i], sliderRects_[i], values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const float value, const bool active)
    {
        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(154, 169, 183, 255);
        text(rect.x, rect.y - 18.0f, def.label, nullptr);

        const std::string valueText = formatSliderValue(def.index, value);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fillColor(223, 230, 236, 255);
        text(rect.x + rect.w, rect.y - 18.0f, valueText.c_str(), nullptr);

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 10.0f);
        fillColor(42, 50, 62, 255);
        fill();
        closePath();

        const float t = (value - def.min) / (def.max - def.min);
        beginPath();
        roundedRect(rect.x, rect.y, rect.w * clampf(t, 0.0f, 1.0f), rect.h, 10.0f);
        fillColor(active ? 223 : 196, active ? 127 : 92, 76, 255);
        fill();
        closePath();
    }

    void drawRightPanel(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(23, 28, 37, 248);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 230, 234, 255);
        text(x + 20.0f, y + 18.0f, "Frame", nullptr);

        const float innerX = x + 20.0f;
        const float innerW = w - 40.0f;
        const float colGap = 10.0f;
        const float colW = (innerW - colGap) * 0.5f;
        const float selectorH = 50.0f;
        const float selectorGap = 8.0f;
        const float selectorTop = y + 54.0f;
        rootRect_ = {innerX, selectorTop, colW, selectorH};
        drawRootSelector(rootRect_);

        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            const std::size_t controlIndex = i + 1u;
            const int col = static_cast<int>(controlIndex % 2u);
            const int row = static_cast<int>(controlIndex / 2u);
            selectorRects_[i] = {
                innerX + static_cast<float>(col) * (colW + colGap),
                selectorTop + static_cast<float>(row) * (selectorH + selectorGap),
                colW,
                selectorH
            };
            drawSelector(static_cast<int>(i), kSelectors[i], selectorRects_[i], values_[kSelectors[i].index]);
        }

        const int selectorRows = static_cast<int>((std::size(kSelectors) + 2u) / 2u);
        float cy = selectorTop + static_cast<float>(selectorRows) * selectorH +
                   static_cast<float>(selectorRows - 1) * selectorGap + 20.0f;
        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 230, 234, 255);
        text(x + 20.0f, cy + 2.0f, "Actions", nullptr);
        cy += 26.0f;

        const float buttonGap = 10.0f;
        const float buttonH = 34.0f;
        const float buttonW = (innerW - buttonGap * static_cast<float>(std::size(kButtons) - 1)) / static_cast<float>(std::size(kButtons));
        for (std::size_t i = 0; i < std::size(kButtons); ++i) {
            buttonRects_[i] = {innerX + static_cast<float>(i) * (buttonW + buttonGap), cy, buttonW, buttonH};
            drawButton(kButtons[i], buttonRects_[i], pulsedButton_ == static_cast<int>(i));
        }
    }

    void drawSelector(const int selectorIndex, const SelectorDef& def, const Rect& rect, const float value)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 16.0f);
        fillColor(34, 43, 55, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(152, 166, 181, 255);
        text(rect.x + 16.0f, rect.y + 12.0f, def.label, nullptr);

        const char* const item = def.items[selectorValueToIndex(def, value)];
        const float valueSize = std::strlen(item) > 13u ? 12.0f : (rect.w < 150.0f ? 15.0f : 18.0f);
        fontSize(valueSize);
        fillColor(235, 239, 242, 255);
        text(rect.x + 16.0f, rect.y + 31.0f,
             item, nullptr);

        fontSize(20.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(117, 133, 149, 255);
        text(rect.x + rect.w - 18.0f, rect.y + rect.h * 0.5f + 1.0f,
             openSelector_ == selectorIndex ? "˄" : "˅", nullptr);
    }

    void drawRootSelector(const Rect& rect)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 16.0f);
        fillColor(34, 43, 55, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(152, 166, 181, 255);
        text(rect.x + 16.0f, rect.y + 12.0f, "Root", nullptr);

        const std::string valueText = formatRootValue(values_[kParamRootNote]);
        fontSize(rect.w < 150.0f ? 15.0f : 18.0f);
        fillColor(235, 239, 242, 255);
        text(rect.x + 16.0f, rect.y + 31.0f, valueText.c_str(), nullptr);

        fontSize(20.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(117, 133, 149, 255);
        text(rect.x + rect.w - 18.0f, rect.y + rect.h * 0.5f + 1.0f,
             openRootMenu_ ? "˄" : "˅", nullptr);
    }

    void drawButton(const ButtonDef& def, const Rect& rect, const bool active)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 14.0f);
        fillColor(active ? 205 : 76, active ? 139 : 96, active ? 82 : 120, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 13.0f);
        strokeColor(active ? 248 : 167, active ? 203 : 187, active ? 144 : 211, active ? 190 : 110);
        strokeWidth(active ? 2.0f : 1.0f);
        stroke();
        closePath();

        fontSize(rect.w < 96.0f ? 12.0f : 14.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(240, 244, 247, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);
    }

    void updateSliderFromPosition(const int sliderIndex, const float mouseX)
    {
        const SliderDef& def = kSliders[sliderIndex];
        const Rect& rect = sliderRects_[static_cast<std::size_t>(sliderIndex)];
        const float t = clampf((mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        float value = def.min + t * (def.max - def.min);
        if (def.integer) {
            value = std::round(value);
        }
        setParameter(def.index, value);
    }

    void nudgeSlider(const int sliderIndex, const float direction)
    {
        const SliderDef& def = kSliders[sliderIndex];
        const float step = def.integer ? 1.0f : (def.max - def.min) / 100.0f;
        setParameter(def.index, values_[def.index] + direction * step);
    }

    void nudgeRoot(const float direction)
    {
        setParameter(kParamRootNote, clampf(values_[kParamRootNote] + direction, 0.0f, 127.0f));
    }

    void cycleSelector(const int selectorIndex, const int direction)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const int currentIndex = selectorValueToIndex(def, values_[def.index]);
        const int nextIndex = clampi(currentIndex + direction, 0, def.count - 1);
        setParameter(def.index, selectorIndexToValue(def, nextIndex));
    }

    void cycleRoleSelector(const int phraseIndex, const int direction)
    {
        const uint32_t param = roleParamForPhrase(phraseIndex);
        const int currentIndex = clampi(static_cast<int>(std::lround(values_[param])), 0, static_cast<int>(std::size(kRoleMenuNames)) - 1);
        const int nextIndex = clampi(currentIndex + direction, 0, static_cast<int>(std::size(kRoleMenuNames)) - 1);
        setParameter(param, static_cast<float>(nextIndex));
    }

    void drawOpenSelectorMenu(const int selectorIndex)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const Rect& base = selectorRects_[static_cast<std::size_t>(selectorIndex)];
        const int selected = selectorValueToIndex(def, values_[def.index]);
        const int columns = std::max(1, (def.count + kSelectorMenuMaxRows - 1) / kSelectorMenuMaxRows);
        const int rows = (def.count + columns - 1) / columns;
        const float itemW = std::max(base.w, 158.0f);
        const float menuW = itemW * static_cast<float>(columns);
        const float menuH = static_cast<float>(rows) * kSelectorItemHeight;
        const float windowW = static_cast<float>(getWidth());
        const float windowH = static_cast<float>(getHeight());
        const float gap = 6.0f;

        float menuX = clampf(base.x, 10.0f, std::max(10.0f, windowW - menuW - 10.0f));
        float menuY = base.y + base.h + gap;
        if (menuY + menuH > windowH - 10.0f) {
            menuY = base.y - menuH - gap;
        }
        menuY = clampf(menuY, 10.0f, std::max(10.0f, windowH - menuH - 10.0f));

        const Rect menuRect = {menuX, menuY, menuW, menuH};

        beginPath();
        roundedRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 14.0f);
        fillColor(22, 28, 36, 248);
        fill();
        strokeColor(93, 112, 134, 220);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (int i = 0; i < def.count; ++i) {
            const int col = i / rows;
            const int row = i % rows;
            const float itemX = menuRect.x + static_cast<float>(col) * itemW;
            const float rowY = menuRect.y + static_cast<float>(row) * kSelectorItemHeight;
            if (i == selected) {
                beginPath();
                roundedRect(itemX + 4.0f, rowY + 3.0f, itemW - 8.0f, kSelectorItemHeight - 6.0f, 10.0f);
                fillColor(74, 96, 122, 255);
                fill();
                closePath();
            }

            fontSize(13.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(236, 240, 243, 255);
            text(itemX + 14.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, def.items[i], nullptr);
        }
    }

    bool handleOpenSelectorClick(const float x, const float y)
    {
        const Rect& base = selectorRects_[static_cast<std::size_t>(openSelector_)];
        if (base.contains(x, y)) {
            openSelector_ = -1;
            repaint();
            return true;
        }

        const SelectorDef& def = kSelectors[openSelector_];
        const int columns = std::max(1, (def.count + kSelectorMenuMaxRows - 1) / kSelectorMenuMaxRows);
        const int rows = (def.count + columns - 1) / columns;
        const float itemW = std::max(base.w, 158.0f);
        const float menuW = itemW * static_cast<float>(columns);
        const float menuH = static_cast<float>(rows) * kSelectorItemHeight;
        const float windowW = static_cast<float>(getWidth());
        const float windowH = static_cast<float>(getHeight());
        const float gap = 6.0f;

        float menuX = clampf(base.x, 10.0f, std::max(10.0f, windowW - menuW - 10.0f));
        float menuY = base.y + base.h + gap;
        if (menuY + menuH > windowH - 10.0f) {
            menuY = base.y - menuH - gap;
        }
        menuY = clampf(menuY, 10.0f, std::max(10.0f, windowH - menuH - 10.0f));

        const Rect menuRect = {menuX, menuY, menuW, menuH};
        if (!menuRect.contains(x, y)) {
            return false;
        }

        const int col = clampi(static_cast<int>((x - menuRect.x) / itemW), 0, columns - 1);
        const int row = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, rows - 1);
        const int item = col * rows + row;
        if (item >= def.count) {
            return true;
        }
        setParameter(def.index, selectorIndexToValue(def, item));
        openSelector_ = -1;
        repaint();
        return true;
    }

    [[nodiscard]] Rect rootMenuRect() const
    {
        constexpr int kRootMenuRows = 10;
        const int columns = (128 + kRootMenuRows - 1) / kRootMenuRows;
        const float itemW = 58.0f;
        const float menuW = itemW * static_cast<float>(columns);
        const float menuH = static_cast<float>(kRootMenuRows) * kSelectorItemHeight;
        const float windowW = static_cast<float>(getWidth());
        const float windowH = static_cast<float>(getHeight());
        const float gap = 6.0f;

        float menuX = clampf(rootRect_.x, 10.0f, std::max(10.0f, windowW - menuW - 10.0f));
        float menuY = rootRect_.y + rootRect_.h + gap;
        if (menuY + menuH > windowH - 10.0f) {
            menuY = rootRect_.y - menuH - gap;
        }
        menuY = clampf(menuY, 10.0f, std::max(10.0f, windowH - menuH - 10.0f));
        return {menuX, menuY, menuW, menuH};
    }

    void drawOpenRootMenu()
    {
        constexpr int kRootMenuRows = 10;
        const float itemW = 58.0f;
        const int selected = clampi(static_cast<int>(std::lround(values_[kParamRootNote])), 0, 127);
        const Rect menuRect = rootMenuRect();

        beginPath();
        roundedRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 14.0f);
        fillColor(22, 28, 36, 248);
        fill();
        strokeColor(93, 112, 134, 220);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (int i = 0; i < 128; ++i) {
            const int col = i / kRootMenuRows;
            const int row = i % kRootMenuRows;
            const float itemX = menuRect.x + static_cast<float>(col) * itemW;
            const float rowY = menuRect.y + static_cast<float>(row) * kSelectorItemHeight;
            if (i == selected) {
                beginPath();
                roundedRect(itemX + 4.0f, rowY + 3.0f, itemW - 8.0f, kSelectorItemHeight - 6.0f, 10.0f);
                fillColor(74, 96, 122, 255);
                fill();
                closePath();
            }

            const std::string label = formatRootValue(static_cast<float>(i));
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(236, 240, 243, 255);
            text(itemX + 10.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, label.c_str(), nullptr);
        }
    }

    bool handleOpenRootClick(const float x, const float y)
    {
        if (rootRect_.contains(x, y)) {
            openRootMenu_ = false;
            repaint();
            return true;
        }

        constexpr int kRootMenuRows = 10;
        const int columns = (128 + kRootMenuRows - 1) / kRootMenuRows;
        const float itemW = 58.0f;
        const Rect menuRect = rootMenuRect();
        if (!menuRect.contains(x, y)) {
            return false;
        }

        const int col = clampi(static_cast<int>((x - menuRect.x) / itemW), 0, columns - 1);
        const int row = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, kRootMenuRows - 1);
        const int item = col * kRootMenuRows + row;
        if (item >= 128) {
            return true;
        }
        setParameter(kParamRootNote, static_cast<float>(item));
        openRootMenu_ = false;
        repaint();
        return true;
    }

    [[nodiscard]] Rect roleMenuRect(const int phraseIndex) const
    {
        const Rect& base = phraseRoleRects_[static_cast<std::size_t>(phraseIndex)];
        const float itemW = std::max(base.w, 148.0f);
        const float menuW = itemW;
        const float menuH = static_cast<float>(std::size(kRoleMenuNames)) * kSelectorItemHeight;
        const float windowW = static_cast<float>(getWidth());
        const float windowH = static_cast<float>(getHeight());
        const float gap = 6.0f;

        float menuX = clampf(base.x, 10.0f, std::max(10.0f, windowW - menuW - 10.0f));
        float menuY = base.y + base.h + gap;
        if (menuY + menuH > windowH - 10.0f) {
            menuY = base.y - menuH - gap;
        }
        menuY = clampf(menuY, 10.0f, std::max(10.0f, windowH - menuH - 10.0f));
        return {menuX, menuY, menuW, menuH};
    }

    void drawOpenRoleMenu(const int phraseIndex)
    {
        const uint32_t param = roleParamForPhrase(phraseIndex);
        const int selected = clampi(static_cast<int>(std::lround(values_[param])), 0, static_cast<int>(std::size(kRoleMenuNames)) - 1);
        const Rect menuRect = roleMenuRect(phraseIndex);

        beginPath();
        roundedRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 14.0f);
        fillColor(22, 28, 36, 248);
        fill();
        strokeColor(93, 112, 134, 220);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (std::size_t i = 0; i < std::size(kRoleMenuNames); ++i) {
            const float rowY = menuRect.y + static_cast<float>(i) * kSelectorItemHeight;
            if (static_cast<int>(i) == selected) {
                beginPath();
                roundedRect(menuRect.x + 4.0f, rowY + 3.0f, menuRect.w - 8.0f, kSelectorItemHeight - 6.0f, 10.0f);
                fillColor(74, 96, 122, 255);
                fill();
                closePath();
            }

            fontSize(13.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(236, 240, 243, 255);
            text(menuRect.x + 14.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, kRoleMenuNames[i], nullptr);
        }
    }

    bool handleOpenRoleClick(const float x, const float y)
    {
        const Rect& base = phraseRoleRects_[static_cast<std::size_t>(openRoleMenu_)];
        if (base.contains(x, y)) {
            openRoleMenu_ = -1;
            repaint();
            return true;
        }

        const Rect menuRect = roleMenuRect(openRoleMenu_);
        if (!menuRect.contains(x, y)) {
            return false;
        }

        const int item = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, static_cast<int>(std::size(kRoleMenuNames)) - 1);
        setParameter(roleParamForPhrase(openRoleMenu_), static_cast<float>(item));
        openRoleMenu_ = -1;
        repaint();
        return true;
    }

    void triggerButton(const int buttonIndex)
    {
        const uint32_t index = kButtons[buttonIndex].index;
        float value = values_[index] + 1.0f;
        if (value > 1048576.0f) {
            value = 1.0f;
        }
        pulsedButton_ = buttonIndex;
        setParameter(index, value);
        repaint();
    }

    void setParameter(const uint32_t index, const float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroundUI)
};

UI* createUI()
{
    return new GroundUI();
}

END_NAMESPACE_DISTRHO
