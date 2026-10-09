#include "DistrhoUI.hpp"
#include "downspout/look_and_feel.hpp"

#include "skream_core.hpp"
#include "skream_presets.hpp"

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

// Must match SkreemPlugin.cpp exactly — indices are stable across saves.
enum ParameterIndex : uint32_t {
    kParamInputGain = 0,
    kParamCutoff,
    kParamScream,
    kParamResonance,
    kParamMix,
    kParamOutputGain,
    kParamTrack,
    kParamCCCutoff,
    kParamCCScream,
    kParamCCChannel,
    kParamMorph,
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

constexpr std::array<SliderDef, 8> kMainSliders = {{
    { kParamInputGain,  "Input Gain",  -24.0f,  24.0f, false, "input_gain"  },
    { kParamCutoff,     "Cutoff",        0.0f, 100.0f, false, "cutoff"      },
    { kParamMorph,      "Morph LP-HP", -100.0f, 100.0f, false, "morph"      },
    { kParamScream,     "Scream",        0.0f, 100.0f, false, "scream"      },
    { kParamResonance,  "Resonance",     0.0f, 100.0f, false, "resonance"   },
    { kParamMix,        "Mix",           0.0f, 100.0f, false, "mix"         },
    { kParamOutputGain, "Output Gain", -24.0f,   0.0f, false, "output_gain" },
    { kParamTrack,      "Track",         0.0f, 100.0f, false, "track"       },
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

constexpr std::array<DropDef, 3> kCCDropDefs = {{
    { kParamCCCutoff,  "CC Cutoff",  "cc_cutoff",  DropKind::CcNumber },
    { kParamCCScream,  "CC Scream",  "cc_scream",  DropKind::CcNumber },
    { kParamCCChannel, "CC Channel", "cc_channel", DropKind::Channel  },
}};

struct Accent {
    int r;
    int g;
    int b;
};

// Section accents from the magneto palette: hot metal for the scream,
// cold steel for CC routing plumbing.
constexpr Accent kMainAccent {176, 88, 62};
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

[[nodiscard]] std::string formatSliderValue(const SliderDef& def, float v)
{
    char buf[32];
    if (def.index == kParamInputGain || def.index == kParamOutputGain)
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
    else if (def.index == kParamMorph && std::fabs(v) < 0.5f)
        std::snprintf(buf, sizeof(buf), "off");
    else if (def.index == kParamMorph)
        std::snprintf(buf, sizeof(buf), "%+.0f%%", v);
    else
        std::snprintf(buf, sizeof(buf), "%.1f%%", v);
    return buf;
}

[[nodiscard]] int dropItemCount(DropKind kind) noexcept
{
    switch (kind) {
    case DropKind::Channel:  return 16;
    case DropKind::CcNumber: return 129;
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
    case DropKind::CcNumber: return static_cast<float>(item);
    }
    return 0.0f;
}

constexpr float kPad      = 16.0f;
constexpr float kDivX     = 470.0f;
constexpr float kMainX    = kPad;
constexpr float kCCX      = kDivX + 12.0f;
constexpr float kSlidersY = 76.0f;

constexpr float kSliderH = 52.0f;
constexpr float kDropH   = 48.0f;

constexpr float kItemH       = 20.0f;
constexpr int   kMaxRows     = 10;
constexpr int   kTwoColLimit = 20;

}  // namespace

class SkreemUI : public UI
{
public:
    SkreemUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        values_[kParamInputGain]  = 0.0f;
        values_[kParamCutoff]     = 85.0f;
        values_[kParamScream]     = 46.5f;
        values_[kParamResonance]  = 100.0f;
        values_[kParamMix]        = 100.0f;
        values_[kParamOutputGain] = -6.0f;
        values_[kParamTrack]      = 0.0f;
        values_[kParamCCCutoff]   = 1.0f;
        values_[kParamCCScream]   = 2.0f;
        values_[kParamCCChannel]  = 1.0f;
        values_[kParamMorph]      = 0.0f;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(uint32_t /*index*/, float /*value*/) override
    {
        // Values arrive via stateChanged; ignore host automation replay.
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (!value) return;
        const float f = static_cast<float>(std::atof(value));
        if      (std::strcmp(key, "input_gain")  == 0) { values_[kParamInputGain]  = f; }
        else if (std::strcmp(key, "cutoff")      == 0) { values_[kParamCutoff]     = f; }
        else if (std::strcmp(key, "scream")      == 0) { values_[kParamScream]     = f; }
        else if (std::strcmp(key, "resonance")   == 0) { values_[kParamResonance]  = f; }
        else if (std::strcmp(key, "mix")         == 0) { values_[kParamMix]        = f; }
        else if (std::strcmp(key, "output_gain") == 0) { values_[kParamOutputGain] = f; }
        else if (std::strcmp(key, "track")       == 0) { values_[kParamTrack]      = f; }
        else if (std::strcmp(key, "cc_cutoff")   == 0) { values_[kParamCCCutoff]   = f; }
        else if (std::strcmp(key, "cc_scream")   == 0) { values_[kParamCCScream]   = f; }
        else if (std::strcmp(key, "cc_channel")  == 0) { values_[kParamCCChannel]  = f; }
        else if (std::strcmp(key, "morph")       == 0) { values_[kParamMorph]      = f; }
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
        if (presetOpen_ || ccOpen_ >= 0)
            drawPopup(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1) return false;
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        const float W  = static_cast<float>(getWidth());
        if (!ev.press) {
            dragSlider_ = -1;
            return false;
        }

        if (themeRect_.contains(mx, my)) {
            darkTheme_ = !darkTheme_;
            closePopups();
            repaint();
            return true;
        }

        if (presetButtonRect(W).contains(mx, my)) {
            const bool was = presetOpen_;
            closePopups();
            presetOpen_ = !was;
            repaint();
            return true;
        }

        if (popupOpen()) {
            const Rect popup = popupRect(W, static_cast<float>(getHeight()));
            if (popup.contains(mx, my)) {
                if (presetOpen_) {
                    const int item = presetItemAt(mx, my, popup, W);
                    if (item >= 0)
                        loadPreset(item);
                } else {
                    const int item = ccItemAt(mx, my, popup);
                    if (item >= 0)
                        commitDrop(item);
                }
                closePopups();
                return true;
            }
            closePopups();
            if (dropHeaderAt(mx, my) < 0) {
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
            const int item = presetOpen_
                ? presetItemAt(mx, my, popup, static_cast<float>(getWidth()))
                : ccItemAt(mx, my, popup);
            if (item != popupHover_) { popupHover_ = item; repaint(); }
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (presetOpen_ || ccOpen_ < 0) return false;
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

    std::array<float, kParameterCount> values_{};
    int  currentPreset_ = 0;
    bool presetOpen_ = false;
    int  ccOpen_ = -1;  // index into kCCDropDefs, or -1
    int  dropScroll_ = 0;
    int  popupHover_ = -1;
    int  dragSlider_ = -1;

    [[nodiscard]] bool popupOpen() const noexcept { return presetOpen_ || ccOpen_ >= 0; }

    [[nodiscard]] const DropDef& openDef() const noexcept
    {
        return kCCDropDefs[static_cast<std::size_t>(ccOpen_)];
    }

    [[nodiscard]] DropKind openKind() const noexcept { return openDef().kind; }

    [[nodiscard]] Rect mainTrack(int s) const noexcept
    {
        const float trackW = kDivX - kMainX - kPad;
        return { kMainX, kSlidersY + s * kSliderH + 22.0f, trackW, 14.0f };
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

    [[nodiscard]] Rect presetButtonRect(float W) const noexcept
    {
        return { W - 316.0f, 16.0f, 210.0f, 26.0f };
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
        closePopups();
        ccOpen_ = d;
        const DropDef& def = kCCDropDefs[static_cast<std::size_t>(d)];
        dropScroll_ = std::max(0, dropCurrentItem(def.kind, values_[def.index]) - kMaxRows / 2);
        popupHover_ = -1;
        repaint();
    }

    void closePopups() noexcept
    {
        presetOpen_ = false;
        ccOpen_ = -1;
        dropScroll_ = 0;
        popupHover_ = -1;
    }

    void commitDrop(int item)
    {
        const DropDef& def = openDef();
        commitParam(def.index, def.stateKey, dropItemValue(def.kind, item));
    }

    void loadPreset(int i)
    {
        using namespace downspout::skream;
        currentPreset_ = i;
        const Parameters& p = kPresets[i].params;

        commitParam(kParamInputGain,  "input_gain",  p.inputGain);
        commitParam(kParamCutoff,     "cutoff",      p.cutoff);
        commitParam(kParamScream,     "scream",      p.scream);
        commitParam(kParamResonance,  "resonance",   p.resonance);
        commitParam(kParamMix,        "mix",         p.mix);
        commitParam(kParamOutputGain, "output_gain", p.outputGain);
        commitParam(kParamTrack,      "track",       p.track);
        commitParam(kParamMorph,      "morph",       p.morph);
    }

    [[nodiscard]] bool dropScrollable() const noexcept
    {
        return !presetOpen_ && dropItemCount(openKind()) > kTwoColLimit;
    }

    [[nodiscard]] int popupRows() const noexcept
    {
        if (presetOpen_)
            return downspout::skream::kPresetCount;
        const int count = dropItemCount(openKind());
        if (dropScrollable())
            return kMaxRows;
        return (count + 1) / 2;
    }

    [[nodiscard]] int popupCols() const noexcept
    {
        if (presetOpen_ || dropScrollable())
            return 1;
        return 2;
    }

    [[nodiscard]] Rect openHeaderBox(float W) const noexcept
    {
        if (presetOpen_)
            return presetButtonRect(W);
        return ccDropBox(ccOpen_);
    }

    [[nodiscard]] Rect popupRect(float W, float H) const noexcept
    {
        const Rect header = openHeaderBox(W);
        const float popH = popupRows() * kItemH + 8.0f;
        const float popW = std::max(header.w, 190.0f);
        float x = std::min(header.x, W - popW - 8.0f);
        float y = header.y + header.h + 3.0f;
        if (y + popH > H - 8.0f)
            y = header.y - 3.0f - popH;
        return { x, y, popW, popH };
    }

    [[nodiscard]] int ccItemAt(float mx, float my, const Rect& popup) const noexcept
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
        const int rows = popupRows();
        if (row < 0 || row >= rows) return -1;
        const int col = static_cast<int>((mx - listX) / (listW / 2));
        if (col < 0 || col > 1) return -1;
        const int item = row * 2 + col;
        return item < count ? item : -1;
    }

    [[nodiscard]] int presetItemAt(float mx, float my, const Rect& popup, float W) const noexcept
    {
        (void)W;
        const float listX = popup.x + 4.0f;
        const float listY = popup.y + 4.0f;
        if (mx < listX || mx > listX + popup.w - 8.0f || my < listY)
            return -1;
        const int row = static_cast<int>((my - listY) / kItemH);
        if (row < 0 || row >= downspout::skream::kPresetCount) return -1;
        return row;
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
        text(kPad, 27.0f, "SKREAM", nullptr);

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(kPad + 125.0f, 27.0f, "scream filter  |  MIDI CC via Drift", nullptr);

        beginPath();
        sc(t.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, 58.0f);
        lineTo(W, 58.0f);
        stroke();
        closePath();

        drawColumnStrip(kMainX, kDivX - kMainX - kPad, "PARAMETERS", kMainAccent);
        drawColumnStrip(kCCX, W - kCCX - kPad, "DRIFT CC ROUTING", kCcAccent);

        drawPresetButton(W);

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

    void drawPresetButton(float W)
    {
        const auto& t = theme();
        const Rect r = presetButtonRect(W);

        beginPath();
        roundedRect(r.x, r.y, r.w, r.h, 4.0f);
        fc(t.buttonFace);
        fill();
        closePath();
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        roundedRect(r.x + 0.5f, r.y + 0.5f, r.w - 1.0f, r.h - 1.0f, 4.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(r.x + 8.0f, r.y + r.h * 0.5f,
             downspout::skream::kPresets[currentPreset_].name, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(r.x + r.w - 8.0f, r.y + r.h * 0.5f, presetOpen_ ? "\xe2\x96\xb2" : "\xe2\x96\xbe", nullptr);
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

    void drawMainColumn()
    {
        const auto& t = theme();
        const float trackW = kDivX - kMainX - kPad;

        for (int s = 0; s < static_cast<int>(kMainSliders.size()); ++s) {
            const SliderDef& def = kMainSliders[static_cast<std::size_t>(s)];
            const Rect tr        = mainTrack(s);
            drawLabelValue(kMainX, tr.y - 18.0f, trackW, def.label,
                           formatSliderValue(def, values_[def.index]).c_str());
            const float norm = clampf((values_[def.index] - def.min) / (def.max - def.min), 0.0f, 1.0f);
            beginPath();
            roundedRect(tr.x, tr.y, tr.w, tr.h, 7.0f);
            fc(t.controlTrack);
            fill();
            closePath();
            if (def.index == kParamMorph) {
                // Bipolar: the fill grows from the centre, a tick marks 0 (off).
                const float mid = tr.x + tr.w * 0.5f;
                const float end = tr.x + tr.w * norm;
                const float x0  = std::min(mid, end);
                const float w0  = std::max(std::fabs(end - mid), 0.0f);
                if (w0 > 0.5f) {
                    beginPath();
                    roundedRect(x0, tr.y, w0, tr.h, 4.0f);
                    fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
                    fill();
                    closePath();
                }
                beginPath();
                rect(mid - 0.5f, tr.y - 2.0f, 1.0f, tr.h + 4.0f);
                fc(t.textDim);
                fill();
                closePath();
            } else if (norm > 0.0f) {
                beginPath();
                roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 7.0f);
                fillColor(kMainAccent.r, kMainAccent.g, kMainAccent.b, 255);
                fill();
                closePath();
            }
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
        text(kCCX, hintY, "Route Drift MIDI out \xe2\x86\x92 Skream MIDI in.", nullptr);
        text(kCCX, hintY + 14.0f, "off disables the override for that control.", nullptr);
        text(kCCX, hintY + 38.0f, "Defaults: 1=Cutoff  2=Scream.", nullptr);
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

    void drawFooterNote()
    {
        const auto& t = theme();
        const float y = kSlidersY + kMainSliders.size() * kSliderH + 12.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDisabled);
        text(kMainX, y, "Scream is HP feedback cutoff \xc2\xb7 Track locks harmonics.", nullptr);
        text(kMainX, y + 14.0f, "Cut before compressors \xc2\xb7 mind the resonance.", nullptr);
        text(kMainX, y + 28.0f, "Morph +: low-pass turns high-pass as Cutoff rises \xc2\xb7 \xe2\x88\x92: the reverse.", nullptr);
    }

    void drawPopup(float W, float H)
    {
        const auto& t = theme();
        const Rect popup = popupRect(W, H);
        const int rows = popupRows();
        const int cols = popupCols();
        const Accent& accent = presetOpen_ ? kMainAccent : kCcAccent;

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

        const bool scrollable = !presetOpen_ && dropScrollable();
        const float listX = popup.x + 4.0f;
        const float listY = popup.y + 4.0f;
        const float listW = popup.w - 8.0f - (scrollable ? 10.0f : 0.0f);
        const int top = scrollable ? dropScroll_ : 0;
        const int current = presetOpen_
            ? currentPreset_
            : dropCurrentItem(openKind(), values_[openDef().index]);

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int item = scrollable ? top + r : r * cols + c;
                const int count = presetOpen_
                    ? downspout::skream::kPresetCount
                    : dropItemCount(openKind());
                if (item >= count) continue;
                const float cw = listW / cols;
                const float cx = listX + c * cw;
                const float cy = listY + r * kItemH;
                const bool hovered = item == popupHover_;
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
                fontSize(11.0f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (selected)
                    fillColor(kCreamR, kCreamG, kCreamB, 255);
                else
                    fc(t.textPrimary);
                if (presetOpen_)
                    text(cx + 8.0f, cy + kItemH * 0.5f,
                         downspout::skream::kPresets[item].name, nullptr);
                else {
                    char itemText[32];
                    dropItemText(openKind(), item, itemText, sizeof(itemText));
                    text(cx + 8.0f, cy + kItemH * 0.5f, itemText, nullptr);
                }
            }
        }

        if (scrollable) {
            const float trackX = popup.x + popup.w - 10.0f;
            const float trackY = listY;
            const float trackH = rows * kItemH;
            const int count = dropItemCount(openKind());
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkreemUI)
};

UI* createUI()
{
    return new SkreemUI();
}

END_NAMESPACE_DISTRHO
