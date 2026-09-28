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

// Must match HelterSkelterPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamSensitivity,
    kParamDepth,
    kParamResonance,
    kParamBaseFreq,
    kParamDivision,
    kParamGateBeats,
    kParamAttack,
    kParamDecay,
    kParamSustain,
    kParamRelease,
    kParamInvert,
    kParamMix,
    kParamTrim,
    kParamBypass,
    kParamCCSensitivity,
    kParamCCDepth,
    kParamCCResonance,
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

constexpr std::array<SliderDef, 11> kMainSliders = {{
    { kParamSensitivity, "Sensitivity", 0.0f,    1.0f,    false, "sensitivity" },
    { kParamDepth,       "Depth",        0.0f,    1.0f,    false, "depth"       },
    { kParamResonance,   "Resonance",    0.5f,    12.0f,   false, "resonance"   },
    { kParamBaseFreq,    "Base Freq",    100.0f,  2000.0f, false, "base_freq"   },
    { kParamGateBeats,   "Gate Beats",   0.5f,    8.0f,    false, "gate_beats"  },
    { kParamAttack,      "Attack",       1.0f,    500.0f,  false, "attack"      },
    { kParamDecay,       "Decay",        5.0f,    1000.0f, false, "decay"       },
    { kParamSustain,     "Sustain",      0.0f,    1.0f,    false, "sustain"     },
    { kParamRelease,     "Release",      5.0f,    2000.0f, false, "release"     },
    { kParamMix,         "Mix",          0.0f,    100.0f,  false, "mix"         },
    { kParamTrim,        "Trim",         -12.0f,  12.0f,  false, "trim"        },
}};

enum class DropKind : std::uint8_t {
    Division,  // 1/2/4/8 beats
    Channel,   // 1-16
    CcNumber,  // off + 1-127 with common names
};

struct DropDef {
    uint32_t    index;
    const char* label;
    const char* stateKey;
    DropKind    kind;
};

constexpr std::array<DropDef, 1> kMainDrops = {{
    { kParamDivision, "Division", "division", DropKind::Division },
}};

constexpr std::array<DropDef, 5> kCCDropDefs = {{
    { kParamCCSensitivity, "CC Sens",  "cc_sensitivity", DropKind::CcNumber },
    { kParamCCDepth,       "CC Depth", "cc_depth",       DropKind::CcNumber },
    { kParamCCResonance,   "CC Res",   "cc_resonance",   DropKind::CcNumber },
    { kParamCCMix,         "CC Mix",   "cc_mix",         DropKind::CcNumber },
    { kParamCCChannel,     "CC Channel", "cc_channel",   DropKind::Channel  },
}};

constexpr const char* kModeChoices[3] = { "Envelope", "BBT ADSR", "Blend" };
constexpr const char* kDivisionChoices[4] = { "1 beat", "2 beats", "4 beats", "8 beats" };

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
    case kParamDepth:
    case kParamSustain:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0f);
        break;
    case kParamResonance:
        std::snprintf(buf, sizeof(buf), "Q %.1f", v);
        break;
    case kParamBaseFreq:
        std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
        break;
    case kParamGateBeats:
        std::snprintf(buf, sizeof(buf), "%.1f beats", v);
        break;
    case kParamAttack:
    case kParamDecay:
    case kParamRelease:
        if (v >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f s", v / 1000.0f);
        else std::snprintf(buf, sizeof(buf), "%.0f ms", v);
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
    case DropKind::Division: return 4;
    case DropKind::Channel:  return 16;
    case DropKind::CcNumber: return 129;
    }
    return 0;
}

void dropItemText(DropKind kind, int item, char* buf, std::size_t size)
{
    switch (kind) {
    case DropKind::Division:
        std::snprintf(buf, size, "%s", kDivisionChoices[item]);
        break;
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
    case DropKind::Division: return std::clamp(static_cast<int>(std::lround(value)), 0, 3);
    case DropKind::Channel:  return std::clamp(static_cast<int>(std::lround(value)) - 1, 0, 15);
    case DropKind::CcNumber: return std::clamp(static_cast<int>(std::lround(value)), 0, 127);
    }
    return 0;
}

[[nodiscard]] float dropItemValue(DropKind kind, int item) noexcept
{
    switch (kind) {
    case DropKind::Division: return static_cast<float>(item);
    case DropKind::Channel:  return static_cast<float>(item + 1);
    case DropKind::CcNumber: return static_cast<float>(item);
    }
    return 0.0f;
}

constexpr float kPad      = 16.0f;
constexpr float kDivX     = 450.0f;
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kSlidersY = 76.0f;

constexpr float kSliderH = 42.0f;
constexpr float kSegH    = 46.0f;
constexpr float kDropH   = 46.0f;
constexpr float kSwitchH = 38.0f;

constexpr float kItemH       = 20.0f;
constexpr int   kMaxRows     = 10;
constexpr int   kTwoColLimit = 20;

}  // namespace

