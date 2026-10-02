#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "voxmod_core_types.hpp"
#include "voxmod_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::voxmod::ParamId;
using downspout::voxmod::ParamSpec;
using downspout::voxmod::RingShapeId;
using downspout::voxmod::kControllerMap;
using downspout::voxmod::kParameterCount;
using downspout::voxmod::kParameterSpecs;
using downspout::voxmod::index;

constexpr float kPi = 3.14159265358979323846f;

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

// Same palette family as magneto: amber for the ring modulator, cold steel for
// the vocoder, olive for the derived readout, violet for the CC routing column.
constexpr Accent kRingAccent { 198, 132, 58 };
constexpr Accent kVoxAccent { 92, 140, 156 };
constexpr Accent kCcAccent { 138, 116, 168 };
constexpr Accent kReadoutAccent { 124, 148, 104 };
constexpr int kCreamR = 250;
constexpr int kCreamG = 248;
constexpr int kCreamB = 242;

[[nodiscard]] float clampf(const float v, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

// ── Sliders ────────────────────────────────────────────────────────────────

// Mix and Ring Freq are deliberately absent: they are the two axes of the x-y
// pad below, which owns them. Duplicating them as sliders would give the user
// two controls for the same values and risk them disagreeing.
enum SliderId : std::uint32_t {
    kSliderRingDepth = 0,
    kSliderWidth,
    kSliderDrift,
    kSliderAttack,
    kSliderRelease,
    kSliderFormant,
    kSliderTilt,
    kSliderCount,
};

constexpr std::array<std::uint32_t, kSliderCount> kSliderParams = {
    index(ParamId::ringDepth),
    index(ParamId::stereoWidth),
    index(ParamId::drift),
    index(ParamId::attackMs),
    index(ParamId::releaseMs),
    index(ParamId::formantShift),
    index(ParamId::tilt),
};

constexpr std::array<const char*, kSliderCount> kSliderLabels = {
    "Ring Depth", "Width", "Drift", "Attack", "Release", "Formant", "Tilt",
};

// Which engine each slider belongs to, so the columns and the accent follow the
// same split the audio does.
constexpr std::array<Accent, kSliderCount> kSliderAccents = {
    kRingAccent,
    kRingAccent,
    kRingAccent,
    kVoxAccent,
    kVoxAccent,
    kVoxAccent,
    kVoxAccent,
};

// Log-scale mapping where the range spans a decade or more.
[[nodiscard]] bool sliderIsLogarithmic(const std::uint32_t slider) noexcept
{
    return slider == kSliderAttack;
}

[[nodiscard]] float sliderToValue(const std::uint32_t slider, const float t) noexcept
{
    const ParamSpec& spec = kParameterSpecs[kSliderParams[slider]];
    if (!sliderIsLogarithmic(slider) || spec.minimum <= 0.0f)
        return spec.minimum + t * (spec.maximum - spec.minimum);

    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return std::exp(lo + t * (hi - lo));
}

[[nodiscard]] float valueToSlider(const std::uint32_t slider, const float value) noexcept
{
    const ParamSpec& spec = kParameterSpecs[kSliderParams[slider]];
    if (!sliderIsLogarithmic(slider) || spec.minimum <= 0.0f)
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
    case kSliderRingDepth:
    case kSliderWidth:
    case kSliderDrift:
        std::snprintf(buf, sizeof(buf), "%.0f%%", v);
        break;
    case kSliderAttack:
        std::snprintf(buf, sizeof(buf), "%.1f ms", v);
        break;
    case kSliderRelease:
        std::snprintf(buf, sizeof(buf), "%.0f ms", v);
        break;
    case kSliderFormant:
        std::snprintf(buf, sizeof(buf), "%+.1f st", v);
        break;
    case kSliderTilt:
        std::snprintf(buf, sizeof(buf), "%+.1f dB/oct", v);
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.2f", v);
        break;
    }
    return buf;
}

// ── x-y control ────────────────────────────────────────────────────────────
//
// The Mix slider and a frequency control are the two axes that define the
// plugin's character, so they get an x-y pad rather than two more rows: X is
// the vocoder/ring balance, Y is the ring carrier frequency. Dragging one
// changes both, which is how you actually explore this effect.

