#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

// Must match GhostPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamSensitivity = 0,
    kParamDensity,
    kParamVelocity,
    kParamDrag,
    kParamMode,
    kParamChannel,
    kParamBaseNote,
    kParamPassInput,
    kParamSeed,
    kParamCCSensitivity,
    kParamCCDensity,
    kParamCCVelocity,
    kParamCCDrag,
    kParamCCChannel,
    kParamAudioThru,
    kParameterCount
};

struct Rect {
    float x, y, w, h;
    [[nodiscard]] bool contains(float px, float py) const noexcept {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

struct SliderDef {
    uint32_t    index;
    const char* label;
    float       min, max;
    bool        integer;
    const char* stateKey;
};

constexpr std::array<SliderDef, 5> kMainSliders = {{
    { kParamSensitivity, "Sensitivity", 0.0f,   1.0f,   false, "sensitivity" },
    { kParamDensity,     "Density",     0.0f,   1.0f,   false, "density"     },
    { kParamVelocity,    "Velocity",    1.0f,   127.0f, true,  "velocity"    },
    { kParamDrag,        "Drag",        0.0f,   1.0f,   false, "drag"        },
    { kParamSeed,        "Seed",        1.0f,   65535.0f, true,"seed"        },
}};

enum class DropKind : std::uint8_t {
    Channel,   // 1-16
    BaseNote,  // 0-127 with note names
    CcNumber,  // off + 1-127 with common names
};

struct DropDef {
    uint32_t    index;
    const char* label;
    const char* stateKey;
    DropKind    kind;
};

constexpr std::array<DropDef, 2> kMainDrops = {{
    { kParamChannel,  "Channel",   "channel",   DropKind::Channel  },
    { kParamBaseNote, "Base Note", "base_note", DropKind::BaseNote },
}};

constexpr std::array<DropDef, 5> kCCDropDefs = {{
    { kParamCCSensitivity, "CC Sensitivity", "cc_sensitivity", DropKind::CcNumber },
    { kParamCCDensity,     "CC Density",     "cc_density",     DropKind::CcNumber },
    { kParamCCVelocity,    "CC Velocity",    "cc_velocity",    DropKind::CcNumber },
    { kParamCCDrag,        "CC Drag",        "cc_drag",        DropKind::CcNumber },
    { kParamCCChannel,     "CC Channel",     "cc_channel",     DropKind::Channel  },
}};

constexpr const char* kModeChoices[2] = { "Drums 10", "Notes" };

[[nodiscard]] float clampf(float v, float lo, float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] const char* ccCommonName(int cc) noexcept
{
    switch (cc) {
    case 1: return "Mod";
    case 2: return "Breath";
    case 7: return "Volume";
    case 10: return "Pan";
    case 11: return "Expr";
    case 64: return "Sustain";
    case 71: return "Reson";
    case 74: return "Bright";
    default: return nullptr;
    }
}

[[nodiscard]] std::string formatSlider(const SliderDef& def, float v)
{
    char buf[40];
    switch (def.index) {
    case kParamSensitivity:
    case kParamDensity:
    case kParamDrag:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.0f", v);
        break;
    }
    return buf;
}

[[nodiscard]] int dropItemCount(DropKind kind) noexcept
{
    switch (kind) {
    case DropKind::Channel:  return 16;
    case DropKind::BaseNote: return 128;
    case DropKind::CcNumber: return 129;  // item 0 = off, items 1-127 = CC#
    }
    return 0;
}

void dropItemText(DropKind kind, int item, char* buf, std::size_t size)
{
    switch (kind) {
    case DropKind::Channel:
        std::snprintf(buf, size, "Ch %d", item + 1);
        break;
    case DropKind::BaseNote: {
        static constexpr const char* kNames[12] = {
            "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
        std::snprintf(buf, size, "%d %s%d", item, kNames[item % 12], item / 12 - 1);
        break;
    }
    case DropKind::CcNumber:
        if (item == 0) {
            std::snprintf(buf, size, "off");
        } else if (const char* common = ccCommonName(item)) {
            std::snprintf(buf, size, "%d %s", item, common);
        } else {
            std::snprintf(buf, size, "CC %d", item);
        }
        break;
    }
}

[[nodiscard]] int dropCurrentItem(DropKind kind, float value) noexcept
{
    switch (kind) {
    case DropKind::Channel:  return std::clamp(static_cast<int>(std::lround(value)) - 1, 0, 15);
    case DropKind::BaseNote: return std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    case DropKind::CcNumber: return std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    }
    return 0;
}

[[nodiscard]] float dropItemValue(DropKind kind, int item) noexcept
{
    switch (kind) {
    case DropKind::Channel:  return static_cast<float>(item + 1);
    case DropKind::BaseNote: return static_cast<float>(item);
    case DropKind::CcNumber: return static_cast<float>(item);  // item 0 = off
    }
    return 0.0f;
}

// Layout constants
struct Accent {
    int r;
    int g;
    int b;
};

// Section accents from the magneto palette: amber for parameters,
// cold steel for CC routing plumbing.
constexpr Accent kMainAccent {198, 132, 58};
constexpr Accent kCcAccent {92, 140, 156};

constexpr int kCreamR = 250;
constexpr int kCreamG = 248;
constexpr int kCreamB = 242;

constexpr float kPad      = 16.0f;
constexpr float kDivX     = 400.0f;
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kSlidersY = 76.0f;

constexpr float kSliderH = 52.0f;
constexpr float kSegH    = 48.0f;
constexpr float kDropH   = 48.0f;
constexpr float kSwitchH = 40.0f;

constexpr float kItemH       = 20.0f;
constexpr int   kMaxRows     = 10;  // scrollable single column beyond this
constexpr int   kTwoColLimit = 20;  // <= this many items: 2-column grid, no scroll

}  // namespace

class GhostUI : public UI
{
public:
    GhostUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamSensitivity] = 0.5f;
        values_[kParamDensity]     = 0.35f;
        values_[kParamVelocity]    = 90.0f;
        values_[kParamDrag]        = 0.15f;
        values_[kParamMode]        = 0.0f;
        values_[kParamChannel]     = 10.0f;
        values_[kParamBaseNote]    = 38.0f;
        values_[kParamPassInput]   = 1.0f;
        values_[kParamAudioThru]   = 0.0f;
        values_[kParamSeed]        = 7.0f;
        values_[kParamCCSensitivity] = 1.0f;
        values_[kParamCCDensity]     = 2.0f;
        values_[kParamCCVelocity]    = 3.0f;
        values_[kParamCCDrag]        = 4.0f;
        values_[kParamCCChannel]     = 1.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t, float) override {}  // state-driven; ignore

