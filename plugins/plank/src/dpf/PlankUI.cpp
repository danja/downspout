#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "plank_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using namespace downspout::plank;

constexpr float kGridX = 24.0f;
constexpr float kGridY = 100.0f;
constexpr float kCell = 42.0f;
constexpr float kGap = 5.0f;
constexpr float kGridPitch = kCell + kGap;
constexpr float kGridSize = kGridWidth * kGridPitch - kGap;

constexpr float kPanelX = kGridX + kGridSize + 16.0f;
constexpr float kPanelW = 316.0f;

// Derived from DISTRHO_UI_DEFAULT_* so the layout and the declared window size
// cannot drift apart silently. The height is sized to hold every lane of
// controls plus the button strip; layoutPanel() enforces that budget.
constexpr float kWidth = DISTRHO_UI_DEFAULT_WIDTH;
constexpr float kHeight = DISTRHO_UI_DEFAULT_HEIGHT;

constexpr float kPanelPad = 14.0f;
constexpr float kSelectorH = 34.0f;
constexpr float kSliderH = 30.0f;
constexpr float kLaneLabelH = 13.0f;
constexpr float kRowGap = 3.0f;
constexpr float kLaneGap = 4.0f;
constexpr float kItemGap = 5.0f;
constexpr float kButtonH = 26.0f;
constexpr float kButtonGap = 8.0f;

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(const float px, const float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

constexpr std::size_t kSliderCount = 28;
constexpr std::size_t kSelectorCount = 13;
constexpr std::size_t kPerSliderRow = 5;
constexpr std::size_t kButtonCount = 5;
constexpr std::size_t kToggleCount = 4;

struct SliderDef {
    ParamId parameter;
    const char* label;
    std::uint8_t lane;  // 0 osc, 1 filter, 2 env1, 3 env2, 4 pitch, 5 fx
};

struct SelectorDef {
    ParamId parameter;
    const char* label;
    std::uint8_t lane;
};

// Parameter sliders, arranged in five labelled lanes so the panel reads as a
// signal path rather than a wall of knobs.
constexpr SliderDef kSliders[kSliderCount] = {
    {ParamId::morph, "Morph", 0},
    {ParamId::detune, "Detune", 0},
    {ParamId::drive, "Drive", 0},
    {ParamId::noise, "Noise", 0},
    {ParamId::strike, "Strike", 0},
    {ParamId::damping, "Damp", 0},
    {ParamId::material, "Material", 0},

    {ParamId::cutoff, "Cutoff", 1},
    {ParamId::resonance, "Reso", 1},
    {ParamId::filterEnv, "F Env", 1},

    {ParamId::envLevel, "Level", 2},
    {ParamId::envAttack, "Attack", 2},
    {ParamId::envDecay, "Decay", 2},
    {ParamId::envSustain, "Sus", 2},
    {ParamId::envRelease, "Rel", 2},

    {ParamId::modLevel, "Mod Lvl", 3},
    {ParamId::modAttack, "M Atk", 3},
    {ParamId::modDecay, "M Dec", 3},
    {ParamId::modSustain, "M Sus", 3},
    {ParamId::modRelease, "M Rel", 3},

    {ParamId::lfoAFrequency, "A Rate", 4},
    {ParamId::lfoBFrequency, "B Rate", 4},
    {ParamId::interval, "Interval", 4},
    {ParamId::glide, "Glide", 4},

    {ParamId::delaySend, "Dly", 5},
    {ParamId::reverbSend, "Verb", 5},
    {ParamId::width, "Width", 5},
    {ParamId::level, "Level", 5},
};

constexpr SelectorDef kSelectors[kSelectorCount] = {
    {ParamId::engine, "Engine", 0},
    {ParamId::exciter, "Exciter", 0},
    {ParamId::scale, "Scale", 4},
    {ParamId::spread, "Spread", 4},
    {ParamId::root, "Root", 4},
    {ParamId::octave, "Octave", 4},
    {ParamId::rotate, "Rotate", 4},
    {ParamId::stride, "Stride", 4},
    {ParamId::lfoAShape, "LFO A", 4},
    {ParamId::lfoATarget, "A Dest", 4},
    {ParamId::lfoBShape, "LFO B", 4},
    {ParamId::lfoBTarget, "B Dest", 4},
    {ParamId::baseChannel, "Base Ch", 5},
};

constexpr ParamId kButtons[kButtonCount] = {
    ParamId::latch,
    ParamId::midiThru,
    ParamId::ledFeedback,
    ParamId::passInput,
};

constexpr const char* kButtonLabels[kToggleCount] = {
    "Latch",
    "MIDI",
    "LED",
    "Pass",
};

constexpr const char* kLaneLabels[6] = {
    "OSCILLATORS",
    "FILTER",
    "AMPLITUDE ENV",
    "MOD ENV",
    "PITCH AND LFO",
    "EFFECTS",
};

[[nodiscard]] float clampf(const float value, const float minimum, const float maximum) noexcept
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] std::uint32_t idx(const ParamId id) { return static_cast<std::uint32_t>(id); }

