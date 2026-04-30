#include "DistrhoUI.hpp"

#include "ground_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using downspout::ground::kParameterCount;
using downspout::ground::kParamActionMutateCell;
using downspout::ground::kParamActionNewForm;
using downspout::ground::kParamActionNewPhrase;
using downspout::ground::kParamCadence;
using downspout::ground::kParamChannel;
using downspout::ground::kParamDensity;
using downspout::ground::kParamFormBars;
using downspout::ground::kParamMotion;
using downspout::ground::kParamPhraseBars;
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
    {kParamRootNote, "Root", 0.0f, 127.0f, true},
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamMotion, "Motion", 0.0f, 1.0f, false},
    {kParamTension, "Tension", 0.0f, 1.0f, false},
    {kParamCadence, "Cadence", 0.0f, 1.0f, false},
    {kParamRegisterArc, "Reg Arc", 0.0f, 1.0f, false},
    {kParamSequence, "Sequence", 0.0f, 1.0f, false},
    {kParamVary, "Vary", 0.0f, 100.0f, true},
    {kParamSeed, "Seed", 1.0f, 65535.0f, true},
};

constexpr const char* kScaleNames[] = {
    "Minor", "Major", "Dorian", "Phrygian", "Pent Minor", "Blues",
    "Mixolydian", "Harm Minor", "Pent Major", "Locrian", "Phryg Dom"
};

constexpr const char* kStyleNames[] = {
    "Grounded", "Ostinato", "March", "Pulse", "Drone", "Climb"
};

constexpr const char* kFormNames[] = {
    "8", "16", "32", "64"
};

constexpr float kFormValues[] = {
    8.0f, 16.0f, 32.0f, 64.0f
};

constexpr const char* kPhraseNames[] = {
    "2", "4", "8"
};

constexpr float kPhraseValues[] = {
    2.0f, 4.0f, 8.0f
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

constexpr std::array<const char*, 16> kChannelNames = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr std::array<float, 16> kChannelValues = {
    1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f,
    9.0f, 10.0f, 11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f
};

constexpr SelectorDef kSelectors[] = {
    {kParamScale, "Scale", kScaleNames, 11, nullptr},
    {kParamStyle, "Style", kStyleNames, 6, nullptr},
    {kParamFormBars, "Form", kFormNames, 4, kFormValues},
    {kParamPhraseBars, "Phrase", kPhraseNames, 3, kPhraseValues},
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
    default:
        std::snprintf(buffer, sizeof(buffer), "%.2f", value);
        return buffer;
    }
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
        values_[kParamChannel] = 1.0f;
        values_[kParamFormBars] = 16.0f;
        values_[kParamPhraseBars] = 4.0f;
        values_[kParamDensity] = 0.45f;
        values_[kParamMotion] = 0.55f;
        values_[kParamTension] = 0.45f;
        values_[kParamCadence] = 0.50f;
        values_[kParamRegister] = 1.0f;
        values_[kParamRegisterArc] = 0.40f;
        values_[kParamSequence] = 0.35f;
        values_[kParamSeed] = 1.0f;
        values_[kParamVary] = 0.0f;
        values_[kParamStatusPhrase] = 1.0f;
        values_[kParamStatusRole] = 0.0f;

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
            return false;
        }

        if (openSelector_ >= 0) {
            if (handleOpenSelectorClick(x, y)) {
                return true;
            }
            openSelector_ = -1;
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
                openSelector_ = static_cast<int>(i);
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

        return false;
    }

