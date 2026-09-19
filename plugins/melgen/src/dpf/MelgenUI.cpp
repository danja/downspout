#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

enum ParameterIndex : uint32_t {
    kParamRootNote = 0,
    kParamScale,
    kParamChannel,
    kParamLengthBeats,
    kParamPhraseLength,
    kParamSubdivision,
    kParamPeriod,
    kParamContour,
    kParamAnswer,
    kParamDensity,
    kParamRegister,
    kParamHold,
    kParamAccent,
    kParamStructure,
    kParamRange,
    kParamLeap,
    kParamRest,
    kParamCadence,
    kParamSeed,
    kParamVary,
    kParamActionNew,
    kParamActionNotes,
    kParamActionRhythm,
    kParamFollow,
    kParamColor,
    kParamConductorChannel,
    kParameterCount
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept
    {
        return px >= x && px <= x + w && py >= y && py <= y + h;
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
};

struct ButtonDef {
    uint32_t index;
    const char* label;
};

struct SliderGroup {
    const char* title;
    std::size_t first;
    std::size_t count;
};

struct Accent {
    int r;
    int g;
    int b;
};

// Two section accents, mirroring how plugins/magneto colour-codes each panel
// header rather than leaving every panel the same neutral surface.
constexpr Accent kLineAccent {94, 158, 143};        // cold teal — line controls
constexpr Accent kStructureAccent {176, 122, 62};   // warm amber — phrase structure

// Above this item count, a dropdown menu is split into two columns so it
// never has to render as a single column taller than the plugin window.
constexpr int kMenuTwoColumnThreshold = 12;

constexpr const char* kScaleNames[] = {
    "Major", "Ionian", "Minor", "Harm Minor", "Mel Minor",
    "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
    "Phryg Dom", "Neo Major", "Neo Minor",
    "Pent Major", "Pent Minor", "Blues",
    "Whole Tone", "Altered", "Half-Whole Dim",
    "Whole-Half Dim", "Bebop Dom", "Bebop Major", "Bebop Minor"
};

constexpr const char* kSubdivisionNames[] = {"1/8", "1/16", "1/16T", "1/4"};
constexpr const char* kPeriodNames[] = {"Free", "A A", "A B", "A A'", "Call Answer", "A B A"};
constexpr const char* kContourNames[] = {"Wander", "Flat", "Rise", "Fall", "Arch", "Inv Arch"};
constexpr const char* kAnswerNames[] = {"Related", "Same", "Transpose", "Invert", "Compress", "Expand"};
constexpr const char* kChannelNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr const char* kCondChNames[] = {
    "Off", "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr std::array<SliderDef, 16> kSliders = {{
    {kParamRootNote, "Root", 0.0f, 127.0f, true},
    {kParamRegister, "Register", 0.0f, 4.0f, true},
    {kParamRange, "Range", 0.0f, 1.0f, false},
    {kParamLeap, "Leap", 0.0f, 1.0f, false},
    {kParamColor, "Color", 0.0f, 1.0f, false},
    {kParamHold, "Hold", 0.0f, 1.0f, false},
    {kParamAccent, "Accent", 0.0f, 1.0f, false},
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamRest, "Rest", 0.0f, 1.0f, false},
    {kParamStructure, "Structure", 0.0f, 1.0f, false},
    {kParamCadence, "Cadence", 0.0f, 1.0f, false},
    {kParamFollow, "Follow", 0.0f, 1.0f, false},
    {kParamVary, "Vary", 0.0f, 100.0f, true},
    {kParamLengthBeats, "Length", 4.0f, 64.0f, true},
    {kParamPhraseLength, "Phrase", 1.0f, 8.0f, true},
    {kParamSeed, "Seed", 1.0f, 65535.0f, true},
}};

constexpr std::array<SliderGroup, 3> kSliderGroups = {{
    {"Pitch", 0, 7},
    {"Phrase", 7, 6},
    {"Pattern", 13, 3},
}};

constexpr std::array<SelectorDef, 7> kSelectors = {{
    {kParamScale, "Scale", kScaleNames, 23},
    {kParamPeriod, "Period", kPeriodNames, 6},
    {kParamContour, "Contour", kContourNames, 6},
    {kParamAnswer, "Answer", kAnswerNames, 6},
    {kParamSubdivision, "Grid", kSubdivisionNames, 4},
    {kParamChannel, "Channel", kChannelNames, 16},
    {kParamConductorChannel, "Conductor ch", kCondChNames, 17},
}};

constexpr std::array<ButtonDef, 3> kButtons = {{
    {kParamActionNew, "New"},
    {kParamActionNotes, "Notes"},
    {kParamActionRhythm, "Rhythm"},
}};

