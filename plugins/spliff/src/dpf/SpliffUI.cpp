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

// Must match SpliffPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDepth,
    kParamSensitivity,
    kParamSharpness,
    kParamDecay,
    kParamDecayTilt,
    kParamSplitLow,
    kParamSplitHigh,
    kParamMix,
    kParamTrim,
    kParamBypass,
    kParamDelta,
    kParamCCDepth,
    kParamCCSensitivity,
    kParamCCDecay,
    kParamCCMix,
    kParamCCChannel,
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

constexpr std::array<SliderDef, 9> kMainSliders = {{
    { kParamDepth,       "Depth",        0.0f,    1.0f,    false, "depth"       },
    { kParamSensitivity, "Sensitivity",  0.0f,    1.0f,    false, "sensitivity" },
    { kParamSharpness,   "Sharpness",    0.0f,    1.0f,    false, "sharpness"   },
    { kParamDecay,       "Decay",        0.0f,    1.0f,    false, "decay"       },
    { kParamDecayTilt,   "Decay LF/HF",  -1.0f,   1.0f,    false, "decay_tilt"  },
    { kParamSplitLow,    "Split Low",    20.0f,   2000.0f, false, "split_low"   },
    { kParamSplitHigh,   "Split High",   500.0f,  12000.0f,false, "split_high"  },
    { kParamMix,         "Mix",          0.0f,    100.0f,  false, "mix"         },
    { kParamTrim,        "Trim",         -12.0f,  12.0f,  false, "trim"        },
}};

enum class DropKind : std::uint8_t {
    Channel,   // 1-16
    CcNumber,  // off + 1-127 with common names
};

struct DropDef {
    uint32_t    index;
    const char* label;
    const char* stateKey;
    DropKind    kind;
};

constexpr std::array<DropDef, 5> kCCDropDefs = {{
    { kParamCCDepth,       "CC Depth", "cc_depth",       DropKind::CcNumber },
    { kParamCCSensitivity, "CC Sens",  "cc_sensitivity", DropKind::CcNumber },
    { kParamCCDecay,       "CC Decay", "cc_decay",       DropKind::CcNumber },
    { kParamCCMix,         "CC Mix",   "cc_mix",         DropKind::CcNumber },
    { kParamCCChannel,     "CC Channel", "cc_channel",   DropKind::Channel  },
}};

constexpr const char* kModeChoices[2] = { "Cut", "Boost" };

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
    case kParamDepth:
    case kParamSensitivity:
    case kParamSharpness:
    case kParamDecay:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
        break;
    case kParamDecayTilt:
        if (std::fabs(v) < 0.01f) std::snprintf(buf, sizeof(buf), "equal");
        else std::snprintf(buf, sizeof(buf), "%s%.0f%%", v < 0 ? "LF " : "HF ", std::fabs(v) * 100.0f);
        break;
    case kParamSplitLow:
    case kParamSplitHigh:
        if (v >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f kHz", v / 1000.0f);
        else std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
        break;
    case kParamMix:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
        break;
    case kParamTrim:
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        break;
    }
    return buf;
}

[[nodiscard]] int dropItemCount(DropKind kind) noexcept
{
    switch (kind) {
    case DropKind::Channel:  return 16;
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
    case DropKind::CcNumber: return std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    }
    return 0;
}

[[nodiscard]] float dropItemValue(DropKind kind, int item) noexcept
{
    switch (kind) {
    case DropKind::Channel:  return static_cast<float>(item + 1);
    case DropKind::CcNumber: return static_cast<float>(item);  // item 0 = off
    }
    return 0.0f;
}

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
constexpr float kDivX     = 430.0f;
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kSlidersY = 76.0f;

constexpr float kSliderH = 44.0f;
constexpr float kSegH    = 48.0f;
constexpr float kDropH   = 48.0f;
constexpr float kSwitchH = 40.0f;

constexpr float kItemH       = 20.0f;
constexpr int   kMaxRows     = 10;
constexpr int   kTwoColLimit = 20;

}  // namespace

