#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "sinevox_core_types.hpp"
#include "sinevox_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::sinevox::ParamId;
using downspout::sinevox::ParamSpec;
using downspout::sinevox::kMaxFormants;
using downspout::sinevox::kOutAmpCcBase;
using downspout::sinevox::kOutFreqCcBase;
using downspout::sinevox::kOutFreqMaxHz;
using downspout::sinevox::kOutFreqMinHz;
using downspout::sinevox::kParameterCount;
using downspout::sinevox::kParameterSpecs;
using downspout::sinevox::index;

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

struct Accent {
    int r;
    int g;
    int b;
};

// Same palette family as magneto and voxmod: cold steel for analysis, amber for
// synthesis, violet for the MIDI column, olive for readouts.
constexpr Accent kAnalysisAccent { 92, 140, 156 };
constexpr Accent kSynthAccent { 198, 132, 58 };
constexpr Accent kMidiAccent { 138, 116, 168 };
constexpr Accent kReadoutAccent { 124, 148, 104 };
constexpr int kCreamR = 250;
constexpr int kCreamG = 248;
constexpr int kCreamB = 242;

// One colour per formant slot, used by the plot, the readout table and the
// MIDI notes, so a formant is the same colour wherever it appears.
constexpr std::array<Accent, kMaxFormants> kFormantColours = {{
    { 214, 138, 52 }, { 92, 140, 156 }, { 138, 116, 168 }, { 124, 148, 104 },
    { 196, 92, 92 },  { 62, 152, 170 }, { 176, 148, 66 },  { 160, 104, 140 },
}};

[[nodiscard]] float clampf(const float v, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

// ── Sliders ────────────────────────────────────────────────────────────────

enum SliderId : std::uint32_t {
    kSliderGate = 0,
    kSliderBwLimit,
    kSliderGlide,
    kSliderAttack,
    kSliderRelease,
    kSliderTilt,
    kSliderLevel,
    kSliderSpeech,
    kSliderCarrierNorm,
    kSliderSpread,
    kSliderCount,
};

constexpr std::size_t kAnalysisSliders = 5;

constexpr std::array<std::uint32_t, kSliderCount> kSliderParams = {
    index(ParamId::gateDb),
    index(ParamId::bwLimit),
    index(ParamId::glideMs),
    index(ParamId::attackMs),
    index(ParamId::releaseMs),
    index(ParamId::tilt),
    index(ParamId::levelDb),
    index(ParamId::dry),
    index(ParamId::carrierNorm),
    index(ParamId::spread),
};

constexpr std::array<const char*, kSliderCount> kSliderLabels = {
    "Gate", "BW Limit", "Glide", "Attack", "Release",
    "Tilt", "Level", "Speech Mix", "Carrier Norm", "Sine Spread",
};

[[nodiscard]] bool sliderIsLogarithmic(const std::uint32_t slider) noexcept
{
    return slider == kSliderBwLimit || slider == kSliderGlide
        || slider == kSliderAttack || slider == kSliderRelease;
}

[[nodiscard]] float sliderToValue(const std::uint32_t slider, const float t) noexcept
{
    const ParamSpec& spec = kParameterSpecs[kSliderParams[slider]];
    if (!sliderIsLogarithmic(slider))
        return spec.minimum + t * (spec.maximum - spec.minimum);
    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return std::exp(lo + t * (hi - lo));
}

[[nodiscard]] float valueToSlider(const std::uint32_t slider, const float value) noexcept
{
    const ParamSpec& spec = kParameterSpecs[kSliderParams[slider]];
    if (!sliderIsLogarithmic(slider))
        return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);
    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return clampf((std::log(std::max(value, spec.minimum)) - lo) / (hi - lo), 0.0f, 1.0f);
}

[[nodiscard]] std::string formatSlider(const std::uint32_t slider, const float value)
{
    char buf[48];
    const double v = value;
    switch (slider) {
    case kSliderGate:
        std::snprintf(buf, sizeof(buf), "%.0f dB", v);
        break;
    case kSliderBwLimit:
        std::snprintf(buf, sizeof(buf), "%.0f Hz", v);
        break;
    case kSliderGlide:
    case kSliderRelease:
        std::snprintf(buf, sizeof(buf), "%.0f ms", v);
        break;
    case kSliderAttack:
        std::snprintf(buf, sizeof(buf), "%.1f ms", v);
        break;
    case kSliderTilt:
        std::snprintf(buf, sizeof(buf), "%+.1f dB/oct", v);
        break;
    case kSliderLevel:
        std::snprintf(buf, sizeof(buf), "%+.1f dB", v);
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
        break;
    }
    return buf;
}

// ── x-y pad ────────────────────────────────────────────────────────────────
//
// Shift and Q Scale are the two controls explored by ear, so they share a pad:
// X transposes every formant, Y narrows or widens every band. The crosshair
// marks the neutral point (no shift, Q x1).

struct XYAxis {
    std::uint32_t param;
    float minimum;
    float maximum;
    bool logarithmic;
    const char* label;
};

constexpr XYAxis kXAxis = { index(ParamId::shift), -12.0f, 12.0f, false, "Shift" };
constexpr XYAxis kYAxis = { index(ParamId::qScale), 0.25f, 4.0f, true, "Q Scale" };

[[nodiscard]] float axisNorm(const XYAxis& axis, const float value) noexcept
{
    if (!axis.logarithmic)
        return clampf((value - axis.minimum) / (axis.maximum - axis.minimum), 0.0f, 1.0f);
    const float lo = std::log(axis.minimum);
    const float hi = std::log(axis.maximum);
    return clampf((std::log(clampf(value, axis.minimum, axis.maximum)) - lo) / (hi - lo), 0.0f, 1.0f);
}

[[nodiscard]] float axisValue(const XYAxis& axis, const float t) noexcept
{
    if (!axis.logarithmic)
        return axis.minimum + t * (axis.maximum - axis.minimum);
    return std::exp(std::log(axis.minimum) + t * (std::log(axis.maximum) - std::log(axis.minimum)));
}