struct XYAxis {
    std::uint32_t param;
    float minimum;
    float maximum;
    bool logarithmic;
    const char* label;
    const char* unit;
};

constexpr XYAxis kXY = {
    index(ParamId::mix), 0.0f, 100.0f, false, "Mix", "%",
};
constexpr XYAxis kXYVertical = {
    index(ParamId::ringFreq), 5.0f, 5000.0f, true, "Ring Freq", "Hz",
};

// Normalised 0-1 position of a parameter along a pad axis, so the dot lands
// where the host value actually is.
[[nodiscard]] float axisNorm(const XYAxis& axis, const float value) noexcept
{
    if (!axis.logarithmic || axis.minimum <= 0.0f)
        return clampf((value - axis.minimum) / (axis.maximum - axis.minimum), 0.0f, 1.0f);
    const float lo = std::log(axis.minimum);
    const float hi = std::log(axis.maximum);
    return clampf((std::log(std::clamp(value, axis.minimum, axis.maximum)) - lo) / (hi - lo),
                  0.0f, 1.0f);
}

[[nodiscard]] float axisValue(const XYAxis& axis, const float t) noexcept
{
    if (!axis.logarithmic || axis.minimum <= 0.0f)
        return axis.minimum + t * (axis.maximum - axis.minimum);
    return std::exp(std::log(axis.minimum) + t * (std::log(axis.maximum) - std::log(axis.minimum)));
}

[[nodiscard]] std::string formatXY(const XYAxis& axis, const float value)
{
    char buf[48];
    if (axis.logarithmic && value >= 1000.0f)
        std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(value) / 1000.0);
    else if (axis.logarithmic)
        std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(value));
    else
        std::snprintf(buf, sizeof(buf), "%.0f%s", static_cast<double>(value), axis.unit);
    return buf;
}

// ── Dropdowns ──────────────────────────────────────────────────────────────

struct DropDef {
    std::uint32_t index;
    const char* label;
    int kind;  // 0 CC number, 1 MIDI channel, 2 ring shape, 3 toggle
};

enum DropKind {
    kDropCc = 0,
    kDropChannel,
    kDropShape,
    kDropToggle,
};

constexpr std::array<DropDef, 9> kDropDefs = {{
    { index(ParamId::ccMix), "CC Mix", kDropCc },
    { index(ParamId::ccRingFreq), "CC Ring Freq", kDropCc },
    { index(ParamId::ccRingRatio), "CC Ring Ratio", kDropCc },
    { index(ParamId::ccBandCount), "CC Bands", kDropCc },
    { index(ParamId::ccCarrier), "CC Carrier", kDropCc },
    { index(ParamId::ccChannel), "CC Channel", kDropChannel },
    { index(ParamId::ringShape), "Ring Shape", kDropShape },
    { index(ParamId::carrierSource), "Carrier", kDropToggle },
    { index(ParamId::sync), "Sync", kDropToggle },
}};

constexpr const char* kCcCommonNames(int cc)
{
    switch (cc) {
    case 1: return "Mod";
    case 2: return "Breath";
    case 3: return "Density";
    case 4: return "Reson";
    case 7: return "Volume";
    case 10: return "Pan";
    case 11: return "Expr";
    case 74: return "Bright";
    default: return nullptr;
    }
}

constexpr const char* kRingShapeNames(int shape)
{
    switch (shape) {
    case 0: return "sine";
    case 1: return "triangle";
    case 2: return "saw";
    case 3: return "square";
    default: return "sine";
    }
}

constexpr const char* kCarrierNames(int source)
{
    return source == 1 ? "internal osc" : "input 1/2";
}

constexpr const char* kSyncNames(int on)
{
    return on == 1 ? "ring \xe2\x86\x92 vocoder" : "off";
}