private:
    std::array<float, kParameterCount> values_ {};
    std::array<Rect, std::size(kSliders)> sliderRects_ {};
    std::array<Rect, std::size(kSelectors)> selectorRects_ {};
    std::array<Rect, std::size(kButtons)> buttonRects_ {};
    int draggingSlider_ = -1;
    int openSelector_ = -1;

    static constexpr float kSelectorItemHeight = 30.0f;

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

        drawPill(x + 22.0f, y + h - 34.0f, 190.0f, 24.0f, "DPF VST3 generator", 104, 188, 142);

        const int currentPhrase = std::max(1, static_cast<int>(std::lround(values_[kParamStatusPhrase])));
        const int currentRole = clampi(static_cast<int>(std::lround(values_[kParamStatusRole])), 0, 6);

        drawStatusCard(x + w - 266.0f, y + 14.0f, 116.0f, h - 28.0f, "Phrase", std::to_string(currentPhrase), 81, 146, 201);
        drawStatusCard(x + w - 136.0f, y + 14.0f, 116.0f, h - 28.0f, "Role", kRoleNames[currentRole], colorForRole(currentRole).r, colorForRole(currentRole).g, colorForRole(currentRole).b);
    }

    void drawPill(const float x,
                  const float y,
                  const float w,
                  const float h,
                  const char* label,
                  const int r,
                  const int g,
                  const int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 36);
        fill();
        strokeColor(r, g, b, 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(r, g, b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
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

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 230, 234, 255);
        text(x + 20.0f, y + 18.0f, "Form Preview", nullptr);

        const int formBars = clampi(static_cast<int>(std::lround(values_[kParamFormBars])), 8, 64);
        const int phraseBars = std::max(1, clampi(static_cast<int>(std::lround(values_[kParamPhraseBars])), 2, 8));
        const int phraseCount = std::max(1, formBars / phraseBars);
        const float tension = values_[kParamTension];
        const float cadence = values_[kParamCadence];
        const float sequence = values_[kParamSequence];
        const int currentPhrase = clampi(static_cast<int>(std::lround(values_[kParamStatusPhrase])) - 1, 0, phraseCount - 1);

        const float laneX = x + 20.0f;
        const float laneY = y + 56.0f;
        const float laneW = w - 40.0f;
        const float laneH = 64.0f;
        const float cellGap = 10.0f;
        const float cellW = (laneW - cellGap * (phraseCount - 1)) / static_cast<float>(phraseCount);

        drawTensionArc(laneX, laneY - 10.0f, laneW, 34.0f, phraseCount, tension, cadence);

        for (int phraseIndex = 0; phraseIndex < phraseCount; ++phraseIndex) {
            const float cellX = laneX + static_cast<float>(phraseIndex) * (cellW + cellGap);
            const int role = predictedRoleForPhrase(phraseIndex, phraseCount, tension, cadence, sequence);
            const RoleColor color = colorForRole(role);
            const bool active = phraseIndex == currentPhrase;

            beginPath();
            roundedRect(cellX, laneY, cellW, laneH, 14.0f);
            fillColor(color.r, color.g, color.b, active ? 88 : 42);
            fill();
            strokeColor(color.r, color.g, color.b, active ? 240 : 170);
            strokeWidth(active ? 2.2f : 1.0f);
            stroke();
            closePath();

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fillColor(150, 162, 176, 255);
            const std::string topLabel = "P" + std::to_string(phraseIndex + 1);
            text(cellX + 10.0f, laneY + 10.0f, topLabel.c_str(), nullptr);

            fontSize(16.0f);
            fillColor(238, 242, 245, 255);
            text(cellX + 10.0f, laneY + 28.0f, kRoleShortNames[role], nullptr);

            fontSize(11.0f);
            fillColor(168, 181, 194, 255);
            const std::string bottomLabel = std::to_string(phraseBars) + " bars";
            text(cellX + 10.0f, laneY + 49.0f, bottomLabel.c_str(), nullptr);
        }

        const int peakPhrase = predictedPeakPhrase(phraseCount, tension);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(150, 162, 176, 255);
        const std::string footer = "Peak around phrase " + std::to_string(peakPhrase + 1) +
                                   " | Form " + std::to_string(formBars) +
                                   " bars | Phrase " + std::to_string(phraseBars) + " bars";
        text(x + 20.0f, y + h - 28.0f, footer.c_str(), nullptr);
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
        const float rowGap = 18.0f;
        const float rowH = 48.0f;
        const float colGap = 16.0f;
        const float colW = (innerW - colGap) * 0.5f;

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const float rx = innerX + static_cast<float>(col) * (colW + colGap);
            const float ry = innerY + static_cast<float>(row) * (rowH + rowGap);
            sliderRects_[i] = {rx, ry + 18.0f, colW, 22.0f};
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

        float cy = y + 54.0f;
        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            selectorRects_[i] = {x + 20.0f, cy, w - 40.0f, 56.0f};
            drawSelector(static_cast<int>(i), kSelectors[i], selectorRects_[i], values_[kSelectors[i].index]);
            cy += 66.0f;
        }

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(226, 230, 234, 255);
        text(x + 20.0f, cy + 2.0f, "Actions", nullptr);
        cy += 30.0f;

        const float buttonGap = 10.0f;
        const float buttonH = 42.0f;
        for (std::size_t i = 0; i < std::size(kButtons); ++i) {
            buttonRects_[i] = {x + 20.0f, cy, w - 40.0f, buttonH};
            drawButton(kButtons[i], buttonRects_[i]);
            cy += buttonH + buttonGap;
        }

        drawHintBlock(x + 20.0f, y + h - 84.0f, w - 40.0f, 64.0f);
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

        fontSize(18.0f);
        fillColor(235, 239, 242, 255);
        text(rect.x + 16.0f, rect.y + 31.0f,
             def.items[selectorValueToIndex(def, value)], nullptr);

        fontSize(20.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(117, 133, 149, 255);
        text(rect.x + rect.w - 18.0f, rect.y + rect.h * 0.5f + 1.0f,
             openSelector_ == selectorIndex ? "˄" : "˅", nullptr);
    }

    void drawButton(const ButtonDef& def, const Rect& rect)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 14.0f);
        fillColor(76, 96, 120, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 13.0f);
        strokeColor(167, 187, 211, 110);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(16.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(240, 244, 247, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);
    }

    void drawHintBlock(const float x, const float y, const float w, const float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 14.0f);
        fillColor(18, 23, 30, 255);
        fill();
        closePath();

        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(149, 164, 179, 255);
        text(x + 14.0f, y + 14.0f, "Wheel or drag for quick edits.", nullptr);
        text(x + 14.0f, y + 32.0f, "Use New Phrase in motion, New Form for bigger resets.", nullptr);
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

    void cycleSelector(const int selectorIndex, const int direction)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const int currentIndex = selectorValueToIndex(def, values_[def.index]);
        const int nextIndex = clampi(currentIndex + direction, 0, def.count - 1);
        setParameter(def.index, selectorIndexToValue(def, nextIndex));
    }

    void drawOpenSelectorMenu(const int selectorIndex)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const Rect& base = selectorRects_[static_cast<std::size_t>(selectorIndex)];
        const int selected = selectorValueToIndex(def, values_[def.index]);

        const Rect menuRect = {
            base.x,
            base.y + base.h + 6.0f,
            base.w,
            static_cast<float>(def.count) * kSelectorItemHeight
        };

        beginPath();
        roundedRect(menuRect.x, menuRect.y, menuRect.w, menuRect.h, 14.0f);
        fillColor(22, 28, 36, 248);
        fill();
        strokeColor(93, 112, 134, 220);
        strokeWidth(1.0f);
        stroke();
        closePath();

        for (int i = 0; i < def.count; ++i) {
            const float rowY = menuRect.y + static_cast<float>(i) * kSelectorItemHeight;
            if (i == selected) {
                beginPath();
                roundedRect(menuRect.x + 4.0f, rowY + 3.0f, menuRect.w - 8.0f, kSelectorItemHeight - 6.0f, 10.0f);
                fillColor(74, 96, 122, 255);
                fill();
                closePath();
            }

            fontSize(13.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(236, 240, 243, 255);
            text(menuRect.x + 14.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, def.items[i], nullptr);
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
        const Rect menuRect = {
            base.x,
            base.y + base.h + 6.0f,
            base.w,
            static_cast<float>(def.count) * kSelectorItemHeight
        };
        if (!menuRect.contains(x, y)) {
            return false;
        }

        const int item = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, def.count - 1);
        setParameter(def.index, selectorIndexToValue(def, item));
        openSelector_ = -1;
        repaint();
        return true;
    }

    void triggerButton(const int buttonIndex)
    {
        const uint32_t index = kButtons[buttonIndex].index;
        editParameter(index, true);
        setParameterValue(index, 1.0f);
        setParameterValue(index, 0.0f);
        editParameter(index, false);
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