// ── Dropdowns ──────────────────────────────────────────────────────────────

enum DropKind {
    kDropCc = 0,
    kDropChannel,
    kDropCount,
    kDropToggle,
    kDropMidiOut,
};

struct DropDef {
    std::uint32_t index;
    const char* label;
    int kind;
};

constexpr std::array<DropDef, 10> kDropDefs = {{
    { index(ParamId::formants), "Formants", kDropCount },
    { index(ParamId::noteShift), "Note Shift", kDropToggle },
    { index(ParamId::midiOut), "MIDI Out", kDropMidiOut },
    { index(ParamId::outChannel), "Out Channel", kDropChannel },
    { index(ParamId::ccChannel), "In Channel", kDropChannel },
    { index(ParamId::ccShift), "CC Shift", kDropCc },
    { index(ParamId::ccQ), "CC Q Scale", kDropCc },
    { index(ParamId::ccFormants), "CC Formants", kDropCc },
    { index(ParamId::ccTilt), "CC Tilt", kDropCc },
    { index(ParamId::ccMode), "CC Mode", kDropCc },
}};

constexpr const char* ccCommonName(const int cc)
{
    switch (cc) {
    case 1: return "Mod";
    case 2: return "Breath";
    case 7: return "Volume";
    case 10: return "Pan";
    case 11: return "Expr";
    case 74: return "Bright";
    default: return nullptr;
    }
}

constexpr const char* midiOutName(const int item)
{
    switch (item) {
    case 1: return "formant CCs";
    case 2: return "formant notes";
    default: return "off";
    }
}

constexpr const char* modeName(const int item)
{
    switch (item) {
    case 1: return "SINE SPEECH";
    case 2: return "BOTH";
    default: return "VOCODER";
    }
}

[[nodiscard]] int dropItemCount(const DropDef& def) noexcept
{
    switch (def.kind) {
    case kDropChannel: return 16;
    case kDropCount: return 8;
    case kDropToggle: return 2;
    case kDropMidiOut: return 3;
    case kDropCc:
    default: return 129;
    }
}

void dropItemText(const DropDef& def, const int item, char* buf, const std::size_t size)
{
    switch (def.kind) {
    case kDropChannel:
        std::snprintf(buf, size, "Ch %d", item + 1);
        return;
    case kDropCount:
        std::snprintf(buf, size, item == 0 ? "1 formant" : "%d formants", item + 1);
        return;
    case kDropToggle:
        std::snprintf(buf, size, "%s", item == 1 ? "held note shifts" : "off");
        return;
    case kDropMidiOut:
        std::snprintf(buf, size, "%s", midiOutName(item));
        return;
    case kDropCc:
    default:
        if (item == 0) {
            std::snprintf(buf, size, "off");
            return;
        }
        if (const char* common = ccCommonName(item))
            std::snprintf(buf, size, "%d %s", item, common);
        else
            std::snprintf(buf, size, "CC %d", item);
        return;
    }
}

[[nodiscard]] int dropCurrentItem(const DropDef& def, const float value) noexcept
{
    const int maxItem = dropItemCount(def) - 1;
    const int rounded = static_cast<int>(std::lround(value));
    if (def.kind == kDropChannel || def.kind == kDropCount)
        return std::clamp(rounded - 1, 0, maxItem);
    return std::clamp(rounded, 0, maxItem);
}

[[nodiscard]] float dropItemValue(const DropDef& def, const int item) noexcept
{
    if (def.kind == kDropChannel || def.kind == kDropCount)
        return static_cast<float>(item + 1);
    return static_cast<float>(item);
}

// ── Layout ─────────────────────────────────────────────────────────────────

constexpr float kPad = 16.0f;
constexpr float kHeaderH = 58.0f;
constexpr float kStripH = 18.0f;

constexpr float kPlotY = 90.0f;
constexpr float kPlotH = 170.0f;
constexpr float kTableY = kPlotY + kPlotH + 8.0f;
constexpr float kTableRowH = 20.0f;

constexpr float kBodyY = 346.0f;
constexpr float kStripY = kBodyY - 30.0f;
constexpr float kColW = 176.0f;
constexpr float kAnalysisX = kPad;
constexpr float kSynthX = kAnalysisX + kColW + 14.0f;
constexpr float kMidiX = kSynthX + kColW + 14.0f;
constexpr float kSubGap = 16.0f;
constexpr float kSliderH = 44.0f;
constexpr float kDropH = 44.0f;
constexpr int kDropRows = 5;
constexpr float kItemH = 20.0f;
constexpr int kMaxRows = 10;
constexpr int kTwoColLimit = 20;
constexpr float kTrackH = 12.0f;

constexpr float kLowerY = kBodyY + 5.0f * kSliderH + 18.0f;
constexpr float kPadW = kColW;
constexpr float kPadH = 150.0f;

// Plot axis range. Matches the MIDI output mapping so what is drawn is what is sent.
constexpr float kPlotMinHz = kOutFreqMinHz;
constexpr float kPlotMaxHz = kOutFreqMaxHz;
constexpr float kPlotMinDb = -80.0f;
constexpr float kPlotMaxDb = -10.0f;

}  // namespace

class SinevoxUI : public UI
{
public:
    SinevoxUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;