[[nodiscard]] float normalized(const std::uint32_t parameter, const float value) noexcept
{
    const ParamSpec& spec = getParameterSpec(parameter);
    if (spec.maximum <= spec.minimum)
        return 0.0f;
    return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
}

// Scales and destinations use their display names; everything else reads as a
// plain number with the parameter's unit.
[[nodiscard]] std::string formatValue(const std::uint32_t parameter, const float value)
{
    char buffer[48];

    switch (static_cast<ParamId>(parameter))
    {
    case ParamId::scale:
    {
        const auto index = static_cast<std::size_t>(
            clampf(value, 0.0f, static_cast<float>(kScaleNames.size() - 1u)));
        return kScaleNames[index];
    }
    case ParamId::lfoAShape:
    case ParamId::lfoBShape:
        return kLfoShapeNames[static_cast<std::size_t>(clampf(value, 0.0f,
                                                              static_cast<float>(kLfoShapeNames.size() - 1u)))];
    case ParamId::lfoATarget:
    case ParamId::lfoBTarget:
        return kModTargetNames[static_cast<std::size_t>(clampf(value, 0.0f,
                                                              static_cast<float>(kModTargetNames.size() - 1u)))];
    case ParamId::engine:
        return kEngineNames[static_cast<std::size_t>(clampf(value, 0.0f,
                                                            static_cast<float>(kEngineNames.size() - 1u)))];
    case ParamId::exciter:
        return kExciterNames[static_cast<std::size_t>(clampf(value, 0.0f,
                                                              static_cast<float>(kExciterNames.size() - 1u)))];
    case ParamId::spread:
        return kSpreadNames[static_cast<std::size_t>(clampf(value, 0.0f,
                                                            static_cast<float>(kSpreadNames.size() - 1u)))];
    case ParamId::root:
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
        return buffer;
    case ParamId::octave:
    {
        const int octave = static_cast<int>(std::lround(value));
        std::snprintf(buffer, sizeof(buffer), "%+d", octave);
        return buffer;
    }
    default:
        break;
    }

    const ParamSpec& spec = getParameterSpec(parameter);
    if (spec.integer)
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(value)));
    else if (spec.maximum <= 1.0f && spec.minimum >= 0.0f)
        std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    else
        std::snprintf(buffer, sizeof(buffer), "%.0f%s", static_cast<double>(value),
                      spec.unit[0] != '\0' ? spec.unit : "");

    return buffer;
}

// Grid pads share the Launchpad's own vocabulary, so a player who knows the
// hardware recognises the layout. Mirrors plank_core.cpp exactly.
struct GridColour {
    int r;
    int g;
    int b;
};

[[nodiscard]] GridColour ledColour(const std::uint8_t value) noexcept
{
    switch (value)
    {
    case kLedDim: return {72, 74, 80};
    case kLedWhite: return {232, 236, 240};
    case kLedRed: return {214, 64, 58};
    case kLedOrange: return {232, 132, 44};
    case kLedYellow: return {236, 208, 84};
    case kLedGreen: return {96, 206, 118};
    case kLedCyan: return {72, 196, 208};
    case kLedBlue: return {78, 134, 226};
    case kLedPurple: return {162, 104, 214};
    case kLedPink: return {230, 108, 178};
    default: return {26, 30, 36};
    }
}

}  // namespace

