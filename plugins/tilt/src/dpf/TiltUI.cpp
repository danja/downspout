#include "downspout/dpf/MagnetoKit.hpp"

#include "tilt_engine.hpp"
#include "tilt_params.hpp"
#include "tilt_spectrum.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::tilt;

constexpr kit::Accent kResponseAccent {198, 132, 58};  // amber: the response curve
constexpr kit::Accent kTiltAccent {92, 140, 156};      // steel: pivot and angle
constexpr kit::Accent kOutputAccent {120, 140, 92};    // olive: level and meters
constexpr float kPlotMinHz = 20.0f, kPlotMaxHz = 20000.0f;
constexpr float kPlotDb = 24.0f;

constexpr std::uint32_t pid(const core::ParamId id) noexcept { return static_cast<std::uint32_t>(id); }

struct Tables {
    std::array<kit::Range, core::kSpectrumFirst> ranges {};
    std::array<float, core::kSpectrumFirst> defaults {};

    Tables()
    {
        for (std::size_t i = 0; i < core::kSpectrumFirst; ++i) {
            const core::ParamSpec& spec = core::specs()[i];
            ranges[i] = {spec.minimum, spec.maximum, spec.integer, spec.logarithmic};
            defaults[i] = spec.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

}  // namespace

class TiltUI : public MagnetoUI {
public:
    static_assert(core::kSpectrumFirst <= kMaxValues, "the controls must fit MagnetoUI's value table");

    TiltUI()
        : MagnetoUI(tables().ranges.data(), core::kSpectrumFirst, tables().defaults.data())
    {
        spectrum_.fill(core::kSpectrumFloorDb);
        engine_.sampleRate = 48000.0;
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            setValue(pid(core::ParamId::pivot), 1200.0f);
            setValue(pid(core::ParamId::angle), 0.55f);
            setValue(pid(core::ParamId::slope), 2.0f);
            setValue(pid(core::ParamId::mode), 0.0f);
            setValue(pid(core::ParamId::outputDb), 0.0f);
            setValue(pid(core::ParamId::spectrumSource), 2.0f);
            for (std::uint32_t k = 0; k < core::kSpectrumBins; ++k) {
                const double hz = std::sqrt(core::spectrumBandEdgeHz(static_cast<int>(k)) *
                                            core::spectrumBandEdgeHz(static_cast<int>(k) + 1));
                double db = -24.0 - 8.0 * std::log2(hz / 100.0);
                db += 5.0 * std::sin(static_cast<double>(k) * 1.3) * std::sin(static_cast<double>(k) * 0.37);
                spectrum_[k] = static_cast<float>(std::clamp(db, -80.0, 0.0));
            }
            setValue(pid(core::ParamId::outPeakL), -8.5f);
            setValue(pid(core::ParamId::outPeakR), -10.0f);
            setValue(core::kOutLatency, 0.0f);
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float value) override
    {
        if (demo_ && index >= core::kInputParameterCount)
            return;
        if (core::isSpectrumBin(index)) {
            spectrum_[index - core::kSpectrumFirst] = value;
            repaint();
            return;
        }
        if (core::isMeter(index) || index == core::kOutLatency) {
            MagnetoUI::parameterChanged(index, value);
            return;
        }
        MagnetoUI::parameterChanged(index, value);
    }

    void onAction(const int) override {}

    bool onMouse(const MouseEvent& ev) override
    {
        const float mx = static_cast<float>(ev.pos.getX());
        const float my = static_cast<float>(ev.pos.getY());
        if (ev.button == 1 && ev.press && plot_.contains(mx, my)) {
            // Close to the pivot marker: drag horizontally only. Elsewhere: place both controls.
            draggingPivot_ = std::fabs(mx - xOf(value(pid(core::ParamId::pivot)))) < 22.0f;
            applyPlot(mx, my, true);
            return true;
        }
        if (!ev.press)
            dragging_ = false;
        return MagnetoUI::onMouse(ev);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (dragging_) {
            applyPlot(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()), false);
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (!plot_.contains(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY())))
            return MagnetoUI::onScroll(ev);
        const std::uint32_t pAngle = pid(core::ParamId::angle);
        commit(pAngle, value(pAngle) + 0.05f * static_cast<float>(ev.delta.getY()));
        return true;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("TILT", "master-bus tone control",
                   "Click or drag the plot: across for Pivot Freq, up and down for Angle. Scroll for fine angle.");

        drawPlotPanel({24.0f, 108.0f, 872.0f, 296.0f});
        drawControlPanel({24.0f, 416.0f, 872.0f, 120.0f});
        drawOutputPanel({24.0f, 548.0f, 872.0f, 132.0f});
    }

private:
    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kInputParameterCount; ++i)
            core::field(p, i) = value(i);
        return core::clampParameters(p);
    }

    [[nodiscard]] float xOf(const float hz) const
    {
        const float n = std::log(std::clamp(hz, kPlotMinHz, kPlotMaxHz) / kPlotMinHz) /
                        std::log(kPlotMaxHz / kPlotMinHz);
        return plot_.x + n * plot_.w;
    }

    [[nodiscard]] float hzOf(const float x) const
    {
        const float n = std::clamp((x - plot_.x) / std::max(1.0f, plot_.w), 0.0f, 1.0f);
        return kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
    }

    [[nodiscard]] float yOf(const float db) const
    {
        return plot_.y + (kPlotDb - std::clamp(db, -kPlotDb, kPlotDb)) / (2.0f * kPlotDb) * plot_.h;
    }

    void applyPlot(const float x, const float y, const bool initial)
    {
        if (!draggingPivot_ || initial)
            commit(pid(core::ParamId::pivot), hzOf(x));
        if (!draggingPivot_)
            commit(pid(core::ParamId::angle),
                   std::clamp((plot_.y + plot_.h * 0.5f - y) / (plot_.h * 0.5f), -1.0f, 1.0f));
        dragging_ = true;
    }

    void drawPlotPanel(const Rect b)
    {
        drawPanel(b, "RESPONSE", kResponseAccent, "straight-line tilt over the live spectrum");
        const auto& t = theme();
        plot_ = {b.x + 48.0f, b.y + 52.0f, b.w - 66.0f, b.h - 82.0f};
        drawPlotFrame(plot_);

        for (int db = -24; db <= 24; db += 12) {
            beginPath();
            sc(t.border);
            strokeWidth(db == 0 ? 1.8f : 0.8f);
            moveTo(plot_.x, yOf(static_cast<float>(db)));
            lineTo(plot_.x + plot_.w, yOf(static_cast<float>(db)));
            stroke();
            if (db % 24 == 0) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "%+d", db);
                label(plot_.x - 8.0f, yOf(static_cast<float>(db)), buf, true, 10.0f,
                      ALIGN_RIGHT | ALIGN_MIDDLE);
            }
        }
        static constexpr float kMarks[] = {20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                           2000.0f, 5000.0f, 10000.0f, 20000.0f};
        for (const float hz : kMarks) {
            beginPath();
            sc(t.border);
            strokeWidth(0.8f);
            moveTo(xOf(hz), plot_.y);
            lineTo(xOf(hz), plot_.y + plot_.h);
            stroke();
            char buf[16];
            if (hz >= 1000.0f)
                std::snprintf(buf, sizeof(buf), "%gk", static_cast<double>(hz) / 1000.0);
            else
                std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(hz));
            label(xOf(hz), plot_.y + plot_.h + 6.0f, buf, true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
        }

        drawSpectrum();

        const core::Parameters p = currentParameters();
        engine_.sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const int points = static_cast<int>(plot_.w);
        // The target line is shown faintly behind the actual response, so the IIR
        // approximation and its clamped ends are visible at a glance.
        beginPath();
        strokeColor(kTiltAccent.r, kTiltAccent.g, kTiltAccent.b, 105);
        strokeWidth(1.4f);
        for (int k = 0; k <= points; ++k) {
            const float n = static_cast<float>(k) / static_cast<float>(points);
            const double hz = kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
            const float x = plot_.x + n * plot_.w, y = yOf(static_cast<float>(core::idealDb(p, hz)));
            if (k == 0) moveTo(x, y); else lineTo(x, y);
        }
        stroke();

        beginPath();
        strokeColor(kResponseAccent.r, kResponseAccent.g, kResponseAccent.b, 255);
        strokeWidth(2.8f);
        for (int k = 0; k <= points; ++k) {
            const float n = static_cast<float>(k) / static_cast<float>(points);
            const double hz = kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
            const float x = plot_.x + n * plot_.w;
            const float y = yOf(static_cast<float>(core::responseDb(engine_, p, hz)));
            if (k == 0) moveTo(x, y); else lineTo(x, y);
        }
        stroke();

        const float px = xOf(p.pivot), py = yOf(0.0f);
        beginPath();
        strokeColor(kTiltAccent.r, kTiltAccent.g, kTiltAccent.b, 190);
        strokeWidth(1.5f);
        moveTo(px, plot_.y);
        lineTo(px, plot_.y + plot_.h);
        stroke();
        beginPath();
        circle(px, py, 6.0f);
        fillColor(kTiltAccent.r, kTiltAccent.g, kTiltAccent.b, 255);
        fill();
        label(px + 10.0f, plot_.y + 6.0f, "PIVOT", false, 10.0f);
    }

    void drawSpectrum()
    {
        const auto& t = theme();
        const float floor = core::kSpectrumFloorDb;
        for (std::uint32_t k = 0; k < core::kSpectrumBins; ++k) {
            const float db = spectrum_[k];
            const float n = std::clamp((db - floor) / -floor, 0.0f, 1.0f);
            if (n <= 0.0f)
                continue;
            const float x0 = xOf(static_cast<float>(core::spectrumBandEdgeHz(static_cast<int>(k))));
            const float x1 = xOf(static_cast<float>(core::spectrumBandEdgeHz(static_cast<int>(k) + 1)));
            const float h = n * plot_.h * 0.86f;
            beginPath();
            rect(x0 + 0.5f, plot_.y + plot_.h - h, std::max(1.0f, x1 - x0 - 1.0f), h);
            fillColor(kResponseAccent.r, kResponseAccent.g, kResponseAccent.b, t.background.a > 0 ? 70 : 55);
            fill();
        }
    }

    [[nodiscard]] std::string format(const core::ParamId id) const
    {
        char buf[64];
        switch (id) {
        case core::ParamId::pivot:
            std::snprintf(buf, sizeof(buf), "%.0f Hz", value(pid(id)));
            break;
        case core::ParamId::angle: {
            const core::Parameters p = currentParameters();
            std::snprintf(buf, sizeof(buf), "%+.2f  (%+.1f dB/oct)", value(pid(id)),
                          value(pid(id)) * static_cast<float>(core::slopeDb(p)));
            break;
        }
        case core::ParamId::outputDb:
            std::snprintf(buf, sizeof(buf), "%+.1f dB", value(pid(id)));
            break;
        default:
            std::snprintf(buf, sizeof(buf), "%.0f", value(pid(id)));
            break;
        }
        return buf;
    }

    void drawControlPanel(const Rect b)
    {
        drawPanel(b, "TILT", kTiltAccent, "one linked slope for both channels");
        drawSlider(pid(core::ParamId::pivot), "Pivot Freq", format(core::ParamId::pivot).c_str(),
                   {b.x + 16.0f, b.y + 42.0f, 220.0f, 34.0f}, kTiltAccent);
        drawSlider(pid(core::ParamId::angle), "Angle", format(core::ParamId::angle).c_str(),
                   {b.x + 252.0f, b.y + 42.0f, 260.0f, 34.0f}, kResponseAccent);

        label(b.x + 542.0f, b.y + 42.0f, "Slope", false, 12.0f);
        drawSegments(pid(core::ParamId::slope), core::kSlopeNames.data(), 3,
                     {b.x + 542.0f, b.y + 60.0f, 130.0f, 24.0f}, kResponseAccent);
        label(b.x + 694.0f, b.y + 42.0f, "Mode", false, 12.0f);
        drawSegments(pid(core::ParamId::mode), core::kModeNames.data(), 2,
                     {b.x + 694.0f, b.y + 60.0f, 160.0f, 24.0f}, kTiltAccent);

        label(b.x + 16.0f, b.y + 90.0f, "Spectrum analyser", false, 12.0f);
        drawSegments(pid(core::ParamId::spectrumSource), core::kSpectrumNames.data(), 3,
                     {b.x + 152.0f, b.y + 84.0f, 180.0f, 24.0f}, kTiltAccent);
        char latency[32];
        std::snprintf(latency, sizeof(latency), "Latency %.1f ms", value(core::kOutLatency));
        label(b.x + 380.0f, b.y + 90.0f, latency, true, 11.0f);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutputAccent, "level, bypass and peak meters");
        drawSlider(pid(core::ParamId::outputDb), "Output", format(core::ParamId::outputDb).c_str(),
                   {b.x + 16.0f, b.y + 42.0f, 220.0f, 34.0f}, kOutputAccent);
        label(b.x + 268.0f, b.y + 42.0f, "Bypass", false, 12.0f);
        drawSegments(pid(core::ParamId::bypass), core::kOffOnNames.data(), 2,
                     {b.x + 268.0f, b.y + 60.0f, 130.0f, 24.0f}, kOutputAccent);
        drawMeters({b.x + 440.0f, b.y + 40.0f, b.w - 456.0f, 76.0f});
    }

    void drawMeters(const Rect b)
    {
        const auto& t = theme();
        static constexpr const char* kNames[2] = {"L", "R"};
        for (int c = 0; c < 2; ++c) {
            const float db = std::clamp(value(pid(core::ParamId::outPeakL) + static_cast<std::uint32_t>(c)),
                                        core::kMeterFloorDb, 0.0f);
            const float n = (db - core::kMeterFloorDb) / -core::kMeterFloorDb;
            label(b.x, b.y + static_cast<float>(c) * 38.0f, kNames[c], false, 11.0f);
            beginPath();
            fc(t.controlTrack);
            roundedRect(b.x + 16.0f, b.y + static_cast<float>(c) * 38.0f + 2.0f, b.w - 16.0f, 12.0f, 3.0f);
            fill();
            if (n > 0.002f) {
                beginPath();
                fillColor(kOutputAccent.r, kOutputAccent.g, kOutputAccent.b, 255);
                roundedRect(b.x + 16.0f, b.y + static_cast<float>(c) * 38.0f + 2.0f,
                            (b.w - 16.0f) * n, 12.0f, 3.0f);
                fill();
            }
            char text[24];
            std::snprintf(text, sizeof(text), "%+.1f dB", db);
            label(b.x + b.w, b.y + static_cast<float>(c) * 38.0f - 2.0f, text, true, 10.0f,
                  ALIGN_RIGHT | ALIGN_TOP);
        }
    }

    std::array<float, core::kSpectrumBins> spectrum_ {};
    core::Engine engine_ {};
    Rect plot_ {70.0f, 160.0f, 806.0f, 214.0f};
    bool dragging_ = false, draggingPivot_ = false, demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TiltUI)
};

UI* createUI()
{
    return new TiltUI();
}

END_NAMESPACE_DISTRHO
