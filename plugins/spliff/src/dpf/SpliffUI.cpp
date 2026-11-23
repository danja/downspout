#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"
#include "spliff_spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;
namespace spectrum = downspout::spliff;

namespace {

// Must match SpliffPlugin.cpp exactly — the original 19 controls keep their indices.
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
    kParamAttackDb,
    kParamSustainDb,
    kParameterCount,
};

// Spectrum outputs start at 19: the analyser source is state, not a host parameter. The UI keeps one extra local slot
// for the source value so the existing stateChanged plumbing can share `values_`.
inline constexpr uint32_t kSpectrumFirst = kParameterCount;
inline constexpr uint32_t kSpectrumLast = kSpectrumFirst + spectrum::kSpectrumBins;
inline constexpr uint32_t kUiValueCount = kSpectrumLast;
inline constexpr uint32_t kAnalyzerSourceValue = kParameterCount;

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
    { kParamTrim,        "Trim",         -12.0f,  12.0f,   false, "trim"        },
}};

// Designer mode replaces the Spiff-specific rows; its state is appended so Cut/Boost projects keep their layout.
constexpr std::array<SliderDef, 2> kDesignerSliders = {{
    { kParamAttackDb,  "Attack",  -15.0f, 15.0f, false, "attack_db"  },
    { kParamSustainDb, "Sustain", -24.0f, 24.0f, false, "sustain_db" },
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

constexpr const char* kModeChoices[3] = { "Cut", "Boost", "Designer" };
constexpr const char* kSourceNames[3] = { "Input", "Output", "Delta" };
constexpr const char* kTabNames[2] = { "PROCESS", "DRIFT CC" };

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
    case kParamAttackDb:
    case kParamSustainDb:
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

// Section accents from the magneto palette: amber for processing, cold steel for Drift plumbing and the input spectrum.
constexpr Accent kMainAccent {198, 132, 58};
constexpr Accent kCcAccent {92, 140, 156};
constexpr Accent kInputAccent {132, 154, 168};
constexpr Accent kDeltaAccent {214, 100, 82};

constexpr int kCreamR = 250;
constexpr int kCreamG = 248;
constexpr int kCreamB = 242;

constexpr float kPad = 20.0f;
constexpr float kHeaderH = 70.0f;
constexpr float kTabY = 72.0f;
constexpr float kTabH = 32.0f;
constexpr float kContentY = 116.0f;
constexpr float kItemH = 20.0f;
constexpr int kMaxRows = 10;
constexpr int kTwoColLimit = 20;

constexpr Rect kAnalyserPanel {kPad, kContentY, 680.0f, 244.0f};
constexpr Rect kProcessPanel {kPad, 372.0f, 680.0f, 286.0f};
constexpr Rect kDriftPanel {kPad, kContentY, 680.0f, 542.0f};

}  // namespace

class SpliffUI : public UI
{
public:
    SpliffUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamMode] = 0.0f;
        values_[kParamDepth] = 0.5f;
        values_[kParamSensitivity] = 0.5f;
        values_[kParamSharpness] = 0.7f;
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
        values_[kParamAttackDb] = 0.0f;
        values_[kParamSustainDb] = 0.0f;
        values_[kAnalyzerSourceValue] = 1.0f;  // output
        spectrum_.fill(spectrum::kSpectrumFloorDb);

        // The standalone used for screenshots has no audio. Show a plausible spectrum so the analyser is legible.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            for (int k = 0; k < spectrum::kSpectrumBins; ++k) {
                const double hz = std::sqrt(spectrum::spectrumBandEdgeHz(k)
                                          * spectrum::spectrumBandEdgeHz(k + 1));
                double db = -72.0 - 8.0 * std::log10(hz / 100.0)
                          + 9.0 * std::sin(k * 0.51) * std::sin(k * 0.17)
                          + (std::fabs(std::log2(hz / 250.0)) < 0.3 ? 6.0 : 0.0)
                          + (std::fabs(std::log2(hz / 4000.0)) < 0.25 ? 5.0 : 0.0);
                spectrum_[static_cast<std::size_t>(k)] =
                    static_cast<float>(std::clamp(db, -90.0, 0.0));
            }
        }

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (demo_ && index >= kSpectrumFirst)
            return;  // keep the demo spectrum
        if (index >= kSpectrumFirst && index < kSpectrumLast) {
            spectrum_[index - kSpectrumFirst] = value;
            repaint();
        }
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (!value) return;
        const float fv = static_cast<float>(std::atof(value));
        if      (std::strcmp(key, "mode")            == 0) { values_[kParamMode] = fv; }
        else if (std::strcmp(key, "depth")           == 0) { values_[kParamDepth] = fv; }
        else if (std::strcmp(key, "sensitivity")     == 0) { values_[kParamSensitivity] = fv; }
        else if (std::strcmp(key, "sharpness")       == 0) { values_[kParamSharpness] = fv; }
        else if (std::strcmp(key, "decay")           == 0) { values_[kParamDecay] = fv; }
        else if (std::strcmp(key, "decay_tilt")      == 0) { values_[kParamDecayTilt] = fv; }
        else if (std::strcmp(key, "split_low")       == 0) { values_[kParamSplitLow] = fv; }
        else if (std::strcmp(key, "split_high")      == 0) { values_[kParamSplitHigh] = fv; }
        else if (std::strcmp(key, "mix")             == 0) { values_[kParamMix] = fv; }
        else if (std::strcmp(key, "trim")            == 0) { values_[kParamTrim] = fv; }
        else if (std::strcmp(key, "bypass")          == 0) { values_[kParamBypass] = fv; }
        else if (std::strcmp(key, "delta")           == 0) { values_[kParamDelta] = fv; }
        else if (std::strcmp(key, "cc_depth")        == 0) { values_[kParamCCDepth] = fv; }
        else if (std::strcmp(key, "cc_sensitivity")  == 0) { values_[kParamCCSensitivity] = fv; }
        else if (std::strcmp(key, "cc_decay")        == 0) { values_[kParamCCDecay] = fv; }
        else if (std::strcmp(key, "cc_mix")          == 0) { values_[kParamCCMix] = fv; }
        else if (std::strcmp(key, "cc_channel")      == 0) { values_[kParamCCChannel] = fv; }
        else if (std::strcmp(key, "attack_db")       == 0) { values_[kParamAttackDb] = fv; }
        else if (std::strcmp(key, "sustain_db")      == 0) { values_[kParamSustainDb] = fv; }
        else if (std::strcmp(key, "analyser_source") == 0) {
            values_[kAnalyzerSourceValue] = clampf(fv, 0.0f, 2.0f);
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());
        drawBackground(W, H);
        drawHeader(W);
        drawTabs();
        if (activeTab_ == 0) {
            drawAnalyser();
            drawProcessPanel();
        } else {
            drawDriftPanel();
        }
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
                if (item >= 0) commitDrop(item);
                closePopup();
                return true;
            }
            closePopup();
        }

        if (tabAt(mx, my) >= 0) {
            activeTab_ = tabAt(mx, my);
            dragSlider_ = -1;
            repaint();
            return true;
        }

        if (activeTab_ == 0) {
            if (sourceAt(mx, my) >= 0) {
                commitParam(kAnalyzerSourceValue, "analyser_source",
                            static_cast<float>(sourceAt(mx, my)));
                return true;
            }
            for (int s = 0; s < visibleSliderCount(); ++s) {
                if (sliderTrack(s).contains(mx, my)) {
                    dragSlider_ = s;
                    updateSlider(s, mx);
                    return true;
                }
            }
            if (segAt(mx, my) >= 0) {
                const Rect box = modeBox();
                const float t = clampf((mx - box.x) / box.w, 0.0f, 0.9999f);
                const int mode = std::min(2, static_cast<int>(std::floor(t * 3.0f)));
                dragSlider_ = -1;  // the row under the cursor changes with the mode
                commitParam(kParamMode, "mode", static_cast<float>(mode));
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
            return false;
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

    std::array<float, kUiValueCount> values_ {};
    std::array<float, spectrum::kSpectrumBins> spectrum_ {};
    bool demo_ = false;
    int activeTab_ = 0;
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

    [[nodiscard]] bool designerMode() const noexcept
    {
        return values_[kParamMode] >= 1.5f;
    }

    [[nodiscard]] int visibleSliderCount() const noexcept
    {
        return designerMode() ? static_cast<int>(kDesignerSliders.size())
                              : static_cast<int>(kMainSliders.size());
    }

    [[nodiscard]] const SliderDef& visibleSlider(const int s) const noexcept
    {
        return designerMode() ? kDesignerSliders[static_cast<std::size_t>(s)]
                              : kMainSliders[static_cast<std::size_t>(s)];
    }

    // Main controls are half their former track length, in two columns in the lower half of the panel.
    [[nodiscard]] Rect sliderTrack(const int s) const noexcept
    {
        constexpr float kW = 220.0f, kRowH = 42.0f;
        const int column = s < 5 ? 0 : 1;
        const int row = s < 5 ? s : s - 5;
        const float x = column == 0 ? 44.0f : 372.0f;
        const float y = 466.0f + static_cast<float>(row) * kRowH;
        return { x, y, kW, 12.0f };
    }

    [[nodiscard]] Rect modeBox() const noexcept { return { 44.0f, 408.0f, 300.0f, 26.0f }; }

    [[nodiscard]] Rect sourceBox() const noexcept { return { 392.0f, 150.0f, 276.0f, 24.0f }; }

    [[nodiscard]] Rect switchBox(int which) const noexcept
    {
        return { 540.0f, 404.0f + static_cast<float>(which) * 30.0f, 132.0f, 26.0f };
    }

    [[nodiscard]] Rect tabBox(int index) const noexcept
    {
        return { kPad + static_cast<float>(index) * 132.0f, kTabY, 132.0f, kTabH };
    }

    [[nodiscard]] int tabAt(float mx, float my) const noexcept
    {
        for (int i = 0; i < 2; ++i)
            if (tabBox(i).contains(mx, my)) return i;
        return -1;
    }

    [[nodiscard]] int segAt(float mx, float my) const noexcept
    {
        return modeBox().contains(mx, my) ? 0 : -1;
    }

    [[nodiscard]] int sourceAt(float mx, float my) const noexcept
    {
        if (!sourceBox().contains(mx, my)) return -1;
        const float n = clampf((mx - sourceBox().x) / sourceBox().w, 0.0f, 0.9999f);
        return std::min(2, static_cast<int>(std::floor(n * 3.0f)));
    }

    [[nodiscard]] int switchAt(float mx, float my) const noexcept
    {
        if (switchBox(0).contains(mx, my)) return 0;
        if (switchBox(1).contains(mx, my)) return 1;
        return -1;
    }

    [[nodiscard]] Rect ccDropBox(int d) const noexcept
    {
        return { 60.0f, 168.0f + static_cast<float>(d) * 74.0f, 360.0f, 26.0f };
    }

    [[nodiscard]] int dropHeaderAt(float mx, float my) const noexcept
    {
        if (activeTab_ != 1) return -1;
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
        const SliderDef& def = visibleSlider(s);
        const Rect tr        = sliderTrack(s);
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
        beginPath();
        fc(theme().background);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawPanel(const Rect& b, const char* title, const Accent& accent, const char* right = nullptr)
    {
        const auto& t = theme();
        beginPath();
        fc(t.surface);
        roundedRect(b.x, b.y, b.w, b.h, 4.0f);
        fill();
        closePath();
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        roundedRect(b.x, b.y, b.w, 28.0f, 4.0f);
        fill();
        rect(b.x, b.y + 18.0f, b.w, 10.0f);
        fill();
        closePath();
        fillColor(kCreamR, kCreamG, kCreamB, 255);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(b.x + 10.0f, b.y + 14.0f, title, nullptr);
        if (right) {
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(b.x + b.w - 10.0f, b.y + 14.0f, right, nullptr);
        }
    }

    void drawHeader(float W)
    {
        const auto& t = theme();
        beginPath();
        fc(t.panel);
        rect(0, 0, W, kHeaderH);
        fill();
        closePath();

        fontSize(24.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(kPad, 28.0f, "SPLIFF", nullptr);

        fontSize(11.0f);
        fc(t.textDim);
        text(kPad + 110.0f, 28.0f,
             "adaptive transient processor \xc2\xb7 cut / boost / designer \xc2\xb7 Drift CC", nullptr);

        themeRect_ = {W - 94.0f, 22.0f, 74.0f, 26.0f};
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

    void drawTabs()
    {
        const auto& t = theme();
        for (int i = 0; i < 2; ++i) {
            const Rect b = tabBox(i);
            const bool on = activeTab_ == i;
            beginPath();
            if (on) fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
            else fc(t.buttonFace);
            roundedRect(b.x, b.y, b.w, b.h, 3.0f);
            fill();
            closePath();
            fontSize(11.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            if (on) fillColor(kCreamR, kCreamG, kCreamB, 255);
            else fc(t.textDim);
            text(b.x + b.w * 0.5f, b.y + b.h * 0.5f, kTabNames[i], nullptr);
        }
    }

    [[nodiscard]] const Accent& sourceAccent() const noexcept
    {
        const int source = std::clamp(static_cast<int>(std::lround(values_[kAnalyzerSourceValue])), 0, 2);
        if (source == 0) return kInputAccent;
        if (source == 2) return kDeltaAccent;
        return kMainAccent;
    }

    void drawAnalyser()
    {
        const Accent& accent = sourceAccent();
        drawPanel(kAnalyserPanel, "SPECTRUM", accent,
                  "64 log bands \xc2\xb7 20 Hz - 20 kHz \xc2\xb7 input / output / delta");

        drawSegments(kAnalyzerSourceValue, kSourceNames, 3, sourceBox(), accent);

        const auto& t = theme();
        const Rect plot { kAnalyserPanel.x + 46.0f, kAnalyserPanel.y + 66.0f,
                          kAnalyserPanel.w - 76.0f, kAnalyserPanel.h - 96.0f };
        beginPath();
        fc(t.panel);
        roundedRect(plot.x, plot.y, plot.w, plot.h, 2.0f);
        fill();
        closePath();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(plot.x, plot.y, plot.w, plot.h, 2.0f);
        stroke();
        closePath();

        // dB grid.
        static constexpr int kDbMarks[] = { -80, -60, -40, -20, 0 };
        for (const int db : kDbMarks) {
            const float y = plot.y + (static_cast<float>(-db) / 90.0f) * plot.h;
            beginPath();
            sc(db == 0 ? t.textDim : t.border);
            strokeWidth(db == 0 ? 1.2f : 0.7f);
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            stroke();
            closePath();
            char buf[8];
            std::snprintf(buf, sizeof(buf), "%d", db);
            fc(t.textDisabled);
            fontSize(9.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(plot.x - 6.0f, y, buf, nullptr);
        }
        static constexpr float kFreqMarks[] = { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f };
        for (const float hz : kFreqMarks) {
            const float n = std::log(hz / 20.0f) / std::log(20000.0f / 20.0f);
            const float x = plot.x + n * plot.w;
            beginPath();
            sc(t.border);
            strokeWidth(0.7f);
            moveTo(x, plot.y);
            lineTo(x, plot.y + plot.h);
            stroke();
            closePath();
            char buf[16];
            if (hz >= 1000.0f) std::snprintf(buf, sizeof(buf), "%gk", hz / 1000.0f);
            else std::snprintf(buf, sizeof(buf), "%g", hz);
            fc(t.textDisabled);
            fontSize(9.0f);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            text(x, plot.y + plot.h + 4.0f, buf, nullptr);
        }

        // 64 log-spaced bars, matching Octet's analyser scale.
        const float barW = plot.w / static_cast<float>(spectrum::kSpectrumBins);
        for (int k = 0; k < spectrum::kSpectrumBins; ++k) {
            const float db = spectrum_[static_cast<std::size_t>(k)];
            const float n = clampf((db - spectrum::kSpectrumFloorDb) / (-spectrum::kSpectrumFloorDb), 0.0f, 1.0f);
            if (n <= 0.0f) continue;
            const float h = n * plot.h * 0.96f;
            beginPath();
            rect(plot.x + static_cast<float>(k) * barW + 0.5f,
                 plot.y + plot.h - h, std::max(1.0f, barW - 1.0f), h);
            fillColor(accent.r, accent.g, accent.b, 150);
            fill();
            closePath();
        }

        const int source = std::clamp(static_cast<int>(std::lround(values_[kAnalyzerSourceValue])), 0, 2);
        fillColor(accent.r, accent.g, accent.b, 255);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(kAnalyserPanel.x + 12.0f, kAnalyserPanel.y + kAnalyserPanel.h - 8.0f,
             source == 0 ? "Input: before transient shaping"
             : source == 1 ? "Output: after transient shaping"
                           : "Delta: what the processor added or removed", nullptr);
    }

    void drawProcessPanel()
    {
        drawPanel(kProcessPanel, "PARAMETERS", kMainAccent, "shorter controls \xc2\xb7 two columns");

        drawLabelValue(modeBox().x, modeBox().y - 14.0f, modeBox().w, "Mode",
                       kModeChoices[std::clamp(static_cast<int>(std::lround(values_[kParamMode])), 0, 2)]);
        drawSegments(kParamMode, kModeChoices, 3, modeBox(), kMainAccent);

        struct SwitchRow { uint32_t index; const char* label; const char* onText; const char* offText; };
        constexpr std::array<SwitchRow, 2> kSwitches = {{
            { kParamBypass, "Bypass", "bypassed", "active" },
            { kParamDelta,  "Delta",  "on",       "off"    },
        }};
        for (int i = 0; i < 2; ++i) {
            const auto& sw = kSwitches[static_cast<std::size_t>(i)];
            const Rect row = switchBox(i);
            drawSwitchRow(row, sw.label, values_[sw.index] >= 0.5f ? sw.onText : sw.offText,
                          values_[sw.index] >= 0.5f);
        }

        for (int s = 0; s < visibleSliderCount(); ++s) {
            const SliderDef& def = visibleSlider(s);
            const Rect tr = sliderTrack(s);
            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            drawSliderRow(tr, def.label, formatSlider(def, values_[def.index]).c_str(), norm);
        }

        const auto& t = theme();
        fc(t.textDisabled);
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(kProcessPanel.x + 12.0f, kProcessPanel.y + kProcessPanel.h - 16.0f,
             designerMode()
                 ? "Attack lifts onsets; Sustain lifts the body. Gain follows the fast/slow ratio."
                 : "Splits set the three detector bands. Cut reaches -30 dB; Boost reaches +18 dB.", nullptr);
    }

    void drawDriftPanel()
    {
        drawPanel(kDriftPanel, "DRIFT CC ROUTING", kCcAccent,
                  "Drift MIDI out \xe2\x86\x92 Spliff MIDI in");

        const auto& t = theme();
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
            text(box.x, box.y - 15.0f, def.label, nullptr);
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fc(isOff ? t.textDisabled : t.textPrimary);
            text(box.x + box.w, box.y - 15.0f, current, nullptr);
            drawDropBox(box, current);
        }

        const float x = 470.0f;
        fc(t.textDim);
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, 176.0f, "Drift's four lanes default to CC 1-4.", nullptr);
        text(x, 196.0f, "CC Depth / Sens / Decay shape Cut and Boost.", nullptr);
        text(x, 216.0f, "CC Mix applies in every mode.", nullptr);
        text(x, 250.0f, "off disables an override.", nullptr);
        text(x, 270.0f, "CC Channel selects which MIDI channel", nullptr);
        text(x, 290.0f, "Spliff listens on.", nullptr);
        fc(t.textDisabled);
        fontSize(9.5f);
        text(x, 330.0f, "The analyser source is stored with the patch.", nullptr);
        text(x, 346.0f, "Drift tab routing is persistent state too.", nullptr);
    }

    void drawLabelValue(float x, float y, float w, const char* label, const char* value)
    {
        const auto& t = theme();
        fontSize(10.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDim);
        text(x, y, label, nullptr);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(t.textPrimary);
        text(x + w, y, value, nullptr);
    }

    void drawSliderRow(const Rect& tr, const char* label, const char* value, float norm)
    {
        const auto& t = theme();
        drawLabelValue(tr.x, tr.y - 15.0f, tr.w, label, value);
        beginPath();
        fc(t.controlTrack);
        roundedRect(tr.x, tr.y, tr.w, tr.h, 6.0f);
        fill();
        closePath();
        if (norm > 0.0f) {
            beginPath();
            fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
            roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 6.0f);
            fill();
            closePath();
        }
    }

    void drawSegments(uint32_t index, const char* const* names, int count, const Rect& box, const Accent& accent)
    {
        const auto& t = theme();
        const int selected = std::clamp(static_cast<int>(std::lround(values_[index])), 0, count - 1);
        const float gap = 5.0f;
        const float w = (box.w - gap * static_cast<float>(count - 1)) / static_cast<float>(count);
        for (int i = 0; i < count; ++i) {
            const float x = box.x + static_cast<float>(i) * (w + gap);
            beginPath();
            if (i == selected) fillColor(accent.r, accent.g, accent.b, 255);
            else fc(t.buttonFace);
            roundedRect(x, box.y, w, box.h, 3.0f);
            fill();
            closePath();
            fontSize(10.5f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            if (i == selected) fillColor(kCreamR, kCreamG, kCreamB, 255);
            else fc(t.textPrimary);
            text(x + w * 0.5f, box.y + box.h * 0.5f, names[i], nullptr);
        }
    }

    void drawSwitchRow(const Rect& row, const char* label, const char* state, bool on)
    {
        const auto& t = theme();
        fontSize(10.5f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(row.x, row.y + row.h * 0.5f, label, nullptr);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(row.x + row.w - 48.0f, row.y + row.h * 0.5f, state, nullptr);

        const float x = row.x + row.w - 42.0f;
        const float y = row.y + (row.h - 18.0f) * 0.5f;
        beginPath();
        roundedRect(x, y, 42.0f, 18.0f, 9.0f);
        if (on) fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
        else fc(t.controlTrack);
        fill();
        closePath();
        beginPath();
        circle(x + (on ? 31.0f : 10.0f), y + 9.0f, 6.0f);
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

    [[nodiscard]] const Accent& popupAccent() const noexcept { return kCcAccent; }

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
                    if (selected) fillColor(accent.r, accent.g, accent.b, 255);
                    else fillColor(accent.r, accent.g, accent.b, 90);
                    fill();
                    closePath();
                }
                char itemText[32];
                dropItemText(kind, item, itemText, sizeof(itemText));
                fontSize(11.0f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (selected) fillColor(kCreamR, kCreamG, kCreamB, 255);
                else fc(t.textPrimary);
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
            const float thumbY = trackY + (trackH - thumbH) * dropScroll_ / std::max(1, count - rows);
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