    void stateChanged(const char* key, const char* value) override
    {
        if (!value) return;
        const float fv = static_cast<float>(std::atof(value));
        if      (std::strcmp(key, "sensitivity")   == 0) { values_[kParamSensitivity] = fv; }
        else if (std::strcmp(key, "density")       == 0) { values_[kParamDensity] = fv; }
        else if (std::strcmp(key, "velocity")      == 0) { values_[kParamVelocity] = fv; }
        else if (std::strcmp(key, "drag")          == 0) { values_[kParamDrag] = fv; }
        else if (std::strcmp(key, "mode")          == 0) { values_[kParamMode] = fv; }
        else if (std::strcmp(key, "channel")       == 0) { values_[kParamChannel] = fv; }
        else if (std::strcmp(key, "base_note")     == 0) { values_[kParamBaseNote] = fv; }
        else if (std::strcmp(key, "pass_input")    == 0) { values_[kParamPassInput] = fv; }
        else if (std::strcmp(key, "audio_thru")    == 0) { values_[kParamAudioThru] = fv; }
        else if (std::strcmp(key, "seed")          == 0) { values_[kParamSeed] = fv; }
        else if (std::strcmp(key, "cc_sensitivity")== 0) { values_[kParamCCSensitivity] = fv; }
        else if (std::strcmp(key, "cc_density")    == 0) { values_[kParamCCDensity] = fv; }
        else if (std::strcmp(key, "cc_velocity")   == 0) { values_[kParamCCVelocity] = fv; }
        else if (std::strcmp(key, "cc_drag")       == 0) { values_[kParamCCDrag] = fv; }
        else if (std::strcmp(key, "cc_channel")    == 0) { values_[kParamCCChannel] = fv; }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        drawBackground(W, H);
        drawHeader(W);
        drawDivider(H);
        drawMainColumn();
        drawCCColumn(W);
        drawFooterNote();
        if (openIsCC_ >= 0 || openMain_ >= 0)
            drawPopup(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (!ev.press) {
            dragSlider_ = -1;
            return false;
        }

        if (themeRect_.contains(mx, my)) {
            darkTheme_ = !darkTheme_;
            closePopup();
            repaint();
            return true;
        }

        if (popupOpen()) {
            const Rect popup = popupRect(static_cast<float>(getWidth()),
                                         static_cast<float>(getHeight()));
            if (popup.contains(mx, my)) {
                const int item = popupItemAt(mx, my, popup);
                if (item >= 0)
                    commitDrop(item);
                closePopup();
                return true;
            }
            // Click outside: close, but let a fresh dropdown header press through.
            closePopup();
            if (dropHeaderAt(mx, my) == nullptr && segAt(mx, my) < 0 && switchAt(mx, my) < 0) {
                for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
                    if (mainTrack(s).contains(mx, my))
                        return true;  // swallow: no drag on the closing click
                }
                return false;
            }
        }

        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            if (mainTrack(s).contains(mx, my)) {
                dragSlider_ = s;
                updateSlider(s, mx);
                return true;
            }
        }
        if (segAt(mx, my) >= 0) {
            const Rect box = segBox();
            const float t = clampf((mx - box.x) / box.w, 0.0f, 0.9999f);
            commitParam(kParamMode, "mode", t < 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (switchAt(mx, my) == 0) {
            commitParam(kParamPassInput, "pass_input",
                        values_[kParamPassInput] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (switchAt(mx, my) == 1) {
            commitParam(kParamAudioThru, "audio_thru",
                        values_[kParamAudioThru] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (const DropDef* def = dropHeaderAt(mx, my)) {
            openDrop(def);
            return true;
        }
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (dragSlider_ >= 0) { updateSlider(dragSlider_, mx); return true; }
        if (popupOpen()) {
            const Rect popup = popupRect(static_cast<float>(getWidth()),
                                         static_cast<float>(getHeight()));
            const int item = popupItemAt(mx, my, popup);
            if (item != dropHover_) { dropHover_ = item; repaint(); }
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!popupOpen()) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        const Rect popup = popupRect(static_cast<float>(getWidth()),
                                     static_cast<float>(getHeight()));
        if (!popup.contains(mx, my)) return false;
        if (dropScrollable()) {
            const int count = dropItemCount(openKind());
            dropScroll_ = std::clamp(dropScroll_ - static_cast<int>(std::round(ev.delta.getY() * 3.0)),
                                     0, count - kMaxRows);
            repaint();
            return true;
        }
        return false;
    }

private:
    [[nodiscard]] const laf::Theme& theme() const noexcept { return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme; }
    bool darkTheme_ = false;  // magneto default: light test-equipment panel
    Rect themeRect_ {};
    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }

    std::array<float, kParameterCount> values_ {};
    int dragSlider_ = -1;

    // Open dropdown: exactly one of openMain_/openIsCC_ is >= 0.
    int openMain_ = -1;  // index into kMainDrops, or -1
    int openIsCC_ = -1;  // index into kCCDropDefs, or -1
    int dropScroll_ = 0;  // top visible row for scrollable popups
    int dropHover_ = -1;  // hovered item, or -1

    [[nodiscard]] bool popupOpen() const noexcept { return openMain_ >= 0 || openIsCC_ >= 0; }

    [[nodiscard]] const DropDef& openDef() const noexcept
    {
        return openIsCC_ >= 0 ? kCCDropDefs[static_cast<std::size_t>(openIsCC_)]
                              : kMainDrops[static_cast<std::size_t>(openMain_)];
    }

    [[nodiscard]] DropKind openKind() const noexcept { return openDef().kind; }

    [[nodiscard]] Rect mainBox() const noexcept
    {
        return { kMainX, kSlidersY, kDivX - kMainX - kPad, 0.0f };
    }

    [[nodiscard]] Rect ccBox() const noexcept
    {
        const float W = static_cast<float>(getWidth());
        return { kCCX, kSlidersY, W - kCCX - kPad, 0.0f };
    }

    // Left column row order: 4 sliders, Mode segment, 2 dropdowns,
    // Pass + Audio switches, Seed slider.
    [[nodiscard]] float mainRowY(int row) const noexcept
    {
        float y = kSlidersY;
        for (int r = 0; r < row; ++r) {
            if (r < 4) y += kSliderH;
            else if (r == 4) y += kSegH;
            else if (r <= 6) y += kDropH;
            else if (r <= 8) y += kSwitchH;
            else y += kSliderH;
        }
        return y;
    }

    [[nodiscard]] int mainSliderRow(int s) const noexcept { return s < 4 ? s : 9; }

    [[nodiscard]] Rect mainTrack(int s) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(mainSliderRow(s)) + 22.0f, trackW, 14.0f };
    }

    [[nodiscard]] Rect segBox() const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(4) + 20.0f, trackW, 22.0f };
    }