[[nodiscard]] float clampf(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const char* noteName(int value)
{
    static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[(value % 12 + 12) % 12];
}

[[nodiscard]] std::string formatValue(const SliderDef& def, float value)
{
    char buf[64];
    if (def.index == kParamRootNote) {
        const int midi = static_cast<int>(std::lround(value));
        std::snprintf(buf, sizeof(buf), "%s%d (%d)", noteName(midi), midi / 12 - 1, midi);
    } else if (def.integer) {
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
    } else {
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(std::lround(value * 100.0f)));
    }
    return buf;
}

}  // namespace

class MelgenUI : public UI
{
public:
    MelgenUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);
        values_[kParamRootNote] = 60.0f;
        values_[kParamScale] = 1.0f;
        values_[kParamChannel] = 1.0f;
        values_[kParamLengthBeats] = 16.0f;
        values_[kParamPhraseLength] = 2.0f;
        values_[kParamSubdivision] = 1.0f;
        values_[kParamPeriod] = 4.0f;
        values_[kParamContour] = 4.0f;
        values_[kParamDensity] = 0.48f;
        values_[kParamRegister] = 1.0f;
        values_[kParamHold] = 0.42f;
        values_[kParamAccent] = 0.45f;
        values_[kParamStructure] = 0.62f;
        values_[kParamFollow] = 0.0f;
        values_[kParamColor] = 0.5f;
        values_[kParamRange] = 0.45f;
        values_[kParamLeap] = 0.28f;
        values_[kParamRest] = 0.24f;
        values_[kParamCadence] = 0.55f;
        values_[kParamSeed] = 1.0f;
        values_[kParamVary] = 0.0f;
        values_[kParamConductorChannel] = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
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
        drawBackground(width, height);
        drawHeader(pad, pad, width - pad * 2.0f, 78.0f);
        drawSliders(pad, 118.0f, width * 0.68f - pad * 1.5f, height - 140.0f);
        drawStructurePanel(width * 0.68f + pad * 0.5f, 118.0f, width * 0.32f - pad * 1.5f, height - 140.0f);
        if (openSelector_ >= 0) {
            drawSelectorMenu(openSelector_);
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

        if (themeRect_.contains(x, y)) {
            darkTheme_ = !darkTheme_;
            repaint();
            return true;
        }

        if (openSelector_ >= 0) {
            if (handleSelectorMenu(x, y)) {
                return true;
            }
            openSelector_ = -1;
        }

        for (std::size_t i = 0; i < sliderRects_.size(); ++i) {
            if (sliderRects_[i].contains(x, y)) {
                draggingSlider_ = static_cast<int>(i);
                updateSlider(static_cast<int>(i), x, y);
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
            updateSlider(draggingSlider_, static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] const laf::Theme& theme() const { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    std::array<float, kParameterCount> values_ {};
    std::array<Rect, kSliders.size()> sliderRects_ {};
    std::array<Rect, kSelectors.size()> selectorRects_ {};
    std::array<Rect, kButtons.size()> buttonRects_ {};
    std::array<Rect, kSelectors.size()> menuRects_ {};
    Rect themeRect_ {};
    int draggingSlider_ = -1;
    int openSelector_ = -1;
    bool darkTheme_ = true;

    void drawBackground(float width, float height)
    {
        const auto& t = theme();
        beginPath();
        fc(t.background);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fc(t.panel);
        rect(0.0f, 0.0f, width, 112.0f);
        fill();
        closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fc(t.panel.withAlpha(244));
        fill();
        closePath();

        fontSize(32.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textPrimary);
        text(x + 22.0f, y + 18.0f, "MelGen", nullptr);

        fontSize(13.0f);
        fc(t.textDim);
        text(x + 176.0f, y + 30.0f, "phrase-aware MIDI melody generator with period structure", nullptr);

        themeRect_ = {x + w - 74.0f, y + 4.0f, 74.0f, 26.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, laf::kRadiusSmall);
        fill();
        closePath();
        fc(t.textDim);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f,
             darkTheme_ ? "DARK" : "LIGHT", nullptr);
    }

    void drawPanel(float x, float y, float w, float h, const char* title, const Accent& accent)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(x, y, w, h, 7.0f);
        fc(t.surface);
        fill();
        sc(t.border);
        strokeWidth(1.0f);
        stroke();
        closePath();

        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        roundedRect(x, y, w, 32.0f, 7.0f);
        fill();
        closePath();
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        rect(x, y + 20.0f, w, 12.0f);
        fill();
        closePath();

        fillColor(250, 248, 242, 255);
        fontSize(14.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 14.0f, y + 16.0f, title, nullptr);
    }

    void drawSliders(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "LINE CONTROLS", kLineAccent);

        const float innerX = x + 18.0f;
        const float innerW = w - 36.0f;
        const float topY = y + 54.0f;
        const float rowGap = 28.0f;
        const float rowH = (h - 86.0f - rowGap) * 0.5f;
        drawSliderGroup(kSliderGroups[0], innerX, topY, innerW, rowH);
        drawSliderGroup(kSliderGroups[1], innerX, topY + rowH + rowGap, innerW * 0.73f, rowH);
        drawSliderGroup(kSliderGroups[2], innerX + innerW * 0.76f, topY + rowH + rowGap, innerW * 0.24f, rowH);
    }

    void drawSliderGroup(const SliderGroup& group, float x, float y, float w, float h)
    {
        const auto& t = theme();
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDim);
        text(x, y, group.title, nullptr);

        const float gap = 10.0f;
        const float cellW = (w - gap * static_cast<float>(group.count - 1)) / static_cast<float>(group.count);
        const float cellY = y + 22.0f;
        const float cellH = h - 22.0f;
        for (std::size_t offset = 0; offset < group.count; ++offset) {
            const std::size_t sliderIndex = group.first + offset;
            const Rect rect {x + static_cast<float>(offset) * (cellW + gap), cellY, cellW, cellH};
            sliderRects_[sliderIndex] = rect;
            drawSlider(kSliders[sliderIndex], rect, kLineAccent);
        }
    }

    void drawStructurePanel(float x, float y, float w, float h)
    {
        drawPanel(x, y, w, h, "PHRASE STRUCTURE", kStructureAccent);

        float rowY = y + 62.0f;
        for (std::size_t i = 0; i < kSelectors.size(); ++i) {
            const Rect rect {x + 18.0f, rowY, w - 36.0f, 40.0f};
            selectorRects_[i] = rect;
            drawSelector(kSelectors[i], rect);
            rowY += 56.0f;
        }

        rowY += 18.0f;
        const float buttonW = (w - 36.0f - 16.0f) / 3.0f;
        for (std::size_t i = 0; i < kButtons.size(); ++i) {
            const Rect rect {x + 18.0f + static_cast<float>(i) * (buttonW + 8.0f), rowY, buttonW, 42.0f};
            buttonRects_[i] = rect;
            drawButton(kButtons[i], rect);
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, const Accent& accent)
    {
        const auto& t = theme();
        const float value = values_[def.index];
        const float norm = clampf((value - def.min) / (def.max - def.min), 0.0f, 1.0f);
        const std::string textValue = formatValue(def, value);

        fontSize(12.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fc(t.textPrimary);
        text(rect.x + rect.w * 0.5f, rect.y, def.label, nullptr);

        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fillColor(accent.r, accent.g, accent.b, 255);
        text(rect.x + rect.w * 0.5f, rect.y + 17.0f, textValue.c_str(), nullptr);

        const float trackW = 12.0f;
        const float trackX = rect.x + rect.w * 0.5f - trackW * 0.5f;
        const float trackY = rect.y + 42.0f;
        const float trackH = std::max(36.0f, rect.h - 52.0f);
        const float fillH = std::max(8.0f, trackH * norm);
        const float fillY = trackY + trackH - fillH;
        const float knobY = trackY + trackH * (1.0f - norm);

        beginPath();
        roundedRect(trackX, trackY, trackW, trackH, 6.0f);
        fc(t.controlTrack);
        fill();
        closePath();

        beginPath();
        roundedRect(trackX, fillY, trackW, fillH, 6.0f);
        fillColor(accent.r, accent.g, accent.b, 235);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 10.0f, knobY - 5.0f, rect.w - 20.0f, 10.0f, 5.0f);
        fc(t.textPrimary);
        fill();
        closePath();
    }

    void drawSelector(const SelectorDef& def, const Rect& rect)
    {
        const auto& t = theme();
        const int item = std::max(0, std::min(static_cast<int>(std::lround(values_[def.index])), def.count - 1));
        const bool open = openSelector_ >= 0 && kSelectors[openSelector_].index == def.index;

        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, laf::kRadiusSmall);
        fc(t.surface);
        fill();
        strokeColor(kStructureAccent.r, kStructureAccent.g, kStructureAccent.b, open ? 220 : 140);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(rect.x + 12.0f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);

        fontSize(13.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(rect.x + 112.0f, rect.y + rect.h * 0.5f + 1.0f, def.items[item], nullptr);
    }

    void drawButton(const ButtonDef& def, const Rect& rect)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, laf::kRadiusSmall);
        fc(t.buttonFace);
        fill();
        sc(t.border);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f + 1.0f, def.label, nullptr);
    }

    struct MenuLayout {
        Rect bounds;
        int columns;
        int rows;
        float itemH;
    };

    // A single-column menu tall enough to fall off both the bottom and the
    // top of the window (the 23-item Scale list did this) is split into two
    // columns instead, so it always fits within the plugin window.
    [[nodiscard]] MenuLayout computeMenuLayout(int selectorIndex) const
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const Rect& base = selectorRects_[selectorIndex];
        const float itemH = 26.0f;
        const float winH = static_cast<float>(getHeight());

        int columns = 1;
        int rows = def.count;
        float menuH = static_cast<float>(rows) * itemH;
        const bool fitsBelow = base.y + base.h + 4.0f + menuH <= winH;
        const bool fitsAbove = base.y - menuH - 4.0f >= 0.0f;

        if (!fitsBelow && !fitsAbove && def.count > kMenuTwoColumnThreshold) {
            columns = 2;
            rows = (def.count + 1) / 2;
            menuH = static_cast<float>(rows) * itemH;
        }

        const float menuY = (base.y + base.h + 4.0f + menuH > winH)
                            ? std::max(4.0f, base.y - menuH - 4.0f)
                            : base.y + base.h + 4.0f;
        return {{base.x, menuY, base.w, menuH}, columns, rows, itemH};
    }

    void drawSelectorMenu(int selectorIndex)
    {
        if (selectorIndex < 0 || selectorIndex >= static_cast<int>(kSelectors.size())) {
            return;
        }

        const auto& t = theme();
        const SelectorDef& def = kSelectors[selectorIndex];
        const MenuLayout layout = computeMenuLayout(selectorIndex);
        const Rect& menu = layout.bounds;
        menuRects_[selectorIndex] = menu;

        beginPath();
        roundedRect(menu.x, menu.y, menu.w, menu.h, 7.0f);
        fc(t.panel);
        fill();
        strokeColor(kStructureAccent.r, kStructureAccent.g, kStructureAccent.b, 255);
        strokeWidth(1.0f);
        stroke();
        closePath();

        const float colW = menu.w / static_cast<float>(layout.columns);
        const int selected = static_cast<int>(std::lround(values_[def.index]));
        for (int i = 0; i < def.count; ++i) {
            const int col = i / layout.rows;
            const int row = i % layout.rows;
            const float itemX = menu.x + static_cast<float>(col) * colW;
            const float rowY = menu.y + static_cast<float>(row) * layout.itemH;
            if (i == selected) {
                beginPath();
                rect(itemX + 2.0f, rowY + 2.0f, colW - 4.0f, layout.itemH - 4.0f);
                fc(t.selection);
                fill();
                closePath();
            }
            fontSize(layout.columns > 1 ? 11.0f : 12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fc(t.textPrimary);
            text(itemX + 9.0f, rowY + layout.itemH * 0.5f + 1.0f, def.items[i], nullptr);
        }

        if (layout.columns > 1) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(menu.x + colW, menu.y + 2.0f);
            lineTo(menu.x + colW, menu.y + menu.h - 2.0f);
            stroke();
        }
    }

    bool handleSelectorMenu(float x, float y)
    {
        if (openSelector_ < 0 || openSelector_ >= static_cast<int>(kSelectors.size())) {
            return false;
        }
        const SelectorDef& def = kSelectors[openSelector_];
        const MenuLayout layout = computeMenuLayout(openSelector_);
        const Rect& menu = layout.bounds;
        if (!menu.contains(x, y)) {
            return false;
        }

        const float colW = menu.w / static_cast<float>(layout.columns);
        const int col = std::max(0, std::min(layout.columns - 1, static_cast<int>((x - menu.x) / colW)));
        const int row = std::max(0, std::min(layout.rows - 1, static_cast<int>((y - menu.y) / layout.itemH)));
        const int item = col * layout.rows + row;
        openSelector_ = -1;
        if (item < def.count) {
            commit(def.index, static_cast<float>(item));
        } else {
            repaint();
        }
        return true;
    }

    void updateSlider(int sliderIndex, float, float y)
    {
        if (sliderIndex < 0 || sliderIndex >= static_cast<int>(kSliders.size())) {
            return;
        }
        const SliderDef& def = kSliders[sliderIndex];
        const Rect& rect = sliderRects_[sliderIndex];
        const float trackY = rect.y + 42.0f;
        const float trackH = std::max(36.0f, rect.h - 52.0f);
        const float norm = clampf(1.0f - ((y - trackY) / trackH), 0.0f, 1.0f);
        float value = def.min + norm * (def.max - def.min);
        if (def.integer) {
            value = std::round(value);
        }
        commit(def.index, value);
    }

    void triggerButton(int buttonIndex)
    {
        if (buttonIndex < 0 || buttonIndex >= static_cast<int>(kButtons.size())) {
            return;
        }
        const uint32_t index = kButtons[buttonIndex].index;
        editParameter(index, true);
        setParameterValue(index, 1.0f);
        setParameterValue(index, 0.0f);
        editParameter(index, false);
    }

    void commit(uint32_t index, float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MelgenUI)
};

UI* createUI()
{
    return new MelgenUI();
}

END_NAMESPACE_DISTRHO
