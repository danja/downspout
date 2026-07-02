#include "DistrhoUI.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamRootNote = 0,
    kParamScale,
    kParamGenre,
    kParamStyleMode,
    kParamChannel,
    kParamLengthBeats,
    kParamSubdivision,
    kParamDensity,
    kParamRegister,
    kParamHold,
    kParamAccent,
    kParamSeed,
    kParamVary,
    kParamActionNew,
    kParamActionNotes,
    kParamActionRhythm,
    kParamFollowDodge,
    kParamListenChannel,
    kParamListenNote,
    kParamColor,
    kParamInputMatchMode,
    kParamInputSensitivity,
    kParameterCount
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    [[nodiscard]] bool contains(float px, float py) const noexcept
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
};

struct ButtonDef {
    uint32_t index;
    const char* label;
};

constexpr SliderDef kSliders[] = {
    {kParamRootNote, "Root", 0.0f, 127.0f, true},
    {kParamLengthBeats, "Length", 8.0f, 32.0f, true},
    {kParamDensity, "Density", 0.0f, 1.0f, false},
    {kParamRegister, "Register", 0.0f, 3.0f, true},
    {kParamHold, "Hold", 0.0f, 1.0f, false},
    {kParamAccent, "Accent", 0.0f, 1.0f, false},
    {kParamColor, "Color", 0.0f, 1.0f, false},
    {kParamSeed, "Seed", 1.0f, 65535.0f, true},
    {kParamVary, "Vary", 0.0f, 100.0f, true},
    {kParamFollowDodge, "Follow/Dodge", -100.0f, 100.0f, true},
    {kParamInputSensitivity, "Input Sens", 0.0f, 100.0f, true},
    {kParamListenChannel, "Listen Ch", 1.0f, 16.0f, true},
    {kParamListenNote, "Listen Note", 0.0f, 127.0f, true},
};

constexpr const char* kScaleNames[] = {
    "Minor", "Major", "Dorian", "Phrygian", "Pent Minor", "Blues",
    "Mixolydian", "Harm Minor", "Pent Major", "Locrian", "Phryg Dom",
    "Lydian", "Mel Minor", "Whole Tone", "Altered", "Half-Whole Dim",
    "Whole-Half Dim", "Bebop Dom", "Bebop Major", "Bebop Minor"
};

constexpr const char* kGenreNames[] = {
    "Techno", "Acid", "House", "Electro", "Dub", "Ambient", "Funk", "Sabbath", "Jazz", "Fugue", "Rock"
};

constexpr const char* kStyleNames[] = {
    "Auto", "Straight", "Reel", "Waltz", "Jig", "Slip Jig"
};

constexpr const char* kSubdivisionNames[] = {
    "1/8", "1/16", "1/16T", "1/4"
};

constexpr const char* kChannelNames[] = {
    "1", "2", "3", "4", "5", "6", "7", "8",
    "9", "10", "11", "12", "13", "14", "15", "16"
};

constexpr const char* kInputMatchNames[] = {
    "Exact", "Channel", "Any"
};

constexpr SelectorDef kSelectors[] = {
    {kParamScale, "Scale", kScaleNames, 20},
    {kParamGenre, "Genre", kGenreNames, 11},
    {kParamStyleMode, "Style", kStyleNames, 6},
    {kParamSubdivision, "Subdivision", kSubdivisionNames, 4},
    {kParamChannel, "Channel", kChannelNames, 16},
    {kParamInputMatchMode, "Input", kInputMatchNames, 3},
};

constexpr ButtonDef kButtons[] = {
    {kParamActionNew, "New"},
    {kParamActionNotes, "Notes"},
    {kParamActionRhythm, "Rhythm"},
};

[[nodiscard]] float clampf(float value, float minValue, float maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] int clampi(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

[[nodiscard]] const char* noteName(int value)
{
    static constexpr const char* kNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[(value % 12 + 12) % 12];
}

[[nodiscard]] std::string formatSliderValue(uint32_t index, float value)
{
    char buf[64];
    switch (index) {
    case kParamRootNote: {
        const int midi = static_cast<int>(std::lround(value));
        const int octave = midi / 12 - 1;
        std::snprintf(buf, sizeof(buf), "%s%d (%d)", noteName(midi), octave, midi);
        return buf;
    }
    case kParamListenNote: {
        const int midi = static_cast<int>(std::lround(value));
        const int octave = midi / 12 - 1;
        std::snprintf(buf, sizeof(buf), "%s%d", noteName(midi), octave);
        return buf;
    }
    case kParamFollowDodge:
        if (std::fabs(value) < 0.5f) {
            return "Off";
        }
        std::snprintf(buf, sizeof(buf), "%s %d", value > 0.0f ? "Follow" : "Dodge",
                      static_cast<int>(std::lround(std::fabs(value))));
        return buf;
    case kParamDensity:
    case kParamHold:
    case kParamAccent:
    case kParamColor:
    case kParamInputSensitivity:
        std::snprintf(buf, sizeof(buf), "%.2f", value);
        return buf;
    default:
        std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(std::lround(value)));
        return buf;
    }
}

}  // namespace