    [[nodiscard]] int segAt(float mx, float my) const noexcept
    {
        return segBox().contains(mx, my) ? 0 : -1;
    }

    [[nodiscard]] Rect mainDropBox(int d) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(5 + d) + 20.0f, trackW, 22.0f };
    }

    [[nodiscard]] Rect switchBox(int which) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(7 + which), trackW, kSwitchH };
    }

    [[nodiscard]] int switchAt(float mx, float my) const noexcept
    {
        if (switchBox(0).contains(mx, my)) return 0;
        if (switchBox(1).contains(mx, my)) return 1;
        return -1;
    }

    [[nodiscard]] Rect ccDropBox(int d) const noexcept
    {
        const float W      = static_cast<float>(getWidth());
        const float trackW = W - kCCX - kPad;
        return { kCCX, kSlidersY + d * kDropH + 20.0f, trackW, 22.0f };
    }

    [[nodiscard]] const DropDef* dropHeaderAt(float mx, float my) const noexcept
    {
        for (int d = 0; d < static_cast<int>(kMainDrops.size()); ++d)
            if (mainDropBox(d).contains(mx, my))
                return &kMainDrops[static_cast<std::size_t>(d)];
        for (int d = 0; d < static_cast<int>(kCCDropDefs.size()); ++d)
            if (ccDropBox(d).contains(mx, my))
                return &kCCDropDefs[static_cast<std::size_t>(d)];
        return nullptr;
    }

    void commitParam(uint32_t index, const char* key, float v)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.6g", v);
        setState(key, buf);
        values_[index] = v;
        repaint();
    }

    void updateSlider(int s, float mx)
    {
        const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
        const Rect tr        = mainTrack(s);
        const float t        = clampf((mx - tr.x) / tr.w, 0.0f, 1.0f);
        float v              = def.min + t * (def.max - def.min);
        if (def.integer) v   = std::round(v);
        commitParam(def.index, def.stateKey, v);
    }

    void openDrop(const DropDef* def)
    {
        closePopup();
        for (int d = 0; d < static_cast<int>(kMainDrops.size()); ++d) {
            if (&kMainDrops[static_cast<std::size_t>(d)] == def) {
                openMain_ = d;
                break;
            }
        }
        for (int d = 0; d < static_cast<int>(kCCDropDefs.size()); ++d) {
            if (&kCCDropDefs[static_cast<std::size_t>(d)] == def) {
                openIsCC_ = d;
                break;
            }
        }
        // Scroll the current value into view for scrollable lists.
        const int current = dropCurrentItem(def->kind, values_[def->index]);
        dropScroll_ = std::max(0, current - kMaxRows / 2);
        dropHover_ = -1;
        repaint();
    }

    void closePopup() noexcept
    {
        openMain_ = -1;
        openIsCC_ = -1;
        dropScroll_ = 0;
        dropHover_ = -1;
    }

    void commitDrop(int item)
    {
        const DropDef& def = openDef();
        commitParam(def.index, def.stateKey, dropItemValue(def.kind, item));
    }

    [[nodiscard]] Rect openHeaderBox() const noexcept
    {
        if (openIsCC_ >= 0)
            return ccDropBox(openIsCC_);
        return mainDropBox(openMain_);
    }

    [[nodiscard]] bool dropScrollable() const noexcept
    {
        return dropItemCount(openKind()) > kTwoColLimit;
    }

    [[nodiscard]] int dropCols() const noexcept
    {
        return dropScrollable() ? 1 : 2;
    }

    [[nodiscard]] int dropRows() const noexcept
    {
        const int count = dropItemCount(openKind());
        if (dropScrollable())
            return kMaxRows;
        return (count + 1) / 2;
    }

    [[nodiscard]] Rect popupRect(float W, float H) const noexcept
    {
        const Rect header = openHeaderBox();
        const int rows = dropRows();
        const float listH = rows * kItemH;
        const float popH = listH + 8.0f;
        const float popW = std::max(header.w, 190.0f);
        float x = std::min(header.x, W - popW - 8.0f);
        float y = header.y + header.h + 3.0f;
        if (y + popH > H - 8.0f)
            y = header.y - 3.0f - popH;  // flip upward when space is short
        return { x, y, popW, popH };
    }

    [[nodiscard]] int popupItemAt(float mx, float my, const Rect& popup) const noexcept
    {
        const float listX = popup.x + 4.0f;
        const float listY = popup.y + 4.0f;
        const float listW = popup.w - 8.0f - (dropScrollable() ? 10.0f : 0.0f);
        if (mx < listX || mx > listX + listW || my < listY)
            return -1;
        const int row = static_cast<int>((my - listY) / kItemH);
        const int count = dropItemCount(openKind());
        if (dropScrollable()) {
            if (row < 0 || row >= kMaxRows) return -1;
            const int item = dropScroll_ + row;
            return item < count ? item : -1;
        }
        const int cols = 2;
        const int rows = dropRows();
        if (row < 0 || row >= rows) return -1;
        const int col = static_cast<int>((mx - listX) / (listW / cols));
        if (col < 0 || col >= cols) return -1;
        const int item = row * cols + col;
        return item < count ? item : -1;
    }

    void drawBackground(float W, float H)
    {
        const auto& t = theme();
        beginPath();
        fc(t.background);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawHeader(float W)
    {
        const auto& t = theme();
        beginPath();
        fc(t.panel);
        rect(0, 0, W, 58.0f);
        fill();
        closePath();

        fontSize(24.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(kPad, 27.0f, "GHOST", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(kPad + 110.0f, 27.0f,
             "audio in \xe2\x86\x92 ghost MIDI \xc2\xb7 BBT 16ths \xc2\xb7 drums 10 / notes \xc2\xb7 Drift CC", nullptr);

        beginPath();
        sc(t.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, 58.0f);
        lineTo(W, 58.0f);
        stroke();
        closePath();

        drawColumnStrip(kMainX, kDivX - kMainX - kPad, "PARAMETERS", kMainAccent);
        drawColumnStrip(kCCX, W - kCCX - kPad, "DRIFT CC ROUTING", kCcAccent);

        // Theme toggle, magneto style: light panel by default.
        themeRect_ = {W - 90.0f, 16.0f, 74.0f, 26.0f};
        beginPath();
        fc(t.buttonFace);
        roundedRect(themeRect_.x, themeRect_.y, themeRect_.w, themeRect_.h, 2.0f);
        fill();
        closePath();
        fc(t.textDim);
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(themeRect_.x + themeRect_.w * 0.5f, themeRect_.y + themeRect_.h * 0.5f,
             darkTheme_ ? "DARK" : "LIGHT", nullptr);
    }

    void drawDivider(float H)
    {
        const auto& t = theme();
        beginPath();
        sc(t.panel);
        strokeWidth(1.0f);
        moveTo(kDivX, 58.0f);
        lineTo(kDivX, H);
        stroke();
        closePath();
    }

    void drawColumnStrip(float x, float w, const char* title, const Accent& accent)
    {
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        roundedRect(x, 59.0f, w, 18.0f, 2.0f);
        fill();
        closePath();
        fillColor(kCreamR, kCreamG, kCreamB, 255);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 8.0f, 68.0f, title, nullptr);
    }

    void drawTrack(const Rect& tr, float norm, bool dimmed)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
        fc(t.controlTrack);
        fill();
        closePath();

        if (norm > 0.0f) {
            beginPath();
            roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
            if (dimmed)
                fc(t.textDisabled);
            else
                fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
            fill();
            closePath();
        }
    }

    void drawLabelValue(float x, float y, float w, const char* label, const char* value)
    {
        const auto& t = theme();
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDim);
        text(x, y, label, nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(t.textPrimary);
        text(x + w, y, value, nullptr);
    }

    void drawMainColumn()
    {
        const auto& t = theme();
        const float trackW = kDivX - kMainX - kPad;

        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            drawLabelValue(kMainX, tr.y - 18.0f, trackW, def.label,
                           formatSlider(def, values_[def.index]).c_str());
            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            drawTrack(tr, norm, false);
        }

        // Mode: segmented two-way switch.
        {
            const Rect box = segBox();
            drawLabelValue(kMainX, box.y - 16.0f, trackW, "Mode",
                           values_[kParamMode] < 0.5f ? "Drums 10" : "Notes");
            const int selected = values_[kParamMode] < 0.5f ? 0 : 1;
            for (int i = 0; i < 2; ++i) {
                const float segW = box.w / 2.0f;
                const float sx = box.x + i * segW;
                beginPath();
                roundedRect(sx + 1.0f, box.y, segW - 2.0f, box.h, 4.0f);
                if (i == selected)
                    fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
                else
                    fc(t.buttonFace);
                fill();
                closePath();
                fontSize(11.0f);
                textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
                if (i == selected)
                    fillColor(kCreamR, kCreamG, kCreamB, 255);
                else
                    fc(t.textPrimary);
                text(sx + segW * 0.5f, box.y + box.h * 0.5f, kModeChoices[i], nullptr);
            }
        }

        // Channel + Base Note dropdowns.
        for (int d = 0; d < static_cast<int>(kMainDrops.size()); ++d) {
            const DropDef& def = kMainDrops[static_cast<std::size_t>(d)];
            const Rect box = mainDropBox(d);
            char current[32];
            dropItemText(def.kind, dropCurrentItem(def.kind, values_[def.index]),
                         current, sizeof(current));
            drawLabelValue(kMainX, box.y - 16.0f, trackW, def.label, current);
            drawDropBox(box, current);
        }

        // Pass MIDI + Audio Thru: toggle switches.
        struct SwitchRow { uint32_t index; const char* label; };
        constexpr std::array<SwitchRow, 2> kSwitches = {{
            { kParamPassInput, "Pass MIDI"  },
            { kParamAudioThru, "Audio Thru" },
        }};
        for (int i = 0; i < 2; ++i) {
            const auto& sw = kSwitches[static_cast<std::size_t>(i)];
            const Rect row = switchBox(i);
            const bool on = values_[sw.index] >= 0.5f;
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fc(t.textDim);
            text(kMainX, row.y + row.h * 0.5f, sw.label, nullptr);
            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fc(t.textPrimary);
            text(row.x + row.w - 52.0f, row.y + row.h * 0.5f, on ? "on" : "off", nullptr);
            drawSwitch(row.x + row.w - 44.0f, row.y + (row.h - 20.0f) * 0.5f, on);
        }
    }

    void drawSwitch(float x, float y, bool on)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(x, y, 44.0f, 20.0f, 10.0f);
        if (on)
            fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
        else
            fc(t.controlTrack);
        fill();
        closePath();
        beginPath();
        circle(x + (on ? 33.0f : 11.0f), y + 10.0f, 7.0f);
        fc(t.controlKnob);
        fill();
        closePath();
    }

    void drawDropBox(const Rect& box, const char* current)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(box.x, box.y, box.w, box.h, 4.0f);
        fc(t.buttonFace);
        fill();
        closePath();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, 4.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(box.x + 8.0f, box.y + box.h * 0.5f, current, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(box.x + box.w - 8.0f, box.y + box.h * 0.5f, "\xe2\x96\xbe", nullptr);
    }

    void drawCCColumn(float W)
    {
        const auto& t = theme();
        const float trackW = W - kCCX - kPad;

        for (int d = 0; d < static_cast<int>(kCCDropDefs.size()); ++d) {
            const DropDef& def = kCCDropDefs[static_cast<std::size_t>(d)];
            const Rect box = ccDropBox(d);
            char current[32];
            dropItemText(def.kind, dropCurrentItem(def.kind, values_[def.index]),
                         current, sizeof(current));
            const bool isOff = def.kind == DropKind::CcNumber
                && static_cast<int>(std::lround(values_[def.index])) == 0;
            fontSize(12.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(t.textDim);
            text(kCCX, box.y - 16.0f, def.label, nullptr);
            fontSize(11.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fc(isOff ? t.textDisabled : t.textPrimary);
            text(kCCX + trackW, box.y - 16.0f, current, nullptr);
            drawDropBox(box, current);
        }

        const float hintY = kSlidersY + kCCDropDefs.size() * kDropH + 8.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDisabled);
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 Ghost MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "off disables the override for that control.", nullptr);
        text(kCCX, hintY + 28.0f, "Routing CCs are consumed, other MIDI passes thru.", nullptr);
    }

    void drawFooterNote()
    {
        const auto& t = theme();
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDisabled);
        text(kMainX, mainRowY(9) + kSliderH + 10.0f,
             "Needs running transport: ghosts quantise to 16ths.", nullptr);
        text(kMainX, mainRowY(9) + kSliderH + 24.0f,
             "Drag pushes ghosts late inside their slot.", nullptr);
    }

    [[nodiscard]] const Accent& popupAccent() const noexcept
    {
        return openIsCC_ >= 0 ? kCcAccent : kMainAccent;
    }

    void drawPopup(float W, float H)
    {
        const auto& t = theme();
        const Rect popup = popupRect(W, H);
        const DropKind kind = openKind();
        const int count = dropItemCount(kind);
        const bool scrollable = dropScrollable();
        const int cols = dropCols();
        const int current = dropCurrentItem(kind, values_[openDef().index]);

        // Scrim.
        beginPath();
        fillColor(0, 0, 0, 110);
        rect(0, 0, W, H);
        fill();
        closePath();

        // Panel.
        beginPath();
        roundedRect(popup.x, popup.y, popup.w, popup.h, 4.0f);
        fc(t.surface);
        fill();
        closePath();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(popup.x + 0.5f, popup.y + 0.5f, popup.w - 1.0f, popup.h - 1.0f, 4.0f);
        stroke();
        closePath();

        const float listX = popup.x + 4.0f;
        const float listY = popup.y + 4.0f;
        const float listW = popup.w - 8.0f - (scrollable ? 10.0f : 0.0f);
        const int rows = dropRows();
        const int top = scrollable ? dropScroll_ : 0;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int item = scrollable ? top + r : r * cols + c;
                if (item >= count) continue;
                const float cw = listW / cols;
                const float cx = listX + c * cw;
                const float cy = listY + r * kItemH;
                const bool hovered = item == dropHover_;
                const bool selected = item == current;
                if (hovered || selected) {
                    beginPath();
                    roundedRect(cx, cy, cw, kItemH, 3.0f);
                    const Accent& accent = popupAccent();
                    if (selected)
                        fillColor(accent.r, accent.g, accent.b, 255);
                    else
                        fillColor(accent.r, accent.g, accent.b, 90);
                    fill();
                    closePath();
                }
                char itemText[32];
                dropItemText(kind, item, itemText, sizeof(itemText));
                fontSize(11.0f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (selected)
                    fillColor(kCreamR, kCreamG, kCreamB, 255);
                else
                    fc(t.textPrimary);
                text(cx + 8.0f, cy + kItemH * 0.5f, itemText, nullptr);
            }
        }

        if (scrollable) {
            const float trackX = popup.x + popup.w - 10.0f;
            const float trackY = listY;
            const float trackH = rows * kItemH;
            beginPath();
            roundedRect(trackX, trackY, 5.0f, trackH, 2.0f);
            fc(t.controlTrack);
            fill();
            closePath();
            const float thumbH = std::max(16.0f, trackH * rows / count);
            const float thumbY = trackY + (trackH - thumbH) * dropScroll_
                / std::max(1, count - rows);
            beginPath();
            roundedRect(trackX, thumbY, 5.0f, thumbH, 2.0f);
            const Accent& accent = popupAccent();
            fillColor(accent.r, accent.g, accent.b, 255);
            fill();
            closePath();
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GhostUI)
};

UI* createUI()
{
    return new GhostUI();
}

END_NAMESPACE_DISTRHO