class PlankUI : public UI {
public:
    PlankUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = getParameterSpec(i).defaultValue;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (index < values_.size())
        {
            values_[index] = value;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        drawBackground();
        drawHeader();
        drawGrid();
        drawScaleRuler();
        drawPanel();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press)
        {
            activeSlider_ = -1;
            return false;
        }

        const float x = static_cast<float>(ev.pos.getX());
        const float y = static_cast<float>(ev.pos.getY());

        for (std::size_t row = 0; row < kGridHeight; ++row)
        {
            for (std::size_t col = 0; col < kGridWidth; ++col)
            {
                if (!gridRect(row, col).contains(x, y))
                    continue;

                // The UI drives the same string control the Launchpad does, so
                // a cell here and a pad on the hardware are interchangeable.
                const bool sounding = values_[idx(ParamId::outString0) + col] > 0.5f;
                commitGrid(row, col, !sounding);
                return true;
            }
        }

        for (std::size_t i = 0; i < selectorCount_; ++i)
        {
            if (!selectorRects_[i].contains(x, y))
                continue;
            cycleSelector(i);
            return true;
        }

        for (std::size_t i = 0; i < kToggleCount; ++i)
        {
            if (!buttonRects_[i].contains(x, y))
                continue;
            commit(idx(kButtons[i]), values_[idx(kButtons[i])] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }

        // Panic is a trigger, not a toggle.
        if (buttonRects_[kToggleCount].contains(x, y))
        {
            triggerParameter(idx(ParamId::panic));
            return true;
        }

        for (std::size_t i = 0; i < sliderCount_; ++i)
        {
            if (!sliderRects_[i].contains(x, y))
                continue;
            activeSlider_ = static_cast<int>(i);
            setFromMouse(sliderRects_[i], x);
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (activeSlider_ < 0)
            return false;
        setFromMouse(sliderRects_[static_cast<std::size_t>(activeSlider_)],
                     static_cast<float>(ev.pos.getX()));
        return true;
    }

private:
    [[nodiscard]] const laf::Theme& t() const { return laf::defaultTheme(); }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kCellCount> cellRects_ {};
    std::array<Rect, kSliderCount> sliderRects_ {};
    std::array<Rect, kSelectorCount> selectorRects_ {};
    std::array<Rect, kButtonCount> buttonRects_ {};
    std::size_t sliderCount_ = 0;
    std::size_t selectorCount_ = 0;
    std::size_t buttonCount_ = 0;
    int activeSlider_ = -1;

    [[nodiscard]] static Rect gridRect(const std::size_t row, const std::size_t col) noexcept
    {
        // Row 0 is the bottom hardware row, matching padseq and the Launchpad.
        const float visualRow = static_cast<float>(kGridHeight - 1u - row);
        return {kGridX + static_cast<float>(col) * kGridPitch, kGridY + visualRow * kGridPitch, kCell, kCell};
    }

    void commit(const std::uint32_t parameter, const float value)
    {
        const ParamSpec& spec = getParameterSpec(parameter);
        float clamped = clampf(value, spec.minimum, spec.maximum);
        if (spec.integer)
            clamped = std::round(clamped);

        values_[parameter] = clamped;
        editParameter(parameter, true);
        setParameterValue(parameter, clamped);
        editParameter(parameter, false);
        repaint();
    }

    void commitGrid(const std::size_t row, const std::size_t col, const bool on)
    {
        // Cells are ordered row-major, matching cellIndex() in the core.
        commit(static_cast<std::uint32_t>(kCellParameterStart + cellIndex(row, col)), on ? 1.0f : 0.0f);
    }

    void cycleSelector(const std::size_t index)
    {
        if (index >= selectorCount_)
            return;

        const ParamId parameter = kSelectors[index].parameter;
        const ParamSpec& spec = getParameterSpec(idx(parameter));

        float next = std::round(values_[idx(parameter)]) + 1.0f;
        if (next > spec.maximum)
            next = spec.minimum;

        commit(idx(parameter), next);
    }

    void setFromMouse(const Rect& rect, const float x)
    {
        if (activeSlider_ < 0)
            return;

        const std::uint32_t parameter = idx(kSliders[static_cast<std::size_t>(activeSlider_)].parameter);
        const ParamSpec& spec = getParameterSpec(parameter);

        // Sliders are horizontal, so the value comes from x, not y.
        const float travel = rect.w - 8.0f;
        const float left = rect.x + 4.0f;
        const float value = clampf((x - left) / std::max(1.0f, travel), 0.0f, 1.0f);

        commit(parameter, spec.minimum + value * (spec.maximum - spec.minimum));
    }

    // ── Drawing ─────────────────────────────────────────────────────────────

    void drawBackground()
    {
        beginPath();
        fc(t().background);
        rect(0.0f, 0.0f, kWidth, kHeight);
        fill();

        beginPath();
        fc(t().panel);
        rect(0.0f, 0.0f, kWidth, 88.0f);
        fill();
    }

    void drawHeader()
    {
        fc(t().textPrimary);
        fontSize(30.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(kGridX, 18.0f, "Plank", nullptr);

        fc(t().textDim);
        fontSize(13.0f);
        text(kGridX + 118.0f, 26.0f, "eight strings, one per column", nullptr);

        fc(t().textDim);
        fontSize(11.0f);
        text(kGridX, 50.0f, "press a pad to pluck its column", nullptr);

        // Active-string lamp and output peak, parked at the right of the header
        // so they cannot collide with the subtitle.
        const float active = values_[idx(ParamId::outActiveStrings)];
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%d/8 strings   peak %d%%",
                      static_cast<int>(std::lround(active)),
                      static_cast<int>(std::lround(values_[idx(ParamId::outPeak)] * 100.0f)));

        fc(active > 0.5f ? t().textPrimary : t().textDisabled);
        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(kWidth - 24.0f, 34.0f, buffer, nullptr);

        const float lampX = kWidth - 24.0f - 138.0f;
        const float lampY = 57.0f;
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);

        beginPath();
        circle(lampX, lampY, 6.0f);
        if (active > 0.5f)
            fillColor(t().accent.r, t().accent.g, t().accent.b,
                      static_cast<uchar>(90 + 165 * clampf(active / 8.0f, 0.0f, 1.0f)));
        else
            fc(t().accentDim);
        fill();

        // Peak meter, so clipping is visible rather than only audible.
        const float peak = clampf(values_[idx(ParamId::outPeak)], 0.0f, 1.0f);
        const float meterX = lampX + 14.0f;
        const float meterW = 110.0f;

        beginPath();
        roundedRect(meterX, lampY - 4.0f, meterW, 8.0f, 2.0f);
        fc(t().meterOff);
        fill();
        closePath();

        beginPath();
        roundedRect(meterX, lampY - 4.0f, meterW * peak, 8.0f, 2.0f);
        if (peak > 0.98f)
            fc(t().danger);
        else
            fc(t().meterOn);
        fill();
        closePath();
    }

