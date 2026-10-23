#include "downspout/dpf/MagnetoKit.hpp"

#include "trio_core.hpp"
#include "trio_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::trio;

namespace {

constexpr kit::Accent kPlotAccent {198, 132, 58};    // amber
constexpr kit::Accent kGlobalAccent {120, 132, 96};  // sage
constexpr std::array<kit::Accent, core::kBands> kBandAccent = {{{176, 90, 70}, {198, 132, 58}, {92, 140, 156}}};
constexpr std::array<const char*, core::kBands> kBandNames = {"LOW", "MID", "HIGH"};

constexpr float kPlotMinHz = 20.0f, kPlotMaxHz = 20000.0f;
constexpr float kMeterRangeDb = 24.0f;  // the gain bars run +-24 dB

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::specs()[i];
            ranges[i] = {s.minimum, s.maximum, s.integer, s.logarithmic};
            defaults[i] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class TrioUI : public MagnetoUI
{
public:
    TrioUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no audio; let it show a plausible picture.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            const float gains[core::kBands] = {-6.5f, -9.0f, 4.5f};
            const float levels[core::kBands] = {-19.0f, -14.0f, -38.0f};
            for (int b = 0; b < core::kBands; ++b) {
                setValue(core::gainMeter(b), gains[b]);
                setValue(core::levelMeter(b), levels[b]);
            }
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_ && index >= core::kControlCount)
            return;  // keep the demo meters
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(const int /*id*/) override {}