class BassgenUI : public UI
{
public:
    BassgenUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_.fill(0.0f);

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/ttf-dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif

        values_[kParamRootNote] = 36.0f;
        values_[kParamLengthBeats] = 16.0f;
        values_[kParamStyleMode] = 0.0f;
        values_[kParamSubdivision] = 1.0f;
        values_[kParamDensity] = 0.45f;
        values_[kParamRegister] = 1.0f;
        values_[kParamHold] = 0.35f;
        values_[kParamAccent] = 0.45f;
        values_[kParamColor] = 0.5f;
        values_[kParamSeed] = 1.0f;
        values_[kParamVary] = 0.0f;
        values_[kParamFollowDodge] = 0.0f;
        values_[kParamInputMatchMode] = 0.0f;
        values_[kParamInputSensitivity] = 100.0f;
        values_[kParamListenChannel] = 10.0f;
        values_[kParamListenNote] = 36.0f;
        values_[kParamActionNew] = 0.0f;
        values_[kParamActionNotes] = 0.0f;
        values_[kParamActionRhythm] = 0.0f;
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
        const float pad = 20.0f;
        const float headerH = 72.0f;
        const float selectorH = 50.0f;
        const float buttonH = 44.0f;

        drawBackground(width, height);

        drawHeader(pad, pad, width - pad * 2.0f, headerH);

        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.64f;
        const float rightW = width - leftW - pad * 3.0f;

        updateLayout(width, height);
        drawSliderPanel(pad, contentY, leftW, contentH);
        drawRightPanel(pad * 2.0f + leftW, contentY, rightW, contentH, selectorH, buttonH);

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
        updateLayout(static_cast<float>(getWidth()), static_cast<float>(getHeight()));

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
        updateLayout(static_cast<float>(getWidth()), static_cast<float>(getHeight()));

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

    static constexpr float kSelectorItemHeight = 24.0f;
    static constexpr int kSelectorMenuMaxRows = 12;

    void updateLayout(float width, float height)
    {
        const float pad = 20.0f;
        const float headerH = 72.0f;
        const float selectorH = 50.0f;
        const float buttonH = 44.0f;
        const float contentY = pad + headerH + 18.0f;
        const float contentH = height - contentY - pad;
        const float leftW = width * 0.64f;
        const float rightX = pad * 2.0f + leftW;
        const float rightW = width - leftW - pad * 3.0f;

        const float innerX = pad + 20.0f;
        const float innerY = contentY + 52.0f;
        const float innerW = leftW - 40.0f;
        const float rowGap = 10.0f;
        const float rowH = 40.0f;
        const float colGap = 16.0f;
        const float colW = (innerW - colGap) * 0.5f;

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const float rx = innerX + static_cast<float>(col) * (colW + colGap);
            const float ry = innerY + static_cast<float>(row) * (rowH + rowGap);
            sliderRects_[i] = {rx, ry + 18.0f, colW, 22.0f};
        }