class SpliffUI : public UI
{
public:
    SpliffUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamMode] = 0.0f;
        values_[kParamDepth] = 0.5f;
        values_[kParamSensitivity] = 0.5f;
        values_[kParamSharpness] = 0.3f;
        values_[kParamDecay] = 0.25f;
        values_[kParamDecayTilt] = 0.0f;
        values_[kParamSplitLow] = 250.0f;
        values_[kParamSplitHigh] = 4000.0f;
        values_[kParamMix] = 100.0f;
        values_[kParamTrim] = 0.0f;
        values_[kParamBypass] = 0.0f;
        values_[kParamDelta] = 0.0f;
        values_[kParamCCDepth] = 1.0f;
        values_[kParamCCSensitivity] = 2.0f;
        values_[kParamCCDecay] = 3.0f;
        values_[kParamCCMix] = 4.0f;
        values_[kParamCCChannel] = 1.0f;

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
        if      (std::strcmp(key, "mode")           == 0) { values_[kParamMode] = fv; }
        else if (std::strcmp(key, "depth")          == 0) { values_[kParamDepth] = fv; }
        else if (std::strcmp(key, "sensitivity")    == 0) { values_[kParamSensitivity] = fv; }
        else if (std::strcmp(key, "sharpness")      == 0) { values_[kParamSharpness] = fv; }
        else if (std::strcmp(key, "decay")          == 0) { values_[kParamDecay] = fv; }
        else if (std::strcmp(key, "decay_tilt")     == 0) { values_[kParamDecayTilt] = fv; }
        else if (std::strcmp(key, "split_low")      == 0) { values_[kParamSplitLow] = fv; }
        else if (std::strcmp(key, "split_high")     == 0) { values_[kParamSplitHigh] = fv; }
        else if (std::strcmp(key, "mix")            == 0) { values_[kParamMix] = fv; }
        else if (std::strcmp(key, "trim")           == 0) { values_[kParamTrim] = fv; }
        else if (std::strcmp(key, "bypass")         == 0) { values_[kParamBypass] = fv; }
        else if (std::strcmp(key, "delta")          == 0) { values_[kParamDelta] = fv; }
        else if (std::strcmp(key, "cc_depth")       == 0) { values_[kParamCCDepth] = fv; }
        else if (std::strcmp(key, "cc_sensitivity") == 0) { values_[kParamCCSensitivity] = fv; }
        else if (std::strcmp(key, "cc_decay")       == 0) { values_[kParamCCDecay] = fv; }
        else if (std::strcmp(key, "cc_mix")         == 0) { values_[kParamCCMix] = fv; }
        else if (std::strcmp(key, "cc_channel")     == 0) { values_[kParamCCChannel] = fv; }
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
        if (openDrop_ >= 0)
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
            closePopup();
            if (dropHeaderAt(mx, my) < 0 && segAt(mx, my) < 0 && switchAt(mx, my) < 0) {
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
            commitParam(kParamBypass, "bypass",
                        values_[kParamBypass] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (switchAt(mx, my) == 1) {
            commitParam(kParamDelta, "delta",
                        values_[kParamDelta] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (dropHeaderAt(mx, my) >= 0) {
            openDrop(dropHeaderAt(mx, my));
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

    int openDrop_ = -1;  // index into kCCDropDefs, or -1
    int dropScroll_ = 0;
    int dropHover_ = -1;

    [[nodiscard]] bool popupOpen() const noexcept { return openDrop_ >= 0; }

    [[nodiscard]] const DropDef& openDef() const noexcept
    {
        return kCCDropDefs[static_cast<std::size_t>(openDrop_)];
    }

    [[nodiscard]] DropKind openKind() const noexcept { return openDef().kind; }

    // Left column rows: Mode segment, 9 sliders, Bypass switch, Delta switch.
    [[nodiscard]] float mainRowY(int row) const noexcept
    {
        float y = kSlidersY;
        for (int r = 0; r < row; ++r) {
            if (r == 0) y += kSegH;
            else if (r <= 9) y += kSliderH;
            else y += kSwitchH;
        }
        return y;
    }

    [[nodiscard]] Rect mainTrack(int s) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(1 + s) + 20.0f, trackW, 12.0f };
    }

    [[nodiscard]] Rect segBox() const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(0) + 20.0f, trackW, 22.0f };
    }

    [[nodiscard]] int segAt(float mx, float my) const noexcept
    {
        return segBox().contains(mx, my) ? 0 : -1;
    }

    [[nodiscard]] Rect switchBox(int which) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(10 + which), trackW, kSwitchH };
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

    [[nodiscard]] int dropHeaderAt(float mx, float my) const noexcept
    {
        for (int d = 0; d < static_cast<int>(kCCDropDefs.size()); ++d)
            if (ccDropBox(d).contains(mx, my))
                return d;
        return -1;
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

    void openDrop(int d)
    {
        openDrop_ = d;
        const DropDef& def = kCCDropDefs[static_cast<std::size_t>(d)];
        const int current = dropCurrentItem(def.kind, values_[def.index]);
        dropScroll_ = std::max(0, current - kMaxRows / 2);
        dropHover_ = -1;
        repaint();
    }

    void closePopup() noexcept
    {
        openDrop_ = -1;
        dropScroll_ = 0;
        dropHover_ = -1;
    }

    void commitDrop(int item)
    {
        const DropDef& def = openDef();
        commitParam(def.index, def.stateKey, dropItemValue(def.kind, item));
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
        const Rect header = ccDropBox(openDrop_);
        const int rows = dropRows();
        const float popH = rows * kItemH + 8.0f;
        const float popW = std::max(header.w, 190.0f);
        float x = std::min(header.x, W - popW - 8.0f);
        float y = header.y + header.h + 3.0f;
        if (y + popH > H - 8.0f)
            y = header.y - 3.0f - popH;
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
        text(kPad, 27.0f, "SPLIFF", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(kPad + 110.0f, 27.0f,
             "adaptive transient processor \xc2\xb7 cut / boost \xc2\xb7 3-band \xc2\xb7 Drift CC", nullptr);

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

    void drawTrack(const Rect& tr, float norm)
    {
        const auto& t = theme();
        beginPath();
        roundedRect(tr.x, tr.y, tr.w, tr.h, 6.0f);
        fc(t.controlTrack);
        fill();
        closePath();

        if (norm > 0.0f) {
            beginPath();
            roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 6.0f);
            fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
            fill();
            closePath();
        }
    }

    void drawLabelValue(float x, float y, float w, const char* label, const char* value)
    {
        const auto& t = theme();
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDim);
        text(x, y, label, nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(t.textPrimary);
        text(x + w, y, value, nullptr);
    }

    void drawMainColumn()
    {
        const auto& t = theme();
        const float trackW = kDivX - kMainX - kPad;

        // Mode: segmented two-way switch.
        {
            const Rect box = segBox();
            drawLabelValue(kMainX, box.y - 17.0f, trackW, "Mode",
                           values_[kParamMode] < 0.5f ? "Cut" : "Boost");
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

        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            drawLabelValue(kMainX, tr.y - 17.0f, trackW, def.label,
                           formatSlider(def, values_[def.index]).c_str());
            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            drawTrack(tr, norm);
        }

        // Bypass + Delta: toggle switches.
        struct SwitchRow { uint32_t index; const char* label; const char* onText; const char* offText; };
        constexpr std::array<SwitchRow, 2> kSwitches = {{
            { kParamBypass, "Bypass", "bypassed", "active" },
            { kParamDelta,  "Delta",  "on",       "off"    },
        }};
        for (int i = 0; i < 2; ++i) {
            const auto& sw = kSwitches[static_cast<std::size_t>(i)];
            const Rect row = switchBox(i);
            const bool on = values_[sw.index] >= 0.5f;
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fc(t.textDim);
            text(kMainX, row.y + row.h * 0.5f, sw.label, nullptr);
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fc(t.textPrimary);
            text(row.x + row.w - 52.0f, row.y + row.h * 0.5f, on ? sw.onText : sw.offText, nullptr);
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
            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(t.textDim);
            text(kCCX, box.y - 17.0f, def.label, nullptr);
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fc(isOff ? t.textDisabled : t.textPrimary);
            text(kCCX + trackW, box.y - 17.0f, current, nullptr);
            drawDropBox(box, current);
        }

        const float hintY = kSlidersY + kCCDropDefs.size() * kDropH + 8.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDisabled);
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 Spliff MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "off disables the override for that control.", nullptr);
        text(kCCX, hintY + 38.0f, "Workflow: Delta on, raise Depth, tune Sens+Decay,", nullptr);
        text(kCCX, hintY + 52.0f, "then Delta off and set Mix. Bypass compares.", nullptr);
    }

    void drawFooterNote()
    {
        const auto& t = theme();
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDisabled);
        text(kMainX, mainRowY(12) + 10.0f,
             "Cut last in the chain \xc2\xb7 Boost early, before compression.", nullptr);
    }

    [[nodiscard]] const Accent& popupAccent() const noexcept
    {
        return kCcAccent;  // all spliff dropdowns live in the CC column
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

        beginPath();
        fillColor(0, 0, 0, 110);
        rect(0, 0, W, H);
        fill();
        closePath();

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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpliffUI)
};

UI* createUI()
{
    return new SpliffUI();
}

END_NAMESPACE_DISTRHO