    // Drag a crossover line on the plot.
    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1) {
            if (!ev.press) {
                dragging_ = -1;
                return MagnetoUI::onMouse(ev);
            }
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
            if (plot_.contains(mx, my)) {
                const float dl = std::fabs(mx - xOf(value(core::kCrossLow)));
                const float dh = std::fabs(mx - xOf(value(core::kCrossHigh)));
                if (std::min(dl, dh) < 14.0f) {
                    dragging_ = dl <= dh ? core::kCrossLow : core::kCrossHigh;
                    drag(mx);
                    return true;
                }
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragging_ >= 0) {
            drag(static_cast<float>(ev.pos.getX()));
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("TRIO", "three-band dynamics",
                   "Each band compresses what is loud (down) and lifts what is quiet (up). Drag a crossover line on the plot to move it.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawPlotPanel({24.0f, 108.0f, full, 190.0f});
        const float gap = 12.0f;
        const float colW = (full - 2.0f * gap) / 3.0f;
        for (int b = 0; b < core::kBands; ++b)
            drawBandPanel(b, {24.0f + static_cast<float>(b) * (colW + gap), 310.0f, colW, 342.0f});
        drawGlobalPanel({24.0f, 664.0f, full, 84.0f});
    }

private:
    [[nodiscard]] float xOf(const float hz) const
    {
        const float n = std::log(std::clamp(hz, kPlotMinHz, kPlotMaxHz) / kPlotMinHz) / std::log(kPlotMaxHz / kPlotMinHz);
        return plot_.x + n * plot_.w;
    }
    [[nodiscard]] float hzOf(const float x) const
    {
        const float n = std::clamp((x - plot_.x) / std::max(1.0f, plot_.w), 0.0f, 1.0f);
        return kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
    }

    void drag(const float mx) { commit(static_cast<std::uint32_t>(dragging_), hzOf(mx)); }

    void drawPlotPanel(const Rect b)
    {
        drawPanel(b, "BANDS", kPlotAccent, "bars: gain each band is applying now");
        const auto& t = theme();
        plot_ = {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, b.h - 44.0f - 26.0f};
        drawPlotFrame(plot_);

        const float xl = xOf(value(core::kCrossLow)), xh = xOf(value(core::kCrossHigh));
        const std::array<std::array<float, 2>, core::kBands> span = {{{plot_.x, xl}, {xl, xh}, {xh, plot_.x + plot_.w}}};
        const float mid = plot_.y + plot_.h * 0.5f;

        for (int i = 0; i < core::kBands; ++i) {
            const auto& a = kBandAccent[static_cast<size_t>(i)];
            const bool on = intValue(core::bandParameter(i, core::kOn)) == 1;
            // Band region.
            beginPath();
            fillColor(a.r, a.g, a.b, on ? 40 : 14);
            rect(span[static_cast<size_t>(i)][0], plot_.y, span[static_cast<size_t>(i)][1] - span[static_cast<size_t>(i)][0], plot_.h);
            fill();
            // Gain bar: down from the centre line for reduction, up for lift.
            const float gain = value(core::gainMeter(i));
            const float cx = 0.5f * (span[static_cast<size_t>(i)][0] + span[static_cast<size_t>(i)][1]);
            const float w = std::min(46.0f, 0.6f * (span[static_cast<size_t>(i)][1] - span[static_cast<size_t>(i)][0]));
            const float h = std::clamp(gain / kMeterRangeDb, -1.0f, 1.0f) * (plot_.h * 0.5f - 6.0f);
            if (on && std::fabs(h) > 0.5f) {
                beginPath();
                fillColor(a.r, a.g, a.b, 230);
                rect(cx - w * 0.5f, h >= 0.0f ? mid - h : mid, w, std::fabs(h));
                fill();
            }
            char buf[40];
            std::snprintf(buf, sizeof(buf), on ? "%+.1f dB" : "off", static_cast<double>(gain));
            label(cx, plot_.y + 6.0f, kBandNames[static_cast<size_t>(i)], false, 12.0f, ALIGN_CENTER | ALIGN_TOP);
            label(cx, plot_.y + plot_.h - 6.0f, buf, false, 12.0f, ALIGN_CENTER | ALIGN_BOTTOM);
            std::snprintf(buf, sizeof(buf), "level %.0f dB", static_cast<double>(value(core::levelMeter(i))));
            label(cx, plot_.y + 24.0f, value(core::levelMeter(i)) > -100.0f ? buf : "no signal", true, 10.0f,
                  ALIGN_CENTER | ALIGN_TOP);
        }
        // Zero line.
        beginPath();
        sc(t.border);
        strokeWidth(1.2f);
        moveTo(plot_.x, mid);
        lineTo(plot_.x + plot_.w, mid);
        stroke();

        // Crossover lines and their values.
        for (int k = 0; k < 2; ++k) {
            const float x = k == 0 ? xl : xh;
            beginPath();
            strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 255);
            strokeWidth(2.2f);
            moveTo(x, plot_.y);
            lineTo(x, plot_.y + plot_.h);
            stroke();
            const float hz = value(k == 0 ? core::kCrossLow : core::kCrossHigh);
            char buf[24];
            if (hz >= 1000.0f) std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(hz) / 1000.0);
            else std::snprintf(buf, sizeof(buf), "%.0f Hz", static_cast<double>(hz));
            label(x, plot_.y + plot_.h + 5.0f, buf, false, 11.0f, ALIGN_CENTER | ALIGN_TOP);
        }
    }

    void drawBandPanel(const int band, const Rect b)
    {
        const auto& accent = kBandAccent[static_cast<size_t>(band)];
        drawPanel(b, kBandNames[static_cast<size_t>(band)], accent);
        static constexpr const char* kOnNames[2] = {"Off", "On"};
        const float pad = 14.0f;
        const float w = b.w - 2.0f * pad;
        drawSegments(core::bandParameter(band, core::kOn), kOnNames, 2, {b.x + pad, b.y + 42.0f, w, 24.0f}, accent);
        const bool on = intValue(core::bandParameter(band, core::kOn)) == 1;
        setDim(!on);
        float y = b.y + 78.0f;
        const float pitch = 36.0f;
        auto row = [&](core::BandParameter p, const char* name) {
            const std::uint32_t id = core::bandParameter(band, p);
            drawSlider(id, name, format(id).c_str(), {b.x + pad, y, w, 28.0f}, accent);
            y += pitch;
        };
        row(core::kDownThresh, "Down threshold");
        row(core::kDownRatio, "Down ratio");
        row(core::kUpThresh, "Up threshold");
        row(core::kUpRatio, "Up ratio (1 = no lift)");
        row(core::kAttack, "Attack");
        row(core::kRelease, "Release");
        row(core::kGain, "Band gain");
        setDim(false);
    }

    void drawGlobalPanel(const Rect b)
    {
        drawPanel(b, "GLOBAL", kGlobalAccent);
        const float gap = 20.0f;
        const float w = (b.w - 28.0f - 4.0f * gap) / 5.0f;
        float x = b.x + 14.0f;
        const float y = b.y + 42.0f;
        for (const auto& [id, name] : {std::pair<std::uint32_t, const char*>{core::kInput, "Input"},
                                       {core::kAmount, "Amount"},
                                       {core::kTime, "Time scale"},
                                       {core::kUpLimit, "Upward limit"},
                                       {core::kOutput, "Output"}}) {
            drawSlider(id, name, format(id).c_str(), {x, y, w, 30.0f}, kGlobalAccent);
            x += w + gap;
        }
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
        if (parameter < core::kFirstBand) {
            switch (parameter) {
            case core::kInput:
            case core::kOutput:
                std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
                break;
            case core::kUpLimit:
                std::snprintf(buffer, sizeof(buffer), "%.0f dB", static_cast<double>(v));
                break;
            case core::kTime:
                std::snprintf(buffer, sizeof(buffer), "x %.2f", static_cast<double>(v));
                break;
            default:
                std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
                break;
            }
            return buffer;
        }
        switch ((parameter - core::kFirstBand) % core::kPerBand) {
        case core::kDownThresh:
        case core::kUpThresh:
            std::snprintf(buffer, sizeof(buffer), "%.0f dB", static_cast<double>(v));
            break;
        case core::kDownRatio:
        case core::kUpRatio:
            std::snprintf(buffer, sizeof(buffer), "%.1f : 1", static_cast<double>(v));
            break;
        case core::kAttack:
            std::snprintf(buffer, sizeof(buffer), v < 10.0f ? "%.1f ms" : "%.0f ms", static_cast<double>(v));
            break;
        case core::kRelease:
            std::snprintf(buffer, sizeof(buffer), "%.0f ms", static_cast<double>(v));
            break;
        case core::kGain:
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            break;
        }
        return buffer;
    }

    Rect plot_ {};
    int dragging_ = -1;
    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrioUI)
};

UI* createUI()
{
    return new TrioUI();
}

END_NAMESPACE_DISTRHO