[[nodiscard]] int dropItemCount(const DropDef& def) noexcept
{
    switch (def.kind) {
    case kDropChannel: return 16;
    case kDropShape: return 4;
    case kDropToggle: return 2;
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
    case kDropShape:
        std::snprintf(buf, size, "%s", kRingShapeNames(item));
        return;
    case kDropToggle: {
        const bool isRingShape = def.index == index(ParamId::ringShape);
        if (isRingShape)
            std::snprintf(buf, size, "%s", kRingShapeNames(item));
        else
            std::snprintf(buf, size, "%s",
                          def.index == index(ParamId::carrierSource)
                              ? kCarrierNames(item) : kSyncNames(item));
        return;
    }
    case kDropCc:
    default:
        if (item == 0) {
            std::snprintf(buf, size, "off");
            return;
        }
        if (const char* common = kCcCommonNames(item))
            std::snprintf(buf, size, "%d %s", item, common);
        else
            std::snprintf(buf, size, "CC %d", item);
        return;
    }
}

[[nodiscard]] int dropCurrentItem(const DropDef& def, const float value) noexcept
{
    const int maxItem = dropItemCount(def) - 1;
    if (def.kind == kDropChannel)
        return std::clamp(static_cast<int>(std::lround(value)) - 1, 0, 15);
    return std::clamp(static_cast<int>(std::lround(value)), 0, maxItem);
}

[[nodiscard]] float dropItemValue(const DropDef& def, const int item) noexcept
{
    if (def.kind == kDropChannel)
        return static_cast<float>(item + 1);
    return static_cast<float>(item);
}

// ── Layout ─────────────────────────────────────────────────────────────────

constexpr float kPad = 16.0f;
constexpr float kHeaderH = 58.0f;
constexpr float kStripH = 18.0f;
constexpr float kRingX = kPad;
constexpr float kRingW = 176.0f;
constexpr float kVoxX = kRingX + kRingW + 14.0f;
constexpr float kVoxW = 176.0f;
constexpr float kCcX = kVoxX + kVoxW + 14.0f;
constexpr float kBodyY = kHeaderH + kStripH + 14.0f;
constexpr float kSliderH = 44.0f;
constexpr float kDropH = 44.0f;
constexpr float kItemH = 20.0f;
constexpr int kMaxRows = 10;
constexpr int kTwoColLimit = 20;
constexpr float kTrackH = 12.0f;

constexpr float kXYSize = 150.0f;
// Placed below every row of sliders so the pad cannot collide with the fifth
// column row or the Bypass switch.
constexpr float kXYTop = kBodyY + 6.0f * kSliderH + 30.0f;

}  // namespace

class VoxmodUI : public UI
{
public:
    VoxmodUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        for (std::uint32_t i = 0; i < kParameterCount; ++i)
            values_[i] = kParameterSpecs[i].defaultValue;

       #ifdef DGL_NO_SHARED_RESOURCES
        createFontFromFile("sans", "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
       #else
        loadSharedResources();
       #endif
    }

protected:
    void parameterChanged(const std::uint32_t index, const float value) override
    {
        if (index >= values_.size())
            return;
        values_[index] = value;
        // A trigger never sticks on: the processor resets it to 0.
        if (kParameterSpecs[index].trigger && value > 0.5f)
            values_[index] = 0.0f;
        repaint();
    }