        const float panelBottom = contentY + contentH - 20.0f;
        float cy = contentY + 54.0f;
        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            selectorRects_[i] = {rightX + 20.0f, cy, rightW - 40.0f, selectorH};
            cy += selectorH + 8.0f;
        }

        cy += 28.0f;
        const float buttonGap = 8.0f;
        const float buttonW = (rightW - 40.0f - buttonGap * (static_cast<float>(std::size(kButtons)) - 1.0f))
            / static_cast<float>(std::size(kButtons));
        const float drawButtonH = std::min(buttonH, std::max(34.0f, panelBottom - cy));
        for (std::size_t i = 0; i < std::size(kButtons); ++i) {
            buttonRects_[i] = {rightX + 20.0f + static_cast<float>(i) * (buttonW + buttonGap), cy, buttonW, drawButtonH};
        }
    }

    void drawBackground(float width, float height)
    {
        beginPath();
        fillColor(17, 21, 27, 255);
        rect(0.0f, 0.0f, width, height);
        fill();
        closePath();

        beginPath();
        fillColor(27, 35, 45, 255);
        rect(0.0f, 0.0f, width, height * 0.28f);
        fill();
        closePath();
    }

    void drawHeader(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(31, 42, 55, 230);
        fill();
        closePath();

        fontSize(28.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(238, 241, 244, 255);
        text(x + 20.0f, y + 16.0f, "BassGen", nullptr);

        fontSize(13.0f);
        fillColor(154, 169, 183, 255);
        text(x + 22.0f, y + 48.0f, "Transport-synced bassline generator", nullptr);

        drawPill(x + w - 208.0f, y + 16.0f, 188.0f, 30.0f, "DPF VST3 port", 103, 185, 134);
    }

    void drawPill(float x, float y, float w, float h, const char* label, int r, int g, int b)
    {
        beginPath();
        roundedRect(x, y, w, h, h * 0.5f);
        fillColor(r, g, b, 36);
        fill();
        strokeColor(r, g, b, 180);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(13.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(r, g, b, 255);
        text(x + w * 0.5f, y + h * 0.5f + 1.0f, label, nullptr);
    }

    void drawSliderPanel(float x, float y, float w, float h)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(24, 29, 37, 250);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(x + 20.0f, y + 18.0f, "Shape", nullptr);

        const float innerX = x + 20.0f;
        const float innerY = y + 52.0f;
        const float innerW = w - 40.0f;
        const float rowGap = 10.0f;
        const float rowH = 40.0f;
        const float colGap = 16.0f;
        const float colW = (innerW - colGap) * 0.5f;

        for (std::size_t i = 0; i < std::size(kSliders); ++i) {
            const int col = static_cast<int>(i % 2);
            const int row = static_cast<int>(i / 2);
            const float rx = innerX + col * (colW + colGap);
            const float ry = innerY + row * (rowH + rowGap);
            sliderRects_[i] = {rx, ry + 18.0f, colW, 22.0f};
            drawSlider(kSliders[i], sliderRects_[i], values_[kSliders[i].index], draggingSlider_ == static_cast<int>(i));
        }
    }

    void drawSlider(const SliderDef& def, const Rect& rect, float value, bool active)
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
        fillColor(active ? 225 : 195, active ? 123 : 90, 73, 255);
        fill();
        closePath();
    }

    void drawRightPanel(float x, float y, float w, float h, float selectorH, float buttonH)
    {
        beginPath();
        roundedRect(x, y, w, h, 18.0f);
        fillColor(24, 29, 37, 250);
        fill();
        closePath();

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(x + 20.0f, y + 18.0f, "Routing", nullptr);

        const float panelBottom = y + h - 20.0f;
        float cy = y + 54.0f;
        for (std::size_t i = 0; i < std::size(kSelectors); ++i) {
            selectorRects_[i] = {x + 20.0f, cy, w - 40.0f, selectorH};
            drawSelector(kSelectors[i], selectorRects_[i], static_cast<int>(std::lround(values_[kSelectors[i].index])));
            cy += selectorH + 8.0f;
        }

        fontSize(15.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fillColor(224, 228, 232, 255);
        text(x + 20.0f, cy + 4.0f, "Actions", nullptr);
        cy += 28.0f;

        const float buttonGap = 8.0f;
        const float buttonW = (w - 40.0f - buttonGap * (static_cast<float>(std::size(kButtons)) - 1.0f))
            / static_cast<float>(std::size(kButtons));
        const float drawButtonH = std::min(buttonH, std::max(34.0f, panelBottom - cy));
        for (std::size_t i = 0; i < std::size(kButtons); ++i) {
            buttonRects_[i] = {x + 20.0f + static_cast<float>(i) * (buttonW + buttonGap), cy, buttonW, drawButtonH};
            drawButton(kButtons[i], buttonRects_[i]);
        }
    }

    void drawSelector(const SelectorDef& def, const Rect& rect, int value)
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

        fontSize(19.0f);
        fillColor(235, 239, 242, 255);
        text(rect.x + 16.0f, rect.y + 33.0f, def.items[clampi(value, 0, def.count - 1)], nullptr);

        fontSize(20.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fillColor(117, 133, 149, 255);
        text(rect.x + rect.w - 18.0f, rect.y + rect.h * 0.5f + 1.0f, openSelector_ == static_cast<int>(&def - kSelectors) ? "˄" : "˅", nullptr);
    }

    void drawButton(const ButtonDef& def, const Rect& rect)
    {
        beginPath();
        roundedRect(rect.x, rect.y, rect.w, rect.h, 16.0f);
        fillColor(76, 96, 120, 255);
        fill();
        closePath();

        beginPath();
        roundedRect(rect.x + 1.0f, rect.y + 1.0f, rect.w - 2.0f, rect.h - 2.0f, 15.0f);
        strokeColor(165, 186, 209, 110);
        strokeWidth(1.0f);
        stroke();
        closePath();

        fontSize(rect.w < 76.0f ? 14.0f : 17.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(240, 244, 247, 255);
        text(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, def.label, nullptr);
    }

    void updateSliderFromPosition(int sliderIndex, float mouseX)
    {
        const SliderDef& def = kSliders[sliderIndex];
        const Rect& rect = sliderRects_[sliderIndex];
        const float t = clampf((mouseX - rect.x) / rect.w, 0.0f, 1.0f);
        float value = def.min + t * (def.max - def.min);
        if (def.integer) {
            value = std::round(value);
        }
        setParameter(def.index, value);
    }

    void nudgeSlider(int sliderIndex, float direction)
    {
        const SliderDef& def = kSliders[sliderIndex];
        float step = def.integer ? 1.0f : (def.max - def.min) / 100.0f;
        setParameter(def.index, values_[def.index] + direction * step);
    }

    void cycleSelector(int selectorIndex, int direction)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        int value = clampi(static_cast<int>(std::lround(values_[def.index])) + direction, 0, def.count - 1);
        setParameter(def.index, static_cast<float>(value));
    }

    void drawOpenSelectorMenu(int selectorIndex)
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const int selected = clampi(static_cast<int>(std::lround(values_[def.index])), 0, def.count - 1);
        const Rect menuRect = selectorMenuRect(selectorIndex);
        const int columns = selectorMenuColumnCount(def);
        const int rows = selectorMenuRowCount(def);
        const float itemW = menuRect.w / static_cast<float>(columns);

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

            fontSize(columns > 1 ? 11.0f : 12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(236, 240, 243, 255);
            text(itemX + 14.0f, rowY + kSelectorItemHeight * 0.5f + 1.0f, def.items[i], nullptr);
        }
    }

    bool handleOpenSelectorClick(float x, float y)
    {
        const Rect& base = selectorRects_[openSelector_];
        if (base.contains(x, y)) {
            openSelector_ = -1;
            repaint();
            return true;
        }

        const SelectorDef& def = kSelectors[openSelector_];
        const Rect menuRect = selectorMenuRect(openSelector_);
        if (!menuRect.contains(x, y)) {
            return false;
        }

        const int columns = selectorMenuColumnCount(def);
        const int rows = selectorMenuRowCount(def);
        const float itemW = menuRect.w / static_cast<float>(columns);
        const int col = clampi(static_cast<int>((x - menuRect.x) / itemW), 0, columns - 1);
        const int row = clampi(static_cast<int>((y - menuRect.y) / kSelectorItemHeight), 0, rows - 1);
        const int item = col * rows + row;
        if (item >= def.count) {
            return true;
        }

        setParameter(def.index, static_cast<float>(item));
        openSelector_ = -1;
        repaint();
        return true;
    }

    [[nodiscard]] int selectorMenuColumnCount(const SelectorDef& def) const
    {
        return std::max(1, (def.count + kSelectorMenuMaxRows - 1) / kSelectorMenuMaxRows);
    }

    [[nodiscard]] int selectorMenuRowCount(const SelectorDef& def) const
    {
        const int columns = selectorMenuColumnCount(def);
        return (def.count + columns - 1) / columns;
    }

    [[nodiscard]] Rect selectorMenuRect(int selectorIndex) const
    {
        const SelectorDef& def = kSelectors[selectorIndex];
        const Rect& base = selectorRects_[selectorIndex];
        const int columns = selectorMenuColumnCount(def);
        const int rows = selectorMenuRowCount(def);
        const float itemW = std::max(base.w, 158.0f);
        const float menuW = itemW * static_cast<float>(columns);
        const float menuH = static_cast<float>(rows) * kSelectorItemHeight;
        const float windowW = static_cast<float>(getWidth());
        const float windowH = static_cast<float>(getHeight());
        const float margin = 14.0f;

        float menuX = clampf(base.x, margin, std::max(margin, windowW - margin - menuW));
        float menuY = base.y + base.h + 6.0f;
        if (menuY + menuH > windowH - margin) {
            menuY = base.y - menuH - 6.0f;
        }
        menuY = clampf(menuY, margin, std::max(margin, windowH - margin - menuH));
        return {menuX, menuY, menuW, menuH};
    }

    void triggerButton(int buttonIndex)
    {
        const uint32_t index = kButtons[buttonIndex].index;
        float value = values_[index] + 1.0f;
        if (value > 1048576.0f) {
            value = 1.0f;
        }
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    void setParameter(uint32_t index, float value)
    {
        editParameter(index, true);
        setParameterValue(index, value);
        editParameter(index, false);
        values_[index] = value;
        repaint();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassgenUI)
};

UI* createUI()
{
    return new BassgenUI();
}

END_NAMESPACE_DISTRHO