        // Catalog screenshots are captured from the standalone with no audio, which
        // would show an empty plot. The capture script sets this so the panel shows
        // a vowel; it is never set in a host.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr)
            loadDemo();

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const std::uint32_t idx, const float value) override
    {
        if (idx >= values_.size())
            return;
        if (demo_ && idx >= downspout::sinevox::kInputParameterCount)
            return;  // keep the demo vowel on screen
        values_[idx] = value;
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        drawBackground(W, H);
        drawHeader(W);
        drawColumnStrips(W);
        drawFormantPlot(W);
        drawFormantTable(W);
        drawSliders();
        drawSwitchRow(bypassBox(), "Bypass", values_[index(ParamId::bypass)] >= 0.5f,
                      "speech passes", "processing");
        drawPad();
        drawInputStatus();
        drawMidiColumn();
        if (popupOpen())
            drawPopup(W, H);
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        const float mx = ev.pos.getX();
        const float my = ev.pos.getY();

        if (!ev.press) {
            dragSlider_ = -1;
            draggingPad_ = false;
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
            if (dropHeaderAt(mx, my) < 0) {
                for (std::uint32_t s = 0; s < kSliderCount; ++s) {
                    if (sliderTrack(s).contains(mx, my))
                        return true;
                }
                return false;
            }
        }

        for (int m = 0; m < 3; ++m) {
            if (modeRect(m).contains(mx, my)) {
                commitParameter(index(ParamId::mode), static_cast<float>(m));
                return true;
            }
        }

        if (bypassBox().contains(mx, my)) {
            commitParameter(index(ParamId::bypass), values_[index(ParamId::bypass)] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }

        if (padBox().contains(mx, my)) {
            draggingPad_ = true;
            updatePad(mx, my);
            return true;
        }

        for (std::uint32_t s = 0; s < kSliderCount; ++s) {
            if (sliderTrack(s).contains(mx, my)) {
                dragSlider_ = static_cast<int>(s);
                updateSlider(s, mx);
                return true;
            }
        }

        const int header = dropHeaderAt(mx, my);
        if (header >= 0) {
            openDrop(header);
            return true;
        }

        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float mx = ev.pos.getX();
        const float my = ev.pos.getY();

        if (draggingPad_) {
            updatePad(mx, my);
            return true;
        }
        if (dragSlider_ >= 0) {
            updateSlider(static_cast<std::uint32_t>(dragSlider_), mx);
            return true;
        }
        if (popupOpen()) {
            const Rect popup = popupRect(static_cast<float>(getWidth()),
                                         static_cast<float>(getHeight()));
            const int item = popupItemAt(mx, my, popup);
            if (item != dropHover_) {
                dropHover_ = item;
                repaint();
            }
            return true;
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!popupOpen() || !dropScrollable())
            return false;
        const Rect popup = popupRect(static_cast<float>(getWidth()),
                                     static_cast<float>(getHeight()));
        if (!popup.contains(ev.pos.getX(), ev.pos.getY()))
            return false;

        const int count = dropItemCount(openDef());
        dropScroll_ = std::clamp(dropScroll_ - static_cast<int>(std::lround(ev.delta.getY() * 3.0)),
                                 0, count - kMaxRows);
        repaint();
        return true;
    }

private:
    [[nodiscard]] const laf::Theme& theme() const noexcept
    {
        return darkTheme_ ? laf::kDarkTheme : laf::kLightTheme;
    }
    bool darkTheme_ = false;  // magneto default: light test-equipment panel
    Rect themeRect_ {};

    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }
    void fc(const Accent& a, const int alpha = 255) { fillColor(a.r, a.g, a.b, alpha); }
    void sc(const Accent& a, const int alpha = 255) { strokeColor(a.r, a.g, a.b, alpha); }

    std::array<float, kParameterCount> values_ {};
    bool demo_ = false;

    void loadDemo()
    {
        demo_ = true;
        constexpr std::array<float, 4> hz = { 690.0f, 1210.0f, 2590.0f, 3480.0f };
        constexpr std::array<float, 4> db = { -17.0f, -23.0f, -34.0f, -45.0f };
        constexpr std::array<float, 4> q = { 8.5f, 11.0f, 15.0f, 20.0f };
        for (std::size_t i = 0; i < hz.size(); ++i) {
            values_[index(ParamId::outFreq1) + i] = hz[i];
            values_[index(ParamId::outAmp1) + i] = db[i];
            values_[index(ParamId::outQ1) + i] = q[i];
        }
        values_[index(ParamId::outLevelDb)] = -21.0f;
        values_[index(ParamId::outActive)] = 4.0f;
    }
    int dragSlider_ = -1;
    bool draggingPad_ = false;

    int openDrop_ = -1;
    int dropScroll_ = 0;
    int dropHover_ = -1;

    [[nodiscard]] bool popupOpen() const noexcept { return openDrop_ >= 0; }
    [[nodiscard]] const DropDef& openDef() const noexcept
    {
        return kDropDefs[static_cast<std::size_t>(openDrop_)];
    }

    [[nodiscard]] float value(const ParamId id) const noexcept { return values_[index(id)]; }

    [[nodiscard]] int formantCount() const noexcept
    {
        return std::clamp(static_cast<int>(std::lround(value(ParamId::formants))), 1, kMaxFormants);
    }

    // ── Hit geometry ─────────────────────────────────────────────────────

    [[nodiscard]] float sliderColX(const std::uint32_t slider) const noexcept
    {
        return slider < kAnalysisSliders ? kAnalysisX : kSynthX;
    }

    [[nodiscard]] Rect sliderTrack(const std::uint32_t slider) const noexcept
    {
        const std::uint32_t row = slider < kAnalysisSliders ? slider : slider - kAnalysisSliders;
        return { sliderColX(slider), kBodyY + static_cast<float>(row) * kSliderH + 19.0f,
                 kColW, kTrackH };
    }

    [[nodiscard]] Rect bypassBox() const noexcept
    {
        return { kSynthX, kBodyY + 5.0f * kSliderH + 2.0f, kColW, 32.0f };
    }

    [[nodiscard]] Rect padBox() const noexcept
    {
        return { kAnalysisX, kLowerY + 16.0f, kPadW, kPadH };
    }

    [[nodiscard]] Rect modeRect(const int mode) const noexcept
    {
        const float W = static_cast<float>(getWidth());
        const float w = 104.0f;
        const float x0 = W - 90.0f - 14.0f - 3.0f * w - 8.0f;
        return { x0 + static_cast<float>(mode) * (w + 4.0f), 16.0f, w, 26.0f };
    }

    [[nodiscard]] float subColW(const float W) const noexcept
    {
        return (W - kMidiX - kPad - kSubGap) * 0.5f;
    }

    [[nodiscard]] Rect dropBox(const std::uint32_t drop) const noexcept
    {
        const float W = static_cast<float>(getWidth());
        const float w = subColW(W);
        const std::uint32_t col = drop / kDropRows;
        const std::uint32_t row = drop % kDropRows;
        return { kMidiX + static_cast<float>(col) * (w + kSubGap),
                 kBodyY + static_cast<float>(row) * kDropH + 19.0f, w, 22.0f };
    }

    [[nodiscard]] int dropHeaderAt(const float mx, const float my) const noexcept
    {
        for (std::uint32_t d = 0; d < kDropDefs.size(); ++d) {
            if (dropBox(d).contains(mx, my))
                return static_cast<int>(d);
        }
        return -1;
    }

    // ── Commit ───────────────────────────────────────────────────────────

    void commitParameter(const std::uint32_t parameter, const float v)
    {
        const ParamSpec& spec = kParameterSpecs[parameter];
        float clamped = clampf(v, spec.minimum, spec.maximum);
        if (spec.integer)
            clamped = std::round(clamped);
        values_[parameter] = clamped;
        setParameterValue(parameter, clamped);
        repaint();
    }

    void updateSlider(const std::uint32_t slider, const float mx)
    {
        const Rect track = sliderTrack(slider);
        const float t = clampf((mx - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
        commitParameter(kSliderParams[slider], sliderToValue(slider, t));
    }

    void updatePad(const float mx, const float my)
    {
        const Rect box = padBox();
        const float tx = clampf((mx - box.x) / std::max(1.0f, box.w), 0.0f, 1.0f);
        // Y is inverted: the top of the pad is the narrowest (highest Q) setting.
        const float ty = clampf(1.0f - (my - box.y) / std::max(1.0f, box.h), 0.0f, 1.0f);
        commitParameter(kXAxis.param, axisValue(kXAxis, tx));
        commitParameter(kYAxis.param, axisValue(kYAxis, ty));
    }

    void openDrop(const int id)
    {
        openDrop_ = id;
        const DropDef& def = openDef();
        const int current = dropCurrentItem(def, values_[def.index]);
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

    void commitDrop(const int item)
    {
        const DropDef& def = openDef();
        commitParameter(def.index, dropItemValue(def, item));
    }

    [[nodiscard]] bool dropScrollable() const noexcept
    {
        return dropItemCount(openDef()) > kTwoColLimit;
    }
    [[nodiscard]] int dropCols() const noexcept { return dropScrollable() ? 1 : (dropItemCount(openDef()) > 4 ? 2 : 1); }
    [[nodiscard]] int dropRows() const noexcept
    {
        const int count = dropItemCount(openDef());
        if (dropScrollable())
            return kMaxRows;
        return (count + dropCols() - 1) / dropCols();
    }

    [[nodiscard]] Rect popupRect(const float W, const float H) const noexcept
    {
        const Rect header = dropBox(static_cast<std::uint32_t>(openDrop_));
        const int rows = dropRows();
        const float popH = static_cast<float>(rows) * kItemH + 8.0f;
        const float popW = std::max(header.w, 200.0f);
        const float x = std::min(header.x, W - popW - 8.0f);
        float y = header.y + header.h + 3.0f;
        if (y + popH > H - 8.0f)
            y = std::max(8.0f, header.y - 3.0f - popH);
        return { x, y, popW, popH };
    }

    [[nodiscard]] int popupItemAt(const float mx, const float my, const Rect& popup) const noexcept
    {
        const float listX = popup.x + 4.0f;
        const float listY = popup.y + 4.0f;
        const float listW = popup.w - 8.0f - (dropScrollable() ? 10.0f : 0.0f);
        if (mx < listX || mx > listX + listW || my < listY)
            return -1;

        const int row = static_cast<int>((my - listY) / kItemH);
        const int count = dropItemCount(openDef());

        if (dropScrollable()) {
            if (row < 0 || row >= kMaxRows)
                return -1;
            const int item = dropScroll_ + row;
            return item < count ? item : -1;
        }

        const int cols = dropCols();
        if (row < 0 || row >= dropRows())
            return -1;
        const int col = static_cast<int>((mx - listX) / (listW / static_cast<float>(cols)));
        if (col < 0 || col >= cols)
            return -1;
        const int item = row * cols + col;
        return item < count ? item : -1;
    }

    // ── Drawing ──────────────────────────────────────────────────────────

    void drawBackground(const float W, const float H)
    {
        beginPath();
        fc(theme().background);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawHeader(const float W)
    {
        const auto& th = theme();
        beginPath();
        fc(th.panel);
        rect(0, 0, W, kHeaderH);
        fill();
        closePath();

        fontSize(24.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(th.textPrimary);
        text(kPad, 27.0f, "SINEVOX", nullptr);

        fontSize(10.0f);
        fc(th.textDim);
        text(kPad + 128.0f, 21.0f, "formant tracker \xc2\xb7 vocoder + sine wave speech", nullptr);
        text(kPad + 128.0f, 36.0f, "speech in 1/2 \xc2\xb7 carrier in 3/4 \xc2\xb7 MIDI in/out", nullptr);

        beginPath();
        sc(th.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, kHeaderH);
        lineTo(W, kHeaderH);
        stroke();
        closePath();

        const int mode = std::clamp(static_cast<int>(std::lround(value(ParamId::mode))), 0, 2);
        for (int m = 0; m < 3; ++m)
            drawButton(modeRect(m), modeName(m), m == mode);

        themeRect_ = { W - 90.0f, 16.0f, 74.0f, 26.0f };
        drawButton(themeRect_, darkTheme_ ? "DARK" : "LIGHT", false);
    }

    void drawColumnStrips(const float W)
    {
        drawColumnStrip(kPad, W - 2.0f * kPad, kHeaderH + 1.0f, "FORMANTS  (what the analyser hears)", kReadoutAccent);
        drawColumnStrip(kAnalysisX, kColW, kStripY, "ANALYSIS", kAnalysisAccent);
        drawColumnStrip(kSynthX, kColW, kStripY, "SYNTHESIS", kSynthAccent);
        drawColumnStrip(kMidiX, W - kMidiX - kPad, kStripY, "MIDI", kMidiAccent);
    }

    void drawColumnStrip(const float x, const float w, const float y, const char* title, const Accent& accent)
    {
        beginPath();
        fc(accent);
        roundedRect(x, y, w, kStripH, 2.0f);
        fill();
        closePath();
        fillColor(kCreamR, kCreamG, kCreamB, 255);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 8.0f, y + kStripH * 0.5f, title, nullptr);
    }

    void drawPanelBox(const Rect& box)
    {
        const auto& th = theme();
        beginPath();
        fc(th.panel);
        roundedRect(box.x, box.y, box.w, box.h, 3.0f);
        fill();
        closePath();
        beginPath();
        sc(th.border);
        strokeWidth(1.0f);
        roundedRect(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, 3.0f);
        stroke();
        closePath();
    }

    // The tracker's view of the input: one bell per formant (height = level,
    // width = bandwidth), and the power sum of all of them as the envelope.
    void drawFormantPlot(const float W)
    {
        const auto& th = theme();
        const Rect box = { kPad, kPlotY, W - 2.0f * kPad, kPlotH };
        drawPanelBox(box);

        const Rect plot = { box.x + 40.0f, box.y + 8.0f, box.w - 52.0f, box.h - 28.0f };
        const float logMin = std::log(kPlotMinHz);
        const float logSpan = std::log(kPlotMaxHz) - logMin;
        auto xOf = [&](const float hz) {
            return plot.x + plot.w * clampf((std::log(std::max(hz, 1.0f)) - logMin) / logSpan, 0.0f, 1.0f);
        };
        auto yOf = [&](const float db) {
            return plot.y + plot.h * (1.0f - clampf((db - kPlotMinDb) / (kPlotMaxDb - kPlotMinDb), 0.0f, 1.0f));
        };

        fontSize(8.5f);
        const std::array<float, 8> gridHz = { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 3000.0f, 5000.0f, 8000.0f };
        const std::array<const char*, 8> gridText = { "100", "200", "500", "1k", "2k", "3k", "5k", "8k" };
        for (std::size_t g = 0; g < gridHz.size(); ++g) {
            const float gx = xOf(gridHz[g]);
            beginPath();
            sc(th.border);
            strokeWidth(1.0f);
            moveTo(gx, plot.y);
            lineTo(gx, plot.y + plot.h);
            stroke();
            closePath();
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fc(th.textDisabled);
            text(gx, plot.y + plot.h + 3.0f, gridText[g], nullptr);
        }
        for (const float db : { -20.0f, -40.0f, -60.0f }) {
            const float gy = yOf(db);
            beginPath();
            sc(th.border);
            strokeWidth(1.0f);
            moveTo(plot.x, gy);
            lineTo(plot.x + plot.w, gy);
            stroke();
            closePath();
            char label[16];
            std::snprintf(label, sizeof(label), "%.0f dB", static_cast<double>(db));
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            fc(th.textDisabled);
            text(plot.x - 4.0f, gy, label, nullptr);
        }

        const int active = formantCount();
        const float shiftSemitones = value(ParamId::shift);
        const float baseY = plot.y + plot.h;

        std::array<bool, kMaxFormants> sounding {};
        for (int i = 0; i < kMaxFormants; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            const float hz = values_[index(ParamId::outFreq1) + idx];
            const float db = values_[index(ParamId::outAmp1) + idx];
            const float q = std::max(values_[index(ParamId::outQ1) + idx], 0.7f);
            sounding[idx] = i < active && hz > 0.0f && db > -75.0f;
            if (!sounding[idx])
                continue;

            const float bw = hz / q;
            const Accent& colour = kFormantColours[idx];

            // Lorentzian bell, drawn in log-frequency so it stays symmetric on screen.
            // Clipped to +-5 bandwidths: the far skirts only clutter the plot.
            const float loPx = std::max(plot.x, xOf(std::max(hz - 5.0f * bw, kPlotMinHz)));
            const float hiPx = std::min(plot.x + plot.w, xOf(std::min(hz + 5.0f * bw, kPlotMaxHz)));
            beginPath();
            moveTo(loPx, baseY);
            for (float px = loPx; px <= hiPx; px += 2.0f) {
                const float f = std::exp(logMin + logSpan * (px - plot.x) / plot.w);
                const float lorentz = 1.0f / (1.0f + std::pow((f - hz) / (0.5f * bw), 2.0f));
                const float level = db + 10.0f * std::log10(std::max(lorentz, 1.0e-6f));
                lineTo(px, yOf(level));
            }
            lineTo(hiPx, baseY);
            closePath();
            fc(colour, 34);
            fill();

            // The peak itself, as a lamp-style bar.
            beginPath();
            sc(colour);
            strokeWidth(2.0f);
            moveTo(xOf(hz), baseY);
            lineTo(xOf(hz), yOf(db));
            stroke();
            closePath();
            beginPath();
            fc(colour);
            circle(xOf(hz), yOf(db), 3.5f);
            fill();
            closePath();

            char tag[8];
            std::snprintf(tag, sizeof(tag), "F%d", i + 1);
            fontSize(9.5f);
            textAlign(ALIGN_CENTER | ALIGN_BOTTOM);
            fc(th.textPrimary);
            text(xOf(hz), yOf(db) - 6.0f, tag, nullptr);

            // Where the formant will sound once Shift is applied.
            if (std::fabs(shiftSemitones) > 0.05f) {
                const float shifted = hz * std::exp2(shiftSemitones / 12.0f);
                if (shifted > kPlotMinHz && shifted < kPlotMaxHz) {
                    beginPath();
                    fc(kSynthAccent);
                    moveTo(xOf(shifted), baseY - 1.0f);
                    lineTo(xOf(shifted) - 4.0f, baseY + 6.0f);
                    lineTo(xOf(shifted) + 4.0f, baseY + 6.0f);
                    closePath();
                    fill();
                }
            }
        }

        // Envelope: power sum of the formants.
        bool any = false;
        for (const bool s : sounding)
            any = any || s;
        if (any) {
            beginPath();
            bool first = true;
            for (float px = plot.x; px <= plot.x + plot.w; px += 2.0f) {
                const float f = std::exp(logMin + logSpan * (px - plot.x) / plot.w);
                float power = 1.0e-12f;
                for (int i = 0; i < kMaxFormants; ++i) {
                    const auto idx = static_cast<std::size_t>(i);
                    if (!sounding[idx])
                        continue;
                    const float hz = values_[index(ParamId::outFreq1) + idx];
                    const float db = values_[index(ParamId::outAmp1) + idx];
                    const float q = std::max(values_[index(ParamId::outQ1) + idx], 0.7f);
                    const float bw = hz / q;
                    power += std::pow(10.0f, db / 10.0f) / (1.0f + std::pow((f - hz) / (0.5f * bw), 2.0f));
                }
                const float y = yOf(10.0f * std::log10(power));
                if (first) {
                    moveTo(px, y);
                    first = false;
                } else {
                    lineTo(px, y);
                }
            }
            sc(th.textPrimary);
            strokeWidth(1.4f);
            stroke();
            closePath();
        } else {
            fontSize(12.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fc(th.textDisabled);
            text(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f,
                 "no speech above the gate \xe2\x80\x94 send audio to inputs 1/2", nullptr);
        }
    }

    // Numbers for the plot: frequency, Q and level of each slot in use.
    void drawFormantTable(const float W)
    {
        const auto& th = theme();
        const float cellW = (W - 2.0f * kPad) / 4.0f;
        const int active = formantCount();
        for (int i = 0; i < kMaxFormants; ++i) {
            const auto idx = static_cast<std::size_t>(i);
            const float x = kPad + static_cast<float>(i % 4) * cellW;
            const float y = kTableY + static_cast<float>(i / 4) * kTableRowH;
            const bool inUse = i < active;
            const float hz = values_[index(ParamId::outFreq1) + idx];
            const float db = values_[index(ParamId::outAmp1) + idx];
            const bool live = inUse && hz > 0.0f && db > -75.0f;

            beginPath();
            fc(kFormantColours[idx], inUse ? 255 : 70);
            circle(x + 6.0f, y + 9.0f, 4.0f);
            fill();
            closePath();

            char line[96];
            if (live)
                std::snprintf(line, sizeof(line), "F%d  %.0f Hz   Q %.1f   %.0f dB", i + 1,
                              static_cast<double>(hz), static_cast<double>(values_[index(ParamId::outQ1) + idx]),
                              static_cast<double>(db));
            else
                std::snprintf(line, sizeof(line), inUse ? "F%d  \xe2\x80\x94" : "F%d  unused", i + 1);
            fontSize(10.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fc(live ? th.textPrimary : th.textDisabled);
            text(x + 16.0f, y + 9.0f, line, nullptr);
        }
    }

    void drawTrack(const Rect& tr, const float norm, const Accent& accent)
    {
        beginPath();
        fc(theme().controlTrack);
        roundedRect(tr.x, tr.y, tr.w, tr.h, 6.0f);
        fill();
        closePath();

        if (norm > 0.001f) {
            beginPath();
            fc(accent);
            roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 6.0f);
            fill();
            closePath();
        }
    }

    void drawLabelValue(const float x, const float y, const float w,
                        const char* label, const char* val, const bool dim = false)
    {
        const auto& th = theme();
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDim);
        text(x, y, label, nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(dim ? th.textDisabled : th.textPrimary);
        text(x + w, y, val, nullptr);
    }

    void drawSliders()
    {
        const int mode = static_cast<int>(std::lround(value(ParamId::mode)));
        for (std::uint32_t s = 0; s < kSliderCount; ++s) {
            const Rect track = sliderTrack(s);
            const std::uint32_t parameter = kSliderParams[s];
            const Accent& accent = s < kAnalysisSliders ? kAnalysisAccent : kSynthAccent;

            // Controls that belong to an engine that is not running are dimmed
            // rather than hidden, so the layout never moves.
            bool dim = false;
            if (s == kSliderCarrierNorm)
                dim = mode == 1;
            else if (s == kSliderSpread)
                dim = mode == 0;

            drawLabelValue(track.x, track.y - 17.0f, track.w, kSliderLabels[s],
                           formatSlider(s, values_[parameter]).c_str(), dim);
            drawTrack(track, valueToSlider(s, values_[parameter]), dim ? Accent { 130, 126, 116 } : accent);
        }
    }

    // X: Shift. Y: Q Scale. The crosshair is the neutral setting.
    void drawPad()
    {
        const auto& th = theme();
        const Rect box = padBox();
        drawPanelBox(box);

        const float xNorm = axisNorm(kXAxis, value(ParamId::shift));
        const float yNorm = axisNorm(kYAxis, value(ParamId::qScale));
        const float px = box.x + xNorm * box.w;
        const float py = box.y + (1.0f - yNorm) * box.h;
        const float homeX = box.x + axisNorm(kXAxis, 0.0f) * box.w;
        const float homeY = box.y + (1.0f - axisNorm(kYAxis, 1.0f)) * box.h;

        beginPath();
        sc(th.controlTrack);
        strokeWidth(1.0f);
        moveTo(homeX, box.y + 2.0f);
        lineTo(homeX, box.y + box.h - 2.0f);
        moveTo(box.x + 2.0f, homeY);
        lineTo(box.x + box.w - 2.0f, homeY);
        stroke();
        closePath();

        fontSize(8.5f);
        fc(th.textDisabled);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(box.x + 4.0f, box.y + box.h - 3.0f, "-12 st", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(box.x + box.w - 4.0f, box.y + box.h - 3.0f, "+12 st", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(box.x + 4.0f, box.y + 3.0f, "narrow", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(box.x + 4.0f, box.y + box.h - 14.0f, "wide", nullptr);

        beginPath();
        fc(kSynthAccent, 40);
        circle(px, py, 13.0f);
        fill();
        closePath();
        beginPath();
        fc(kSynthAccent);
        circle(px, py, 5.5f);
        fill();
        closePath();

        char line[64];
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDim);
        text(box.x, box.y - 15.0f, "SHIFT / Q", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(th.textPrimary);
        std::snprintf(line, sizeof(line), "%+.1f st   x%.2f", static_cast<double>(value(ParamId::shift)),
                      static_cast<double>(value(ParamId::qScale)));
        text(box.x + box.w, box.y - 15.0f, line, nullptr);
    }

    void drawSwitchRow(const Rect& box, const char* label, const bool on,
                       const char* onText, const char* offText)
    {
        const auto& th = theme();
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(th.textDim);
        text(box.x, box.y + box.h * 0.5f, label, nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(th.textPrimary);
        text(box.x + box.w - 52.0f, box.y + box.h * 0.5f, on ? onText : offText, nullptr);

        const float x = box.x + box.w - 44.0f;
        const float y = box.y + (box.h - 20.0f) * 0.5f;
        beginPath();
        roundedRect(x, y, 44.0f, 20.0f, 10.0f);
        if (on)
            fc(kSynthAccent);
        else
            fc(th.controlTrack);
        fill();
        closePath();
        beginPath();
        circle(x + (on ? 33.0f : 11.0f), y + 10.0f, 7.0f);
        fc(th.controlKnob);
        fill();
        closePath();
    }

    void drawButton(const Rect& box, const char* label, const bool active)
    {
        const auto& th = theme();
        beginPath();
        if (active)
            fc(kSynthAccent);
        else
            fc(th.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, 2.0f);
        fill();
        closePath();
        beginPath();
        sc(th.border);
        strokeWidth(1.0f);
        roundedRect(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, 2.0f);
        stroke();
        closePath();
        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        if (active)
            fillColor(kCreamR, kCreamG, kCreamB, 255);
        else
            fc(th.textPrimary);
        text(box.x + box.w * 0.5f, box.y + box.h * 0.5f, label, nullptr);
    }

    void drawDropBox(const Rect& box, const char* current)
    {
        const auto& th = theme();
        beginPath();
        fc(th.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, 4.0f);
        fill();
        closePath();
        beginPath();
        sc(th.border);
        strokeWidth(1.0f);
        roundedRect(box.x + 0.5f, box.y + 0.5f, box.w - 1.0f, box.h - 1.0f, 4.0f);
        stroke();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(th.textPrimary);
        text(box.x + 8.0f, box.y + box.h * 0.5f, current, nullptr);

        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(th.textDim);
        text(box.x + box.w - 8.0f, box.y + box.h * 0.5f, "\xe2\x96\xbe", nullptr);
    }

    void drawMidiColumn()
    {
        const auto& th = theme();

        for (std::uint32_t d = 0; d < kDropDefs.size(); ++d) {
            const DropDef& def = kDropDefs[d];
            const Rect box = dropBox(d);
            char current[40];
            dropItemText(def, dropCurrentItem(def, values_[def.index]), current, sizeof(current));

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(th.textDim);
            text(box.x, box.y - 17.0f, def.label, nullptr);
            drawDropBox(box, current);
        }

        const float hintY = kBodyY + static_cast<float>(kDropRows) * kDropH + 14.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDim);
        text(kMidiX, hintY, "In: CC drives Shift, Q Scale, Formants, Tilt and Mode;", nullptr);
        text(kMidiX, hintY + 14.0f, "with Note Shift on, a held note transposes the formants", nullptr);
        text(kMidiX, hintY + 28.0f, "from middle C. Notes and CCs share the In Channel.", nullptr);

        char line[120];
        fc(th.textDim);
        text(kMidiX, hintY + 52.0f, "Out (MIDI Out menu), sent after Shift:", nullptr);
        fc(th.textPrimary);
        std::snprintf(line, sizeof(line), "formant CCs   frequency CC %d-%d   level CC %d-%d",
                      kOutFreqCcBase, kOutFreqCcBase + kMaxFormants - 1,
                      kOutAmpCcBase, kOutAmpCcBase + kMaxFormants - 1);
        text(kMidiX + 10.0f, hintY + 67.0f, line, nullptr);
        std::snprintf(line, sizeof(line), "frequency CC: %.0f Hz - %.0f kHz, log; level CC: -60 - 0 dB",
                      static_cast<double>(kOutFreqMinHz), static_cast<double>(kOutFreqMaxHz) / 1000.0);
        text(kMidiX + 10.0f, hintY + 81.0f, line, nullptr);
        text(kMidiX + 10.0f, hintY + 95.0f, "formant notes   one held note per formant, velocity = level", nullptr);
    }

    // Input level against the gate, and a plain-language reminder of what the
    // current mode reads and writes.
    void drawInputStatus()
    {
        const auto& th = theme();
        const Rect box = { kSynthX, kLowerY + 16.0f, kColW, kPadH };
        drawPanelBox(box);

        const float levelDb = value(ParamId::outLevelDb);
        const float gateDb = value(ParamId::gateDb);

        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDim);
        text(box.x + 8.0f, box.y + 6.0f, "INPUT 1/2", nullptr);
        char line[64];
        std::snprintf(line, sizeof(line), "%.0f dB", static_cast<double>(levelDb));
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(th.textPrimary);
        text(box.x + box.w - 8.0f, box.y + 6.0f, line, nullptr);

        const Rect meter = { box.x + 8.0f, box.y + 24.0f, box.w - 16.0f, 10.0f };
        auto norm = [](const float db) { return clampf((db + 80.0f) / 80.0f, 0.0f, 1.0f); };
        beginPath();
        fc(th.controlTrack);
        roundedRect(meter.x, meter.y, meter.w, meter.h, 5.0f);
        fill();
        closePath();
        const bool open = levelDb >= gateDb;
        beginPath();
        if (open)
            fc(kReadoutAccent);
        else
            fc(130, 126, 116);
        roundedRect(meter.x, meter.y, std::max(meter.h, meter.w * norm(levelDb)), meter.h, 5.0f);
        fill();
        closePath();
        // The gate threshold.
        beginPath();
        sc(th.textPrimary);
        strokeWidth(1.5f);
        moveTo(meter.x + meter.w * norm(gateDb), meter.y - 3.0f);
        lineTo(meter.x + meter.w * norm(gateDb), meter.y + meter.h + 3.0f);
        stroke();
        closePath();

        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(open ? th.textPrimary : th.textDisabled);
        text(meter.x, meter.y + 16.0f, open ? "above gate: tracking" : "below gate: silent", nullptr);

        std::snprintf(line, sizeof(line), "Tracks   %d of %d",
                      static_cast<int>(std::lround(value(ParamId::outActive))), formantCount());
        fontSize(10.0f);
        fc(th.textPrimary);
        text(box.x + 8.0f, box.y + 62.0f, line, nullptr);

        const int mode = static_cast<int>(std::lround(value(ParamId::mode)));
        fontSize(9.0f);
        fc(th.textDim);
        const char* const a = mode == 1 ? "Sine waves, one per formant," : "Band-pass filters on 3/4,";
        const char* const b = mode == 1 ? "at the tracked frequency" : "centred and shaped by the";
        const char* const c = mode == 1 ? "and level. 3/4 unused." : "tracked frequency, Q, level.";
        if (mode == 2) {
            text(box.x + 8.0f, box.y + 84.0f, "Vocoder on 3/4 plus one sine", nullptr);
            text(box.x + 8.0f, box.y + 98.0f, "per formant, summed.", nullptr);
        } else {
            text(box.x + 8.0f, box.y + 84.0f, a, nullptr);
            text(box.x + 8.0f, box.y + 98.0f, b, nullptr);
            text(box.x + 8.0f, box.y + 112.0f, c, nullptr);
        }
    }

    void drawPopup(const float W, const float H)
    {
        const auto& th = theme();
        const Rect popup = popupRect(W, H);
        const DropDef& def = openDef();
        const int count = dropItemCount(def);
        const bool scrollable = dropScrollable();
        const int cols = dropCols();
        const int rows = dropRows();
        const int current = dropCurrentItem(def, values_[def.index]);
        const Accent accent = kMidiAccent;

        beginPath();
        fillColor(0, 0, 0, 110);
        rect(0, 0, W, H);
        fill();
        closePath();

        beginPath();
        fc(th.surface);
        roundedRect(popup.x, popup.y, popup.w, popup.h, 4.0f);
        fill();
        closePath();
        beginPath();
        sc(th.border);
        strokeWidth(1.0f);
        roundedRect(popup.x + 0.5f, popup.y + 0.5f, popup.w - 1.0f, popup.h - 1.0f, 4.0f);
        stroke();
        closePath();

        const float listX = popup.x + 4.0f;
        const float listY = popup.y + 4.0f;
        const float listW = popup.w - 8.0f - (scrollable ? 10.0f : 0.0f);
        const int top = scrollable ? dropScroll_ : 0;

        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                const int item = scrollable ? top + r : r * cols + c;
                if (item >= count)
                    continue;
                const float cw = listW / static_cast<float>(cols);
                const float cx = listX + static_cast<float>(c) * cw;
                const float cy = listY + static_cast<float>(r) * kItemH;
                const bool hovered = item == dropHover_;
                const bool selected = item == current;
                if (hovered || selected) {
                    beginPath();
                    roundedRect(cx, cy, cw, kItemH, 3.0f);
                    if (selected)
                        fc(accent);
                    else
                        fc(accent, 90);
                    fill();
                    closePath();
                }
                char itemText[40];
                dropItemText(def, item, itemText, sizeof(itemText));
                fontSize(11.0f);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                if (selected)
                    fillColor(kCreamR, kCreamG, kCreamB, 255);
                else
                    fc(th.textPrimary);
                text(cx + 8.0f, cy + kItemH * 0.5f, itemText, nullptr);
            }
        }

        if (scrollable) {
            const float trackX = popup.x + popup.w - 10.0f;
            const float trackY = listY;
            const float trackH = static_cast<float>(rows) * kItemH;
            beginPath();
            fc(th.controlTrack);
            roundedRect(trackX, trackY, 5.0f, trackH, 2.0f);
            fill();
            closePath();
            const float thumbH = std::max(16.0f, trackH * static_cast<float>(rows) / static_cast<float>(count));
            const float thumbY = trackY + (trackH - thumbH) * static_cast<float>(dropScroll_)
                / static_cast<float>(std::max(1, count - rows));
            beginPath();
            roundedRect(trackX, thumbY, 5.0f, thumbH, 2.0f);
            fc(accent);
            fill();
            closePath();
        }
    }

    // Plain RGB fill, for colours that are not part of the theme.
    void fc(const int r, const int g, const int b) { fillColor(r, g, b, 255); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SinevoxUI)
};

UI* createUI()
{
    return new SinevoxUI();
}

END_NAMESPACE_DISTRHO