    void onNanoDisplay() override
    {
        const float W = static_cast<float>(getWidth());
        const float H = static_cast<float>(getHeight());

        drawBackground(W, H);
        drawHeader(W);
        drawColumnStrips(W);
        drawEngineColumns();
        drawRoutingColumn(W);
        drawRandomiseButton();
        drawStatusReadout(H);
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
            draggingXY_ = false;
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
                for (int s = 0; s < static_cast<int>(kSliderCount); ++s) {
                    if (sliderTrack(s).contains(mx, my))
                        return true;
                }
                return false;
            }
        }

        if (bypassBox().contains(mx, my)) {
            commitParam(index(ParamId::bypass), values_[index(ParamId::bypass)] >= 0.5f ? 0.0f : 1.0f);
            return true;
        }

        if (randomiseRect_.contains(mx, my)) {
            // Trigger the host parameter so Randomise is automatable and the
            // processor, not the panel, decides what a new patch is.
            setParameterValue(index(ParamId::randomise), 1.0f);
            commitParameter(index(ParamId::randomise), 0.0f);
            return true;
        }

        const Rect xy = xyBox();
        if (xy.contains(mx, my)) {
            draggingXY_ = true;
            updateXY(mx, my);
            return true;
        }

        for (int s = 0; s < static_cast<int>(kSliderCount); ++s) {
            const Rect track = sliderTrack(s);
            if (track.contains(mx, my)) {
                dragSlider_ = s;
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

        if (draggingXY_) {
            updateXY(mx, my);
            return true;
        }
        if (dragSlider_ >= 0) {
            updateSlider(dragSlider_, mx);
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
        if (!popupOpen())
            return false;
        const Rect popup = popupRect(static_cast<float>(getWidth()),
                                     static_cast<float>(getHeight()));
        if (!popup.contains(ev.pos.getX(), ev.pos.getY()))
            return false;
        if (!dropScrollable())
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
    Rect randomiseRect_ {};

    void fc(const laf::Colour& c) { fillColor(c.r, c.g, c.b, c.a); }
    void sc(const laf::Colour& c) { strokeColor(c.r, c.g, c.b, c.a); }
    void fc(const Accent& a, const int alpha = 255) { fillColor(a.r, a.g, a.b, alpha); }
    void sc(const Accent& a, const int alpha = 255) { strokeColor(a.r, a.g, a.b, alpha); }

    std::array<float, kParameterCount> values_ {};
    int dragSlider_ = -1;
    bool draggingXY_ = false;

    int openDrop_ = -1;
    int dropScroll_ = 0;
    int dropHover_ = -1;

    [[nodiscard]] bool popupOpen() const noexcept { return openDrop_ >= 0; }
    [[nodiscard]] const DropDef& openDef() const noexcept
    {
        return kDropDefs[static_cast<std::size_t>(openDrop_)];
    }

    // ── Hit geometry ─────────────────────────────────────────────────────

    // Sliders are laid out in two engine columns, so the accent on each track
    // always matches the column it sits in. Ring mod owns the first three
    // sliders, the vocoder the rest.
    [[nodiscard]] std::uint32_t sliderColumn(const std::uint32_t slider) const noexcept
    {
        return slider < 3 ? 0u : 1u;
    }

    [[nodiscard]] float sliderRowY(const std::uint32_t slider) const noexcept
    {
        const std::uint32_t row = sliderColumn(slider) == 0
            ? slider
            : (slider - 3);
        return kBodyY + static_cast<float>(row) * kSliderH;
    }

    [[nodiscard]] float sliderColX(const std::uint32_t slider) const noexcept
    {
        return sliderColumn(slider) == 0 ? kRingX : kVoxX;
    }

    [[nodiscard]] float sliderColW() const noexcept { return kVoxW; }

    [[nodiscard]] Rect sliderTrack(const std::uint32_t slider) const noexcept
    {
        return { sliderColX(slider), sliderRowY(slider) + 19.0f, sliderColW(), kTrackH };
    }

    // Bypass sits directly under the taller of the two slider columns.
    [[nodiscard]] Rect bypassBox() const noexcept
    {
        return { kVoxX, kBodyY + static_cast<float>(kSliderCount - 3) * kSliderH + 20.0f,
                 kVoxW, 32.0f };
    }

    [[nodiscard]] Rect xyBox() const noexcept
    {
        return { kRingX, kXYTop, kXYSize, kXYSize };
    }

    [[nodiscard]] Rect dropBox(const std::uint32_t drop) const noexcept
    {
        const float W = static_cast<float>(getWidth());
        return { kCcX, kBodyY + static_cast<float>(drop) * kDropH + 19.0f, W - kCcX - kPad, 22.0f };
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

    void commitParameter(const std::uint32_t parameter, const float value)
    {
        const ParamSpec& spec = kParameterSpecs[parameter];
        float v = clampf(value, spec.minimum, spec.maximum);
        if (spec.integer)
            v = std::round(v);
        values_[parameter] = v;
        setParameterValue(parameter, v);
        repaint();
    }

    void commitParam(const std::uint32_t parameter, const float value)
    {
        commitParameter(parameter, value);
    }

    void updateSlider(const std::uint32_t slider, const float mx)
    {
        const Rect track = sliderTrack(slider);
        const float t = clampf((mx - track.x) / std::max(1.0f, track.w), 0.0f, 1.0f);
        commitParameter(kSliderParams[slider], sliderToValue(slider, t));
    }

    void updateXY(const float mx, const float my)
    {
        const Rect box = xyBox();
        const float tx = clampf((mx - box.x) / std::max(1.0f, box.w), 0.0f, 1.0f);
        // Y is inverted: the top of the pad is the top of the frequency range.
        const float ty = clampf(1.0f - (my - box.y) / std::max(1.0f, box.h), 0.0f, 1.0f);

        commitParameter(kXY.param, axisValue(kXY, tx));
        commitParameter(kXYVertical.param, axisValue(kXYVertical, ty));
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
    [[nodiscard]] int dropCols() const noexcept { return dropScrollable() ? 1 : 2; }
    [[nodiscard]] int dropRows() const noexcept
    {
        const int count = dropItemCount(openDef());
        return dropScrollable() ? kMaxRows : (count + 1) / 2;
    }

    [[nodiscard]] Rect popupRect(const float W, const float H) const noexcept
    {
        const Rect header = dropBox(static_cast<std::uint32_t>(openDrop_));
        const int rows = dropRows();
        const float popH = static_cast<float>(rows) * kItemH + 8.0f;
        const float popW = std::max(header.w, 200.0f);
        float x = std::min(header.x, W - popW - 8.0f);
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

        const int cols = 2;
        if (row < 0 || row >= dropRows())
            return -1;
        const int col = static_cast<int>((mx - listX) / (listW / cols));
        if (col < 0 || col >= cols)
            return -1;
        const int item = row * cols + col;
        return item < count ? item : -1;
    }

    // ── Drawing ──────────────────────────────────────────────────────────

    void drawBackground(const float W, const float H)
    {
        const auto& th = theme();
        beginPath();
        fc(th.background);
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
        text(kPad, 27.0f, "VOXMOD", nullptr);

        fontSize(11.0f);
        fc(th.textDim);
        text(kPad + 132.0f, 27.0f,
             "vocoder + ring mod \xc2\xb7 carrier in 1/2 \xc2\xb7 modulator in 3/4 \xc2\xb7 Drift CC",
             nullptr);

        beginPath();
        sc(th.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, kHeaderH);
        lineTo(W, kHeaderH);
        stroke();
        closePath();

        themeRect_ = { W - 90.0f, 16.0f, 74.0f, 26.0f };
        drawButton(themeRect_, darkTheme_ ? "DARK" : "LIGHT", false);
    }

    void drawColumnStrips(const float W)
    {
        drawColumnStrip(kRingX, kRingW, "RING MODULATOR", kRingAccent);
        drawColumnStrip(kVoxX, kVoxW, "VOCODER", kVoxAccent);
        drawColumnStrip(kCcX, W - kCcX - kPad, "ROUTING + DRIFT CC", kCcAccent);
    }

    void drawColumnStrip(const float x, const float w, const char* title, const Accent& accent)
    {
        beginPath();
        fc(accent);
        roundedRect(x, kHeaderH + 1.0f, w, kStripH, 2.0f);
        fill();
        closePath();
        fillColor(kCreamR, kCreamG, kCreamB, 255);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 8.0f, kHeaderH + 1.0f + kStripH * 0.5f, title, nullptr);
    }

    void drawTrack(const Rect& tr, const float norm, const Accent& accent)
    {
        const auto& th = theme();
        beginPath();
        fc(th.controlTrack);
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
                        const char* label, const char* value)
    {
        const auto& th = theme();
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDim);
        text(x, y, label, nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(th.textPrimary);
        text(x + w, y, value, nullptr);
    }

    void drawEngineColumns()
    {
        for (std::uint32_t s = 0; s < kSliderCount; ++s) {
            const Rect track = sliderTrack(s);
            const std::uint32_t parameter = kSliderParams[s];
            const float x = sliderColX(s);
            const float w = sliderColW();

            // The Mix slider appears in both engines' story, so give it its own
            // accent and a note when it sits fully in one engine.
            drawLabelValue(x, track.y - 17.0f, w, kSliderLabels[s],
                           formatSlider(s, values_[parameter]).c_str());
            drawTrack(track, valueToSlider(s, values_[parameter]), kSliderAccents[s]);
        }

        drawSwitchRow(bypassBox(), "Bypass",
                      values_[index(ParamId::bypass)] >= 0.5f, "bypassed", "active");

        drawBalancePad();
    }

    // The x-y pad. X is Mix (vocoder on the right, ring mod on the left), Y is
    // Ring Freq. Both are live at once, which is how the effect is explored.
    void drawBalancePad()
    {
        const auto& th = theme();
        const Rect box = xyBox();

        const float mixT = axisNorm(kXY, values_[kXY.param]);
        const float freqT = axisNorm(kXYVertical, values_[kXYVertical.param]);
        const float px = box.x + mixT * box.w;
        const float py = box.y + (1.0f - freqT) * box.h;

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

        // The split: left of the line is ring mod, right is vocoder.
        beginPath();
        sc(th.controlTrack);
        strokeWidth(1.0f);
        moveTo(px, box.y + 2.0f);
        lineTo(px, box.y + box.h - 2.0f);
        stroke();
        closePath();

        // Axis labels.
        fontSize(8.5f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fc(th.textDisabled);
        text(box.x + 4.0f, box.y + box.h - 3.0f, "RING", nullptr);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        text(box.x + box.w - 4.0f, box.y + box.h - 3.0f, "VOCODER", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(box.x + 4.0f, box.y + 3.0f, "HIGH", nullptr);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        text(box.x + 4.0f, box.y + box.h - 14.0f, "LOW", nullptr);

        // The dot, drawn in whichever engine currently dominates.
        const Accent accent = mixT >= 0.5f ? kVoxAccent : kRingAccent;
        beginPath();
        fc(accent, 40);
        circle(px, py, 13.0f);
        fill();
        closePath();
        beginPath();
        fc(accent);
        circle(px, py, 5.5f);
        fill();
        closePath();

        // Current values, so the pad is not the only place they are visible.
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textPrimary);
        char line[80];
        std::snprintf(line, sizeof(line), "%s  %s  %s",
                      kXY.label, formatXY(kXY, values_[kXY.param]).c_str(),
                      kXYVertical.label);
        text(box.x, box.y - 14.0f, line, nullptr);
        fc(th.textPrimary);
        text(box.x + box.w - 26.0f, box.y - 14.0f,
             formatXY(kXYVertical, values_[kXYVertical.param]).c_str(), nullptr);
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
            fc(kVoxAccent);
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
            fc(kVoxAccent);
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

    void drawRoutingColumn(const float W)
    {
        const auto& th = theme();
        const float trackW = W - kCcX - kPad;

        for (std::uint32_t d = 0; d < kDropDefs.size(); ++d) {
            const DropDef& def = kDropDefs[d];
            const Rect box = dropBox(d);
            char current[40];
            dropItemText(def, dropCurrentItem(def, values_[def.index]), current, sizeof(current));

            const bool isOffCc = def.kind == kDropCc
                && static_cast<int>(std::lround(values_[def.index])) == 0;

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(th.textDim);
            text(kCcX, box.y - 17.0f, def.label, nullptr);
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fc(isOffCc ? th.textDisabled : th.textPrimary);
            text(kCcX + trackW, box.y - 17.0f, current, nullptr);
            drawDropBox(box, current);
        }

        const float hintY = kBodyY + static_cast<float>(kDropDefs.size()) * kDropH + 14.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDisabled);
        text(kCcX, hintY, "in 1/2 = carrier to analyse", nullptr);
        text(kCcX, hintY + 15.0f, "in 3/4 = modulator to colour", nullptr);
        text(kCcX, hintY + 30.0f, "Sync makes the ring mod feed", nullptr);
        text(kCcX, hintY + 45.0f, "the vocoder, so 3/4 alone works.", nullptr);

        const float mapY = hintY + 74.0f;
        fc(th.textDim);
        text(kCcX, mapY, "Drift lanes 1-4, then 5:", nullptr);
        for (std::size_t m = 0; m < kControllerMap.size(); ++m) {
            char line[64];
            std::snprintf(line, sizeof(line), "CC %d  %s",
                          static_cast<int>(kControllerMap[m].controller),
                          kControllerMap[m].label);
            fc(th.textPrimary);
            text(kCcX + 10.0f, mapY + 14.0f + static_cast<float>(m) * 13.0f, line, nullptr);
        }
    }

    // Live readouts the processor owns. These are measurements, not settings,
    // which is why they sit apart from the controls.
    void drawStatusReadout(const float H)
    {
        const auto& th = theme();
        const Rect box = { kVoxX, kXYTop, kVoxW, kXYSize };
        if (box.y + box.h > H - 12.0f)
            return;

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

        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textDim);
        text(box.x + 8.0f, box.y + 6.0f, "ANALYSIS", nullptr);

        const float carrierHz = values_[index(ParamId::outCarrierHz)];
        const float sibilance = values_[index(ParamId::outSibilance)];
        const float reduction = values_[index(ParamId::outReduction)];

        fontSize(10.0f);
        fc(th.textPrimary);
        char line[80];

        std::snprintf(line, sizeof(line), "Carrier  %.1f Hz",
                      static_cast<double>(carrierHz));
        text(box.x + 8.0f, box.y + 26.0f, line, nullptr);

        // Sibilance is a meter: too much carrier energy up top means the carrier
        // is brighter than a vocoder can follow.
        const Rect sibl = { box.x + 8.0f, box.y + 56.0f, box.w - 16.0f, 10.0f };
        fontSize(9.0f);
        fc(th.textDim);
        text(box.x + 8.0f, box.y + 42.0f, "Sibilance", nullptr);
        beginPath();
        fc(th.controlTrack);
        roundedRect(sibl.x, sibl.y, sibl.w, sibl.h, 5.0f);
        fill();
        closePath();
        beginPath();
        // Warn rather than merely report: above 0.7 the carrier is mostly hiss.
        if (sibilance > 0.7f)
            fc(kRingAccent);
        else
            fc(kReadoutAccent);
        roundedRect(sibl.x, sibl.y, std::max(sibl.h, sibl.w * clampf(sibilance, 0.0f, 1.0f)),
                    sibl.h, 5.0f);
        fill();
        closePath();

        fontSize(9.0f);
        fc(th.textDim);
        text(box.x + 8.0f, box.y + 76.0f, "Band gate", nullptr);
        std::snprintf(line, sizeof(line), "%.1f dB", static_cast<double>(reduction));
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        fc(th.textPrimary);
        text(box.x + box.w - 8.0f, box.y + 76.0f, line, nullptr);
        textAlign(ALIGN_LEFT | ALIGN_TOP);

        fontSize(8.5f);
        fc(th.textDisabled);
        text(box.x + 8.0f, box.y + 96.0f,
             "A very bright carrier cannot be analysed well.", nullptr);
    }

    void drawRandomiseButton()
    {
        const float W = static_cast<float>(getWidth());
        randomiseRect_ = { W - 250.0f, 16.0f, 148.0f, 26.0f };
        drawButton(randomiseRect_, "RANDOMISE", false);
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
        const Accent accent = kCcAccent;

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
                const float cw = listW / cols;
                const float cx = listX + c * cw;
                const float cy = listY + r * kItemH;
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
            const float thumbH = std::max(16.0f, trackH * rows / count);
            const float thumbY = trackY + (trackH - thumbH) * dropScroll_
                / std::max(1, count - rows);
            beginPath();
            roundedRect(trackX, thumbY, 5.0f, thumbH, 2.0f);
            fc(accent);
            fill();
            closePath();
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoxmodUI)
};

UI* createUI()
{
    return new VoxmodUI();
}

END_NAMESPACE_DISTRHO