    void drawGrid()
    {
        for (std::size_t row = 0; row < kGridHeight; ++row)
        {
            for (std::size_t col = 0; col < kGridWidth; ++col)
            {
                const std::size_t index = cellIndex(row, col);
                const Rect rect = gridRect(row, col);
                cellRects_[index] = rect;

                const float level = values_[idx(ParamId::outString0) + col];
                drawPad(rect, row, col, level);
            }
        }

        // Column ruler: which string each column drives.
        fc(t().textDisabled);
        fontSize(9.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            char label[8];
            std::snprintf(label, sizeof(label), "%c", static_cast<char>('A' + col));
            text(kGridX + static_cast<float>(col) * kGridPitch + kCell * 0.5f, kGridY + kGridSize + 3.0f,
                 label, nullptr);
        }
    }

    // Every pad carries its own note name (see drawPad), because with the strings
    // spread each column plays a different ladder and no single ruler is right.
    // This caption just says how to read the grid.
    void drawScaleRuler()
    {
        const float y = kGridY + kGridSize + 17.0f;

        fc(t().textDisabled);
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(kGridX, y, "column = string, row = scale degree (bottom is lowest)", nullptr);
        text(kGridX, y + 13.0f, "pad labels show the note each cell plays", nullptr);
    }

    // Mirrors the engine's pitch mapping for display only. scaleStepAt() is
    // shared with the core, so the readout cannot drift from the sound.
    [[nodiscard]] int noteForRow(const std::size_t row, const std::size_t column) const noexcept
    {
        const auto p = [](ParamId id) { return static_cast<std::size_t>(id); };
        const auto scale = static_cast<std::size_t>(clampf(values_[p(ParamId::scale)], 0.0f,
                                                            static_cast<float>(ScaleId::count) - 1.0f));
        const int root = static_cast<int>(std::lround(values_[p(ParamId::root)]));
        const int octave = static_cast<int>(std::lround(values_[p(ParamId::octave)]));
        const int rotate = static_cast<int>(std::lround(values_[p(ParamId::rotate)]));
        const int stride = static_cast<int>(std::lround(values_[p(ParamId::stride)]));
        const double fine = static_cast<double>(values_[p(ParamId::microtune)]) * 0.01;

        int degree = static_cast<int>(row) + rotate;
        int columnSemitones = 0;
        switch (static_cast<SpreadId>(static_cast<int>(std::lround(values_[p(ParamId::spread)]))))
        {
        case SpreadId::unison:
            break;
        case SpreadId::scale:
            degree += static_cast<int>(column);
            break;
        case SpreadId::fourths:
            columnSemitones = static_cast<int>(column) * kFourthSemitones;
            break;
        case SpreadId::fifths:
            columnSemitones = static_cast<int>(column) * kFifthSemitones;
            break;
        case SpreadId::stride:
            degree += strideSteps(scale, stride, column);
            break;
        case SpreadId::count:
            break;
        }

        const double note = static_cast<double>(root + octave * 12 + scaleStepAt(scale, degree) +
                                                columnSemitones) +
                            fine;
        return static_cast<int>(clampf(static_cast<float>(std::lround(note)), 0.0f, 127.0f));
    }

