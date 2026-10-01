#include "DistrhoUI.hpp"

#include "downspout/look_and_feel.hpp"
#include "treatment_core_types.hpp"
#include "treatment_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace laf = downspout::laf;

namespace {

using downspout::treatment::PanelState;
using downspout::treatment::ParamId;
using downspout::treatment::ParamSpec;
using downspout::treatment::kControllerMap;
using downspout::treatment::kParameterCount;
using downspout::treatment::kParameterSpecs;
using downspout::treatment::index;

constexpr float kPi = 3.14159265358979323846f;
constexpr float kMmPerM = 1000.0f;

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

// Same palette family as magneto: amber for panel geometry, cold steel for the
// CC routing column, olive for the derived readout.
constexpr Accent kPanelAccent { 198, 132, 58 };
constexpr Accent kCcAccent { 92, 140, 156 };
constexpr Accent kReadoutAccent { 124, 148, 104 };
constexpr int kCreamR = 250;
constexpr int kCreamG = 248;
constexpr int kCreamB = 242;

[[nodiscard]] float clampf(const float v, const float lo, const float hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

// Panel sliders, in the order they are drawn down the left column.
enum SliderId : std::uint32_t {
    kSliderCavity = 0,
    kSliderGap,
    kSliderMass,
    kSliderResist,
    kSliderAmount,
    kSliderCount,
};

constexpr std::array<std::uint32_t, kSliderCount> kSliderParams = {
    index(ParamId::cavity),
    index(ParamId::gap),
    index(ParamId::mass),
    index(ParamId::resist),
    index(ParamId::amount),
};

constexpr std::array<const char*, kSliderCount> kSliderLabels = {
    "Cavity", "Air Gap", "Facing Mass", "Flow Resist", "Amount",
};

constexpr std::array<const char*, kSliderCount> kSliderSuffix = {
    "mm", "mm", "kg/m2", "Rayl/m", "%",
};

struct DropDef {
    std::uint32_t index;
    const char* label;
    bool channel;
};

// CC routing dropdowns: five controller numbers plus the MIDI channel.
constexpr std::array<DropDef, 6> kDropDefs = {{
    { index(ParamId::ccAmount), "CC Amount", false },
    { index(ParamId::ccCavity), "CC Cavity", false },
    { index(ParamId::ccGap), "CC Gap", false },
    { index(ParamId::ccMass), "CC Mass", false },
    { index(ParamId::ccResist), "CC Resist", false },
    { index(ParamId::ccChannel), "CC Channel", true },
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

// Log-scale mapping: cavity 20-400 mm and resistivity 1-60 kRayl/m both span
// more than a decade, so a linear track would leave most of the range unusable.
[[nodiscard]] float sliderToValue(const std::uint32_t slider, const float t) noexcept
{
    const ParamSpec& spec = kParameterSpecs[kSliderParams[slider]];
    const bool logarithmic = slider == kSliderCavity || slider == kSliderResist;

    if (!logarithmic || spec.minimum <= 0.0f)
        return spec.minimum + t * (spec.maximum - spec.minimum);

    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return std::exp(lo + t * (hi - lo));
}

[[nodiscard]] float valueToSlider(const std::uint32_t slider, const float value) noexcept
{
    const ParamSpec& spec = kParameterSpecs[kSliderParams[slider]];
    const bool logarithmic = slider == kSliderCavity || slider == kSliderResist;

    if (!logarithmic || spec.minimum <= 0.0f)
        return clampf((value - spec.minimum) / (spec.maximum - spec.minimum), 0.0f, 1.0f);

    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return clampf((std::log(std::max(value, spec.minimum)) - lo) / (hi - lo), 0.0f, 1.0f);
}

[[nodiscard]] std::string formatSlider(const std::uint32_t slider, const float value)
{
    char buf[48];
    switch (slider) {
    case kSliderCavity:
    case kSliderGap:
        std::snprintf(buf, sizeof(buf), "%.0f mm", static_cast<double>(value));
        break;
    case kSliderMass:
        std::snprintf(buf, sizeof(buf), "%.2f kg/m2", static_cast<double>(value));
        break;
    case kSliderResist:
        if (value >= 1000.0f)
            std::snprintf(buf, sizeof(buf), "%.1f kRayl/m", static_cast<double>(value) / 1000.0);
        else
            std::snprintf(buf, sizeof(buf), "%.0f Rayl/m", static_cast<double>(value));
        break;
    case kSliderAmount:
        std::snprintf(buf, sizeof(buf), "%.0f%%", static_cast<double>(value));
        break;
    default:
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(value));
        break;
    }
    return buf;
}

[[nodiscard]] int dropItemCount(const bool channel) noexcept
{
    return channel ? 16 : 129;
}

void dropItemText(const bool channel, const int item, char* buf, const std::size_t size)
{
    if (channel) {
        std::snprintf(buf, size, "Ch %d", item + 1);
        return;
    }
    if (item == 0) {
        std::snprintf(buf, size, "off");
        return;
    }
    if (const char* common = kCcCommonNames(item))
        std::snprintf(buf, size, "%d %s", item, common);
    else
        std::snprintf(buf, size, "CC %d", item);
}

[[nodiscard]] int dropCurrentItem(const bool channel, const float value) noexcept
{
    return channel ? std::clamp(static_cast<int>(std::lround(value)) - 1, 0, 15)
                   : std::clamp(static_cast<int>(std::lround(value)), 0, 127);
}

[[nodiscard]] float dropItemValue(const bool channel, const int item) noexcept
{
    return channel ? static_cast<float>(item + 1) : static_cast<float>(item);
}

// ── Layout ─────────────────────────────────────────────────────────────────

constexpr float kPad = 16.0f;
constexpr float kHeaderH = 58.0f;
constexpr float kStripH = 18.0f;
constexpr float kSplitX = 424.0f;   // left column ends here
constexpr float kCcX = kSplitX + 14.0f;
constexpr float kBodyY = kHeaderH + kStripH + 14.0f;
constexpr float kSliderH = 44.0f;
constexpr float kDropH = 44.0f;
constexpr float kItemH = 20.0f;
constexpr int kMaxRows = 10;
constexpr int kTwoColLimit = 20;
constexpr float kTrackH = 12.0f;

// The plot's frequency axis and floor.
constexpr float kPlotMinHz = 20.0f;
constexpr float kPlotMaxHz = 20000.0f;
constexpr float kPlotFloorDb = -30.0f;
constexpr int kPlotSegments = 110;

// The Flow Resist slider carries a tick at the impedance optimum, so the
// panel's most important physical fact is visible rather than buried.
[[nodiscard]] float flowResistMatchPosition(const float cavity) noexcept
{
    const ParamSpec& spec = kParameterSpecs[index(ParamId::resist)];
    // Clamp the cavity against the CAVITY minimum, not the resistivity one:
    // 20 mm is the shallowest panel the plugin can build.
    const float cavityMetres = std::max(
        cavity, kParameterSpecs[index(ParamId::cavity)].minimum) * 0.001f;
    const float matched = clampf(downspout::treatment::kCharacteristicImpedance / cavityMetres,
                                 spec.minimum, spec.maximum);
    const float lo = std::log(spec.minimum);
    const float hi = std::log(spec.maximum);
    return clampf((std::log(matched) - lo) / (hi - lo), 0.0f, 1.0f);
}

// Log frequency axis, so the bass end gets the room it deserves.
[[nodiscard]] float plotXForHz(const Rect& plot, const float hz) noexcept
{
    const float t = (std::log(std::max(hz, kPlotMinHz)) - std::log(kPlotMinHz))
        / (std::log(kPlotMaxHz) - std::log(kPlotMinHz));
    return plot.x + clampf(t, 0.0f, 1.0f) * plot.w;
}

// 0 dB sits at the top of the plot and kPlotFloorDb at the bottom. kPlotFloorDb
// is negative, so dividing by it turns the dB reading into a fraction measured
// down from the top: 0 dB -> 0, floor -> 1.
[[nodiscard]] float plotYForDb(const Rect& plot, const float db) noexcept
{
    const float fractionDown = clampf(db / kPlotFloorDb, 0.0f, 1.0f);
    return plot.y + fractionDown * plot.h;
}

}  // namespace

class TreatmentUI : public UI
{
public:
    TreatmentUI()
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
        drawSplitter(H);
        drawPanelColumn();
        drawCcColumn(W);
        drawRandomiseButton();
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
            // processor, not the panel, decides what a new panel is.
            setParameterValue(index(ParamId::randomise), 1.0f);
            commitParameter(index(ParamId::randomise), 0.0f);
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

        const int count = dropItemCount(openChannel());
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

    // Accent colours are plain rgb triples rather than laf::Colour, so give the
    // section palette its own pair of helpers.
    void fc(const Accent& a, const int alpha = 255) { fillColor(a.r, a.g, a.b, alpha); }
    void sc(const Accent& a, const int alpha = 255) { strokeColor(a.r, a.g, a.b, alpha); }

    std::array<float, kParameterCount> values_ {};
    int dragSlider_ = -1;

    int openDrop_ = -1;
    int dropScroll_ = 0;
    int dropHover_ = -1;

    [[nodiscard]] bool popupOpen() const noexcept { return openDrop_ >= 0; }
    [[nodiscard]] const DropDef& openDef() const noexcept { return kDropDefs[static_cast<std::size_t>(openDrop_)]; }
    [[nodiscard]] bool openChannel() const noexcept { return openDef().channel; }

    // ── Hit geometry ─────────────────────────────────────────────────────

    [[nodiscard]] float sliderRowY(const std::uint32_t slider) const noexcept
    {
        return kBodyY + static_cast<float>(slider) * kSliderH;
    }

    [[nodiscard]] Rect sliderTrack(const std::uint32_t slider) const noexcept
    {
        return { kPad, sliderRowY(slider) + 19.0f, kSplitX - 2.0f * kPad, kTrackH };
    }

    [[nodiscard]] Rect bypassBox() const noexcept
    {
        return { kPad, sliderRowY(kSliderCount) + 22.0f, kSplitX - 2.0f * kPad, 34.0f };
    }

    [[nodiscard]] Rect ccDropBox(const std::uint32_t drop) const noexcept
    {
        const float W = static_cast<float>(getWidth());
        return { kCcX, kBodyY + static_cast<float>(drop) * kDropH + 19.0f, W - kCcX - kPad, 22.0f };
    }

    [[nodiscard]] int dropHeaderAt(const float mx, const float my) const noexcept
    {
        for (std::uint32_t d = 0; d < kDropDefs.size(); ++d) {
            if (ccDropBox(d).contains(mx, my))
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

    void openDrop(const int id)
    {
        openDrop_ = id;
        const DropDef& def = openDef();
        const int current = dropCurrentItem(def.channel, values_[def.index]);
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
        commitParameter(def.index, dropItemValue(def.channel, item));
    }

    [[nodiscard]] bool dropScrollable() const noexcept
    {
        return dropItemCount(openChannel()) > kTwoColLimit;
    }
    [[nodiscard]] int dropCols() const noexcept { return dropScrollable() ? 1 : 2; }
    [[nodiscard]] int dropRows() const noexcept
    {
        const int count = dropItemCount(openChannel());
        return dropScrollable() ? kMaxRows : (count + 1) / 2;
    }

    [[nodiscard]] Rect popupRect(const float W, const float H) const noexcept
    {
        const Rect header = ccDropBox(static_cast<std::uint32_t>(openDrop_));
        const int rows = dropRows();
        const float popH = static_cast<float>(rows) * kItemH + 8.0f;
        const float popW = std::max(header.w, 200.0f);
        float x = std::min(header.x, W - popW - 8.0f);
        float y = header.y + header.h + 3.0f;
        if (y + popH > H - 8.0f)
            y = header.y - 3.0f - popH;
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
        const int count = dropItemCount(openChannel());

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

    // ── Derived panel state, recomputed from the same model as the DSP ────

    [[nodiscard]] downspout::treatment::Parameters coreParameters() const noexcept
    {
        downspout::treatment::Parameters p;
        p.cavity = values_[index(ParamId::cavity)];
        p.gap = values_[index(ParamId::gap)];
        p.mass = values_[index(ParamId::mass)];
        p.resist = values_[index(ParamId::resist)];
        p.amount = values_[index(ParamId::amount)];
        p.bypass = values_[index(ParamId::bypass)];
        p.seed = values_[index(ParamId::seed)];
        return p;
    }

    // ── Drawing ──────────────────────────────────────────────────────────

    void drawBackground(const float W, const float H)
    {
        const auto& t = theme();
        beginPath();
        fc(t.background);
        rect(0, 0, W, H);
        fill();
        closePath();
    }

    void drawHeader(const float W)
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
        text(kPad, 27.0f, "TREATMENT", nullptr);

        fontSize(11.0f);
        fc(t.textDim);
        text(kPad + 168.0f, 27.0f,
             "mass-air-mass absorber \xc2\xb7 porous fill \xc2\xb7 Drift CC", nullptr);

        beginPath();
        sc(t.controlTrack);
        strokeWidth(1.0f);
        moveTo(0, kHeaderH);
        lineTo(W, kHeaderH);
        stroke();
        closePath();

        themeRect_ = { W - 90.0f, 16.0f, 74.0f, 26.0f };
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

    void drawColumnStrips(const float W)
    {
        drawColumnStrip(kPad, kSplitX - 2.0f * kPad, "PANEL", kPanelAccent);
        drawColumnStrip(kCcX, W - kCcX - kPad, "DRIFT CC ROUTING", kCcAccent);
    }

    void drawColumnStrip(const float x, const float w, const char* title, const Accent& accent)
    {
        beginPath();
        fillColor(accent.r, accent.g, accent.b, 255);
        roundedRect(x, kHeaderH + 1.0f, w, kStripH, 2.0f);
        fill();
        closePath();
        fillColor(kCreamR, kCreamG, kCreamB, 255);
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(x + 8.0f, kHeaderH + 1.0f + kStripH * 0.5f, title, nullptr);
    }

    void drawSplitter(const float H)
    {
        const auto& t = theme();
        beginPath();
        sc(t.panel);
        strokeWidth(1.0f);
        moveTo(kSplitX, kHeaderH);
        lineTo(kSplitX, H);
        stroke();
        closePath();
    }

    void drawTrack(const Rect& tr, const float norm)
    {
        const auto& t = theme();
        beginPath();
        fc(t.controlTrack);
        roundedRect(tr.x, tr.y, tr.w, tr.h, 6.0f);
        fill();
        closePath();

        if (norm > 0.001f) {
            beginPath();
            fc(kPanelAccent);
            roundedRect(tr.x, tr.y, std::max(tr.h, tr.w * norm), tr.h, 6.0f);
            fill();
            closePath();
        }
    }

    void drawLabelValue(const float x, const float y, const float w,
                        const char* label, const char* value)
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

    // The absorption curve the panel is currently applying. Drawing it from the
    // same core functions the DSP uses means the picture cannot drift from the
    // sound, and it makes the plugin's physical claim visible.
    // Narrower than the left column so the dB scale has room on the right and
    // the decade labels fit along the bottom.
    [[nodiscard]] Rect plotBox() const noexcept
    {
        return { kPad, 378.0f, kSplitX - 2.0f * kPad, 128.0f };
    }

    void drawPanelColumn()
    {
        const float trackW = kSplitX - 2.0f * kPad;

        for (std::uint32_t s = 0; s < kSliderCount; ++s) {
            const Rect track = sliderTrack(s);
            const std::uint32_t parameter = kSliderParams[s];
            drawLabelValue(kPad, track.y - 17.0f, trackW, kSliderLabels[s],
                           formatSlider(s, values_[parameter]).c_str());
            drawTrack(track, valueToSlider(s, values_[parameter]));

            // Mark the impedance optimum on Flow Resist. This is the one
            // control where the physical answer is non-obvious, so the panel
            // should say where it is rather than making the user hunt.
            if (s == kSliderResist)
                drawMatchTick(track);
        }

        drawSwitchRow(bypassBox(), "Bypass",
                      values_[index(ParamId::bypass)] >= 0.5f, "bypassed", "active");

        drawAbsorptionPlot(plotBox());
        drawMatchLegend(plotBox());

        const PanelState panel = downspout::treatment::analysePanel(coreParameters());
        const Rect plot = plotBox();
        const float readoutY = plot.y + plot.h + 22.0f;
        const float totalMm = values_[index(ParamId::cavity)] + values_[index(ParamId::gap)];

        char line[112];
        std::snprintf(line, sizeof(line), "total depth %.0f mm", static_cast<double>(totalMm));
        textAt(kPad, readoutY, t().textDim, line);

        std::snprintf(line, sizeof(line), "peak absorption %.0f%%",
                      static_cast<double>(panel.peakAbsorb * 100.0f));
        textAt(kPad, readoutY + 13.0f, t().textDim, line);

        std::snprintf(line, sizeof(line), "Q %.1f", static_cast<double>(panel.q));
        textAt(kPad + trackW, readoutY, t().textDim, line);

        std::snprintf(line, sizeof(line), "seed %d",
                      static_cast<int>(values_[index(ParamId::seed)]));
        textAt(kPad + trackW, readoutY + 13.0f, t().textDim, line);
    }

    // Say what the plot is and what the tick on Flow Resist means, since both
    // are the non-obvious parts of this panel.
    void drawMatchLegend(const Rect plot)
    {
        const auto& th = theme();

        // Title above the frame: the curve reaches the top-left corner, so
        // anything drawn inside would sit on top of it.
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_BOTTOM);
        fc(th.textDim);
        text(plot.x, plot.y - 5.0f, "ABSORPTION", nullptr);

        // Scale key, bottom-right, so the vertical axis is readable.
        fontSize(8.0f);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        fc(th.textDisabled);
        // Vertical axis ends, inside the frame's right edge where there is
        // always empty space above the curve's return to unity.
        fontSize(8.0f);
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        fc(th.textDim);
        const float x = plot.x + plot.w - 4.0f;
        text(x, plot.y + 11.0f, "0 dB", nullptr);
        text(x, plot.y + plot.h - 2.0f, "-30 dB", nullptr);

        // Explain the Flow Resist tick in place, under the slider it belongs to
        // and above the Bypass row, so neither label is crowded.
        const float y = bypassBox().y - 8.0f;
        textAlign(ALIGN_RIGHT | ALIGN_BOTTOM);
        fontSize(8.5f);
        fc(th.textDisabled);
        const Rect track = sliderTrack(kSliderResist);
        text(track.x + track.w, y, "tick = impedance match", nullptr);
    }

    [[nodiscard]] const laf::Theme& t() const noexcept { return theme(); }

    void textAt(const float x, const float y, const laf::Colour& colour, const char* s)
    {
        fontSize(10.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(colour);
        text(x, y, s, nullptr);
    }

    void drawMatchTick(const Rect& track)
    {
        const auto& th = theme();
        const float t = flowResistMatchPosition(values_[index(ParamId::cavity)]);
        const float x = track.x + track.w * t;

        // A tick through the groove, light enough not to read as a value.
        beginPath();
        sc(th.textDim);
        strokeWidth(2.0f);
        moveTo(x, track.y - 5.0f);
        lineTo(x, track.y + track.h + 5.0f);
        stroke();
        closePath();
    }

    void drawAbsorptionPlot(const Rect plot)
    {
        const auto& th = theme();
        const PanelState panel = downspout::treatment::analysePanel(coreParameters());
        const downspout::treatment::PanelResponse response =
            downspout::treatment::panelCoeffs(coreParameters(), 48000.0);

        // Frame and 0 dB reference line.
        beginPath();
        fc(th.panel);
        roundedRect(plot.x, plot.y, plot.w, plot.h, 2.0f);
        fill();
        closePath();
        beginPath();
        sc(th.border);
        strokeWidth(1.0f);
        roundedRect(plot.x + 0.5f, plot.y + 0.5f, plot.w - 1.0f, plot.h - 1.0f, 2.0f);
        stroke();
        closePath();

        const float zeroY = plotYForDb(plot, 0.0f);
        beginPath();
        sc(th.border);
        strokeWidth(1.0f);
        moveTo(plot.x, zeroY);
        lineTo(plot.x + plot.w, zeroY);
        stroke();
        closePath();

        // Decade ticks, so the axis is readable without a scale strip.
        fontSize(8.0f);
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        for (float hz = 100.0f; hz <= 10000.0f; hz *= 10.0f) {
            const float x = plotXForHz(plot, hz);
            beginPath();
            sc(th.border);
            strokeWidth(1.0f);
            moveTo(x, plot.y + plot.h);
            lineTo(x, plot.y + plot.h - 4.0f);
            stroke();
            closePath();
            char buf[16];
            std::snprintf(buf, sizeof(buf), hz >= 1000.0f ? "%gk" : "%g",
                          static_cast<double>(hz) / (hz >= 1000.0f ? 1000.0 : 1.0));
            fc(th.textDim);
            text(x, plot.y + plot.h + 4.0f, buf, nullptr);
        }

        // The curve itself, as filled area against 0 dB.
        beginPath();
        moveTo(plot.x, zeroY);
        for (int i = 0; i <= kPlotSegments; ++i) {
            const float t = static_cast<float>(i) / kPlotSegments;
            const float x = plot.x + t * plot.w;
            const float hz = std::exp(std::log(kPlotMinHz)
                + t * (std::log(kPlotMaxHz) - std::log(kPlotMinHz)));
            const float mag = downspout::treatment::transmissionAt(response, hz, 48000.0);
            const float db = 20.0f * std::log10(std::max(mag, 1.0e-4f));
            lineTo(x, plotYForDb(plot, db));
        }
        lineTo(plot.x + plot.w, zeroY);
        closePath();
        fc(kPanelAccent, 55);
        fill();

        beginPath();
        for (int i = 0; i <= kPlotSegments; ++i) {
            const float t = static_cast<float>(i) / kPlotSegments;
            const float x = plot.x + t * plot.w;
            const float hz = std::exp(std::log(kPlotMinHz)
                + t * (std::log(kPlotMaxHz) - std::log(kPlotMinHz)));
            const float mag = downspout::treatment::transmissionAt(response, hz, 48000.0);
            const float db = 20.0f * std::log10(std::max(mag, 1.0e-4f));
            const float y = plotYForDb(plot, db);
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        sc(kPanelAccent);
        strokeWidth(2.0f);
        stroke();
        closePath();

        // Mark the resonance, so the number above and the dip below agree.
        const float rx = plotXForHz(plot, panel.resonanceHz);
        const float rmag = downspout::treatment::transmissionAt(response, panel.resonanceHz, 48000.0);
        const float ry = plotYForDb(plot, 20.0f * std::log10(std::max(rmag, 1.0e-4f)));
        beginPath();
        sc(th.textDim);
        strokeWidth(1.0f);
        moveTo(rx, plot.y + 2.0f);
        lineTo(rx, plot.y + plot.h);
        stroke();
        closePath();
        beginPath();
        fc(kPanelAccent);
        circle(rx, ry, 3.5f);
        fill();
        closePath();

        // Label, flipped to whichever side has room.
        fontSize(9.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(th.textPrimary);
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f Hz", static_cast<double>(panel.resonanceHz));
        const bool labelLeft = rx > plot.x + plot.w * 0.6f;
        text(labelLeft ? rx - 62.0f : rx + 6.0f, plot.y + 4.0f, label, nullptr);
    }

    void drawSwitchRow(const Rect& box, const char* label, const bool on,
                       const char* onText, const char* offText)
    {
        const auto& t = theme();
        fontSize(11.0f);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fc(t.textDim);
        text(box.x, box.y + box.h * 0.5f, label, nullptr);

        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(box.x + box.w - 52.0f, box.y + box.h * 0.5f, on ? onText : offText, nullptr);

        const float x = box.x + box.w - 44.0f;
        const float y = box.y + (box.h - 20.0f) * 0.5f;
        beginPath();
        roundedRect(x, y, 44.0f, 20.0f, 10.0f);
        if (on)
            fc(kPanelAccent);
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
        fc(t.buttonFace);
        roundedRect(box.x, box.y, box.w, box.h, 4.0f);
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

    void drawCcColumn(const float W)
    {
        const auto& t = theme();
        const float trackW = W - kCcX - kPad;

        for (std::uint32_t d = 0; d < kDropDefs.size(); ++d) {
            const DropDef& def = kDropDefs[d];
            const Rect box = ccDropBox(d);
            char current[32];
            dropItemText(def.channel, dropCurrentItem(def.channel, values_[def.index]),
                         current, sizeof(current));

            const bool isOff = !def.channel
                && static_cast<int>(std::lround(values_[def.index])) == 0;

            fontSize(11.0f);
            textAlign(ALIGN_LEFT | ALIGN_TOP);
            fc(t.textDim);
            text(kCcX, box.y - 17.0f, def.label, nullptr);
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_TOP);
            fc(isOff ? t.textDisabled : t.textPrimary);
            text(kCcX + trackW, box.y - 17.0f, current, nullptr);
            drawDropBox(box, current);
        }

        const float hintY = kBodyY + static_cast<float>(kDropDefs.size()) * kDropH + 14.0f;
        fontSize(9.5f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        fc(t.textDisabled);
        text(kCcX, hintY, "Route Drift MIDI out \xe2\x86\x92 Treatment MIDI in.", nullptr);
        text(kCcX, hintY + 15.0f, "off disables that control's override.", nullptr);

        // Spell the map out rather than making the user find it: CC 1-4 are
        // Drift's default lanes, CC 5 continues the block.
        const float mapY = hintY + 44.0f;
        fc(t.textDim);
        text(kCcX, mapY, "Drift lanes 1-4, then 5:", nullptr);
        for (std::size_t m = 0; m < kControllerMap.size(); ++m) {
            char line[64];
            std::snprintf(line, sizeof(line), "CC %d  %s",
                          static_cast<int>(kControllerMap[m].controller),
                          kControllerMap[m].label);
            fc(t.textPrimary);
            text(kCcX + 10.0f, mapY + 14.0f + static_cast<float>(m) * 13.0f, line, nullptr);
        }

        fc(t.textDisabled);
        text(kCcX, mapY + 14.0f + static_cast<float>(kControllerMap.size()) * 13.0f + 8.0f,
             "Geometry changes move the resonance live.", nullptr);
    }

    void drawRandomiseButton()
    {
        const auto& t = theme();
        const float W = static_cast<float>(getWidth());
        randomiseRect_ = { kCcX, 16.0f, 148.0f, 26.0f };

        beginPath();
        fc(t.buttonFace);
        roundedRect(randomiseRect_.x, randomiseRect_.y, randomiseRect_.w, randomiseRect_.h, 2.0f);
        fill();
        closePath();

        fontSize(11.0f);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fc(t.textPrimary);
        text(randomiseRect_.x + randomiseRect_.w * 0.5f,
             randomiseRect_.y + randomiseRect_.h * 0.5f, "RANDOMISE PANEL", nullptr);

        // Keep the button clear of the theme toggle on narrow windows.
        if (randomiseRect_.x + randomiseRect_.w > W - 96.0f) {
            randomiseRect_.x = W - 96.0f - randomiseRect_.w;
        }
    }

    void drawPopup(const float W, const float H)
    {
        const auto& t = theme();
        const Rect popup = popupRect(W, H);
        const bool channel = openChannel();
        const int count = dropItemCount(channel);
        const bool scrollable = dropScrollable();
        const int cols = dropCols();
        const int rows = dropRows();
        const int current = dropCurrentItem(channel, values_[openDef().index]);
        const Accent accent = kCcAccent;

        beginPath();
        fillColor(0, 0, 0, 110);
        rect(0, 0, W, H);
        fill();
        closePath();

        beginPath();
        fc(t.surface);
        roundedRect(popup.x, popup.y, popup.w, popup.h, 4.0f);
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
                        fillColor(accent.r, accent.g, accent.b, 255);
                    else
                        fillColor(accent.r, accent.g, accent.b, 90);
                    fill();
                    closePath();
                }
                char itemText[32];
                dropItemText(channel, item, itemText, sizeof(itemText));
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
            const float trackH = static_cast<float>(rows) * kItemH;
            beginPath();
            fc(t.controlTrack);
            roundedRect(trackX, trackY, 5.0f, trackH, 2.0f);
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TreatmentUI)
};

UI* createUI()
{
    return new TreatmentUI();
}

END_NAMESPACE_DISTRHO