class HelterSkelterUI : public UI
{
public:
    HelterSkelterUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamMode] = 0.0f;
        values_[kParamSensitivity] = 0.6f;
        values_[kParamDepth] = 0.7f;
        values_[kParamResonance] = 4.0f;
        values_[kParamBaseFreq] = 400.0f;
        values_[kParamDivision] = 2.0f;
        values_[kParamGateBeats] = 2.0f;
        values_[kParamAttack] = 20.0f;
        values_[kParamDecay] = 150.0f;
        values_[kParamSustain] = 0.7f;
        values_[kParamRelease] = 200.0f;
        values_[kParamInvert] = 0.0f;
        values_[kParamMix] = 100.0f;
        values_[kParamTrim] = 0.0f;
        values_[kParamBypass] = 0.0f;
        values_[kParamCCSensitivity] = 1.0f;
        values_[kParamCCDepth] = 2.0f;
        values_[kParamCCResonance] = 3.0f;
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
        else if (std::strcmp(key, "sensitivity")    == 0) { values_[kParamSensitivity] = fv; }
        else if (std::strcmp(key, "depth")          == 0) { values_[kParamDepth] = fv; }
        else if (std::strcmp(key, "resonance")      == 0) { values_[kParamResonance] = fv; }
        else if (std::strcmp(key, "base_freq")      == 0) { values_[kParamBaseFreq] = fv; }
        else if (std::strcmp(key, "division")       == 0) { values_[kParamDivision] = fv; }
        else if (std::strcmp(key, "gate_beats")     == 0) { values_[kParamGateBeats] = fv; }
        else if (std::strcmp(key, "attack")         == 0) { values_[kParamAttack] = fv; }
        else if (std::strcmp(key, "decay")          == 0) { values_[kParamDecay] = fv; }
        else if (std::strcmp(key, "sustain")        == 0) { values_[kParamSustain] = fv; }
        else if (std::strcmp(key, "release")        == 0) { values_[kParamRelease] = fv; }
        else if (std::strcmp(key, "invert")         == 0) { values_[kParamInvert] = fv; }
        else if (std::strcmp(key, "mix")            == 0) { values_[kParamMix] = fv; }
        else if (std::strcmp(key, "trim")           == 0) { values_[kParamTrim] = fv; }
        else if (std::strcmp(key, "bypass")         == 0) { values_[kParamBypass] = fv; }
        else if (std::strcmp(key, "cc_sensitivity") == 0) { values_[kParamCCSensitivity] = fv; }
        else if (std::strcmp(key, "cc_depth")       == 0) { values_[kParamCCDepth] = fv; }
        else if (std::strcmp(key, "cc_resonance")   == 0) { values_[kParamCCResonance] = fv; }
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
        if (popupOpen())
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
            commitParam(kParamMode, "mode", std::floor(t * 3.0f));
            return true;
        }
        if (switchAt(mx, my) == 0) {
            commitParam(kParamInvert, "invert",
                        values_[kParamInvert] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (switchAt(mx, my) == 1) {
            commitParam(kParamBypass, "bypass",
                        values_[kParamBypass] >= 0.5f ? 0.0f : 1.0f);
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

    // Open dropdown: index into the combined main + CC dropdown list, or -1.
    int openDrop_ = -1;
    int dropScroll_ = 0;
    int dropHover_ = -1;

    [[nodiscard]] bool popupOpen() const noexcept { return openDrop_ >= 0; }

    [[nodiscard]] const DropDef& openDef() const noexcept
    {
        if (openDrop_ == 0)
            return kMainDrops[0];
        return kCCDropDefs[static_cast<std::size_t>(openDrop_ - 1)];
    }

    [[nodiscard]] DropKind openKind() const noexcept { return openDef().kind; }

    [[nodiscard]] const Accent& popupAccent() const noexcept
    {
        return openDrop_ == 0 ? kMainAccent : kCcAccent;
    }

    // Left column rows: Mode segment, 4 sliders, Division drop, 5 sliders,
    // Invert switch, 2 sliders, Bypass switch.
    [[nodiscard]] float mainRowY(int row) const noexcept
    {
        float y = kSlidersY;
        for (int r = 0; r < row; ++r) {
            if (r == 0) y += kSegH;
            else if (r <= 4) y += kSliderH;
            else if (r == 5) y += kDropH;
            else if (r <= 10) y += kSliderH;
            else if (r == 11) y += kSwitchH;
            else if (r <= 13) y += kSliderH;
            else y += kSwitchH;
        }
        return y;
    }

    [[nodiscard]] int mainSliderRow(int s) const noexcept
    {
        if (s < 4) return 1 + s;
        if (s < 9) return 2 + s;
        return 3 + s;
    }

    [[nodiscard]] Rect mainTrack(int s) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(mainSliderRow(s)) + 19.0f, trackW, 12.0f };
    }

    [[nodiscard]] Rect segBox() const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(0) + 19.0f, trackW, 22.0f };
    }

    [[nodiscard]] int segAt(float mx, float my) const noexcept
    {
        return segBox().contains(mx, my) ? 0 : -1;
    }

    [[nodiscard]] Rect mainDropBox() const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(5) + 19.0f, trackW, 22.0f };
    }

    [[nodiscard]] Rect switchBox(int which) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, mainRowY(which == 0 ? 11 : 14), trackW, kSwitchH };
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
        return { kCCX, kSlidersY + d * kDropH + 19.0f, trackW, 22.0f };
    }

    // Combined header ids: 0 = Division, 1..5 = CC dropdowns.
    [[nodiscard]] int dropHeaderAt(float mx, float my) const noexcept
    {
        if (mainDropBox().contains(mx, my))
            return 0;
        for (int d = 0; d < static_cast<int>(kCCDropDefs.size()); ++d)
            if (ccDropBox(d).contains(mx, my))
                return 1 + d;
        return -1;
    }

    [[nodiscard]] Rect openHeaderBox() const noexcept
    {
        if (openDrop_ == 0)
            return mainDropBox();
        return ccDropBox(openDrop_ - 1);
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

    void openDrop(int id)
    {
        openDrop_ = id;
        const DropDef& def = openDef();
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
        const Rect header = openHeaderBox();
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
        text(kPad, 27.0f, "HELTER SKELTER", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(kPad + 215.0f, 27.0f,
             "automatic wah \xc2\xb7 envelope / BBT ADSR \xc2\xb7 Drift CC", nullptr);

        beginPath();
        sc(t.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, 58.0f);
        lineTo(W, 58.0f);
        stroke();
        closePath();

        drawColumnStrip(kMainX, kDivX - kMainX - kPad, "PARAMETERS", kMainAccent);
        drawColumnStrip(kCCX, W - kCCX - kPad, "DRIFT CC ROUTING", kCcAccent);

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

        // Mode: segmented three-way switch.
        {
            const Rect box = segBox();
            const int selected = std::clamp(static_cast<int>(std::lround(values_[kParamMode])), 0, 2);
            drawLabelValue(kMainX, box.y - 17.0f, trackW, "Mode", kModeChoices[selected]);
            for (int i = 0; i < 3; ++i) {
                const float segW = box.w / 3.0f;
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

        for (int s = 0; s < 4; ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            drawLabelValue(kMainX, tr.y - 17.0f, trackW, def.label,
                           formatSlider(def, values_[def.index]).c_str());
            drawTrack(tr, clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f));
        }

        // Division dropdown.
        {
            const DropDef& def = kMainDrops[0];
            const Rect box = mainDropBox();
            char current[32];
            dropItemText(def.kind, dropCurrentItem(def.kind, values_[def.index]),
                         current, sizeof(current));
            drawLabelValue(kMainX, box.y - 17.0f, trackW, def.label, current);
            drawDropBox(box, current);
        }

        for (int s = 4; s < 9; ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            drawLabelValue(kMainX, tr.y - 17.0f, trackW, def.label,
                           formatSlider(def, values_[def.index]).c_str());
            drawTrack(tr, clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f));
        }

        // Invert switch.
        drawSwitchRow(kMainX, trackW, switchBox(0), "Invert",
                      values_[kParamInvert] >= 0.5f, "on", "off");

        for (int s = 9; s < 11; ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            drawLabelValue(kMainX, tr.y - 17.0f, trackW, def.label,
                           formatSlider(def, values_[def.index]).c_str());
            drawTrack(tr, clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f));
        }

        // Bypass switch.
        drawSwitchRow(kMainX, trackW, switchBox(1), "Bypass",
                      values_[kParamBypass] >= 0.5f, "bypassed", "active");
    }

    void drawSwitchRow(float x, float w, const Rect& row, const char* label,
                       bool on, const char* onText, const char* offText)
    {
        const auto& t = theme();
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(x, row.y + row.h * 0.5f, label, nullptr);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(x + w - 52.0f, row.y + row.h * 0.5f, on ? onText : offText, nullptr);
        drawSwitch(row.x + row.w - 44.0f, row.y + (row.h - 20.0f) * 0.5f, on);
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
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 wah MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "off disables the override for that control.", nullptr);
        text(kCCX, hintY + 38.0f, "Defaults: 1=Sens  2=Depth  3=Res  4=Mix.", nullptr);
        text(kCCX, hintY + 62.0f, "BBT needs running transport: Division sets", nullptr);
        text(kCCX, hintY + 76.0f, "the ADSR cycle, Gate its high beats.", nullptr);
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
        const Accent& accent = popupAccent();

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
            fillColor(accent.r, accent.g, accent.b, 255);
            fill();
            closePath();
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelterSkelterUI)
};

UI* createUI()
{
    return new HelterSkelterUI();
}

END_NAMESPACE_DISTRHO