    void drawPad(const Rect& rect, const std::size_t row, const std::size_t col, const float level)
    {
        const bool sounding = level > 0.001f;

        // Idle cells keep a visible tone that steps up with pitch row, so the
        // 8x8 reads as a grid and as a pitch gradient before anything is played.
        const float tint = static_cast<float>(row) / static_cast<float>(kGridHeight - 1u);
        int r = 30 + static_cast<int>(tint * 22.0f);
        int g = 36 + static_cast<int>(tint * 16.0f);
        int b = 46 + static_cast<int>(tint * 12.0f);

        if (sounding)
        {
            // Green through yellow to red, the same ramp the Launchpad uses for
            // a string's level, so hardware and UI agree.
            const GridColour colour = clampf(level, 0.0f, 1.0f) > 0.85f  ? ledColour(kLedRed)
                                      : clampf(level, 0.0f, 1.0f) > 0.55f ? ledColour(kLedOrange)
                                      : clampf(level, 0.0f, 1.0f) > 0.25f ? ledColour(kLedYellow)
                                                                           : ledColour(kLedGreen);
            r = colour.r;
            g = colour.g;
            b = colour.b;
        }

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, laf::kRadiusSmall);
        fillColor(r, g, b, sounding ? 248 : 225);
        fill();
        strokeColor(sounding ? r + 40 : 62, sounding ? g + 40 : 76, sounding ? b + 30 : 92,
                    sounding ? 245 : 170);
        strokeWidth(sounding ? 1.6f : laf::kBorderWidth);
        stroke();
        closePath();

        static constexpr const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                                       "F#", "G",  "G#", "A",  "A#", "B"};
        const int note = noteForRow(row, col);
        char label[16];
        std::snprintf(label, sizeof(label), "%s%d", kNoteNames[((note % 12) + 12) % 12], note / 12 - 1);
        fc(sounding ? t().background : t().textDim);
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, label, nullptr);
    }

    void drawPanel()
    {
        beginPath();
        roundedRect(kPanelX, kGridY - 14.0f, kPanelW, kHeight - kGridY - 2.0f, laf::kRadiusPanel);
        fc(t().panel);
        fill();
        sc(t().border);
        strokeWidth(laf::kBorderWidth);
        stroke();
        closePath();

        layoutPanel();
    }

    void layoutPanel()
    {
        sliderCount_ = 0;
        selectorCount_ = 0;
        buttonCount_ = 0;

        const float innerX = kPanelX + kPanelPad;
        const float innerW = kPanelW - 2.0f * kPanelPad;

        // Reserve the button strip first, then lay the lanes out above it so
        // the last lane cannot be pushed under the buttons.
        const float buttonY = kHeight - kPanelPad - kButtonH;
        const float buttonW = (innerW - static_cast<float>(kButtonCount - 1u) * kItemGap) /
                              static_cast<float>(kButtonCount);
        for (std::size_t i = 0; i < kToggleCount; ++i)
        {
            const Rect rect {innerX + static_cast<float>(i) * (buttonW + kItemGap), buttonY, buttonW, kButtonH};
            buttonRects_[buttonCount_++] = rect;
            drawButton(rect, kButtonLabels[i], idx(kButtons[i]));
        }

        // Panic shares the strip but is styled as a fault control.
        {
            const Rect rect {innerX + static_cast<float>(kToggleCount) * (buttonW + kItemGap), buttonY,
                             buttonW, kButtonH};
            buttonRects_[buttonCount_++] = rect;
            drawPanicButton(rect);
        }

        const float contentBottom = buttonY - kButtonGap;
        float y = kGridY + 2.0f;

        // Selectors first: scale, root and octave define the ladder, so they
        // belong above the timbre controls.
        y = drawSelectorLane(innerX, y, innerW);
        y += 8.0f;
        drawSliderLanes(innerX, y, innerW, contentBottom);
    }

    float drawSelectorLane(const float x, float y, const float w)
    {
        constexpr std::size_t kPerRow = 3;
        const float boxW = (w - static_cast<float>(kPerRow - 1u) * kItemGap) / static_cast<float>(kPerRow);

        for (std::size_t i = 0; i < kSelectorCount; ++i)
        {
            const std::size_t row = i / kPerRow;
            const std::size_t col = i % kPerRow;
            const Rect rect {x + static_cast<float>(col) * (boxW + kItemGap),
                             y + static_cast<float>(row) * (kSelectorH + kRowGap), boxW, kSelectorH};
            selectorRects_[selectorCount_++] = rect;
            drawSelector(rect, kSelectors[i]);
        }

        const std::size_t rows = (kSelectorCount + kPerRow - 1u) / kPerRow;
        return y + static_cast<float>(rows) * (kSelectorH + kRowGap);
    }

    void drawSliderLanes(const float x, float y, const float w, const float bottom)
    {
        for (std::size_t lane = 0; lane < 6; ++lane)
        {
            std::size_t inLane = 0;
            for (const SliderDef& slider : kSliders)
            {
                if (slider.lane == lane)
                    ++inLane;
            }
            if (inLane == 0u)
                continue;

            const std::size_t rows = (inLane + kPerSliderRow - 1u) / kPerSliderRow;
            const float laneHeight = kLaneLabelH + static_cast<float>(rows) * (kSliderH + kRowGap);

            // Drop a lane rather than let it overflow into the button strip.
            if (y + laneHeight > bottom)
                break;

            fc(t().textDim);
            fontSize(9.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            text(x, y, kLaneLabels[lane], nullptr);
            y += kLaneLabelH;

            const float sliderW =
                (w - static_cast<float>(kPerSliderRow - 1u) * kItemGap) / static_cast<float>(kPerSliderRow);

            std::size_t placed = 0;
            for (const SliderDef& slider : kSliders)
            {
                if (slider.lane != lane)
                    continue;

                const std::size_t row = placed / kPerSliderRow;
                const std::size_t col = placed % kPerSliderRow;
                const Rect rect {x + static_cast<float>(col) * (sliderW + kItemGap),
                                 y + static_cast<float>(row) * (kSliderH + kRowGap), sliderW, kSliderH};
                sliderRects_[sliderCount_++] = rect;
                drawSlider(rect, slider);
                ++placed;
            }

            y += static_cast<float>(rows) * (kSliderH + kRowGap) + kLaneGap;
        }
    }

    void drawSelector(const Rect& rect, const SelectorDef& def)
    {
        fc(t().textDim);
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(rect.x, rect.y, def.label, nullptr);

        beginPath();
        roundedRect(rect.x, rect.y + 12.0f, rect.w, rect.h - 12.0f, laf::kRadiusSmall);
        fc(t().surface);
        fill();
        sc(t().border);
        strokeWidth(laf::kBorderWidth);
        stroke();
        closePath();

        fc(t().textPrimary);
        fontSize(10.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rect.x + rect.w * 0.5f, rect.y + 12.0f + (rect.h - 12.0f) * 0.5f,
             formatValue(idx(def.parameter), values_[idx(def.parameter)]).c_str(), nullptr);
    }

    // Sliders are horizontal: the panel is narrow, so a lane of short horizontal
    // tracks fits far more controls legibly than vertical faders would.
    void drawSlider(const Rect& rect, const SliderDef& def)
    {
        const std::uint32_t parameter = idx(def.parameter);
        const float value = normalized(parameter, values_[parameter]);

        fc(t().textPrimary);
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(rect.x, rect.y, def.label, nullptr);

        const float trackY = rect.y + 12.0f;
        const float trackX = rect.x + 2.0f;
        const float trackW = rect.w - 4.0f;

        beginPath();
        roundedRect(trackX, trackY, trackW, 6.0f, 3.0f);
        fc(t().controlTrack);
        fill();
        closePath();

        beginPath();
        roundedRect(trackX, trackY, trackW * value, 6.0f, 3.0f);
        fc(t().controlFill);
        fill();
        closePath();

        const float knobX = trackX + trackW * value;
        beginPath();
        roundedRect(knobX - 3.0f, trackY - 3.0f, 6.0f, 12.0f, 2.0f);
        fc(t().controlKnob);
        fill();
        closePath();

        fc(t().textDim);
        fontSize(8.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        text(rect.x + rect.w * 0.5f, rect.y + 21.0f,
             formatValue(parameter, values_[parameter]).c_str(), nullptr);
    }

    void triggerParameter(const std::uint32_t parameter)
    {
        editParameter(parameter, true);
        setParameterValue(parameter, 1.0f);
        setParameterValue(parameter, 0.0f);
        editParameter(parameter, false);
        repaint();
    }

    void drawPanicButton(const Rect& rect)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, laf::kRadiusSmall);
        fc(t().buttonFace);
        fill();
        sc(t().danger);
        strokeWidth(laf::kBorderWidth);
        stroke();
        closePath();

        fc(t().danger);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, "Panic", nullptr);
    }

    void drawButton(const Rect& rect, const char* label, const std::uint32_t parameter)
    {
        const bool on = values_[parameter] >= 0.5f;

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, laf::kRadiusSmall);
        if (on)
            fc(t().buttonHot);
        else
            fc(t().buttonFace);
        fill();
        strokeColor(t().accent.r, t().accent.g, t().accent.b, on ? 245 : 150);
        strokeWidth(laf::kBorderWidth);
        stroke();
        closePath();

        fc(on ? t().bezel : t().textPrimary);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, label, nullptr);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlankUI)
};

UI* createUI() { return new PlankUI(); }

END_NAMESPACE_DISTRHO
