#include "downspout/dpf/MagnetoKit.hpp"

#include "glue_core.hpp"
#include "glue_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::glue;

namespace {

constexpr kit::Accent kCurveAccent {198, 132, 58};   // amber
constexpr kit::Accent kMeterAccent {176, 90, 70};    // brick
constexpr kit::Accent kCompAccent {198, 120, 64};    // copper
constexpr kit::Accent kTimingAccent {92, 140, 156};  // cold steel
constexpr kit::Accent kOutputAccent {120, 132, 96};  // sage

constexpr float kPlotMinDb = -60.0f, kPlotMaxDb = 0.0f;
constexpr float kMeterMaxDb = 24.0f;  // top of the reduction history
constexpr std::size_t kHistory = 300;

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const auto& s = core::kParameterSpecs[i];
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

class GlueUI : public MagnetoUI
{
public:
    GlueUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots has no audio; let it show a plausible picture.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            for (std::size_t i = 0; i < kHistory; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(kHistory);
                const float beat = std::fmod(t * 7.0f, 1.0f);
                history_[i] = 2.0f + 9.0f * std::exp(-beat * 5.0f) + 1.5f * std::sin(t * 31.0f);
            }
            // Pick the level whose static reduction equals the last history sample, so the dot sits
            // on the curve (it lags the curve only during transients).
            const float shown = history_[kHistory - 1];
            const core::Parameters defaults;
            float lo = -60.0f, hi = 0.0f;
            for (int i = 0; i < 40; ++i) {
                const float mid = 0.5f * (lo + hi);
                if (-core::staticGainDb(defaults, mid) < shown) lo = mid; else hi = mid;
            }
            setValue(core::kStatusReduction, shown);
            setValue(core::kStatusLevel, 0.5f * (lo + hi));
        }
    }

protected:
    void onAction(const int /*id*/) override {}

    // In demo mode the status values are fixed so a screenshot is not overwritten by the
    // standalone's own (silent) meters.
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_ && (index == core::kStatusReduction || index == core::kStatusLevel))
            return;
        MagnetoUI::parameterChanged(index, v);
    }

    // Samples the live reduction at the idle rate to build the scrolling history.
    void uiIdle() override
    {
        if (!demo_) {
            history_[head_] = value(core::kStatusReduction);
            head_ = (head_ + 1) % kHistory;
        }
        repaint();
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("GLUE", "bus compressor",
                   "Stereo-linked: the louder channel sets the reduction. The curve is the static response; "
                   "the dot is where the signal is now.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float curveW = 380.0f;
        drawCurvePanel({24.0f, 108.0f, curveW, 250.0f});
        drawHistoryPanel({24.0f + curveW + 12.0f, 108.0f, full - curveW - 12.0f, 250.0f});
        drawCompressionPanel({24.0f, 370.0f, full, 100.0f});
        const float timingW = full * 0.62f;
        drawTimingPanel({24.0f, 482.0f, timingW, 212.0f});
        drawOutputPanel({24.0f + timingW + 12.0f, 482.0f, full - timingW - 12.0f, 212.0f});
    }

private:
    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            core::field(p, i) = value(i);
        return p;
    }

    void drawCurvePanel(const Rect b)
    {
        drawPanel(b, "TRANSFER CURVE", kCurveAccent, "output vs input level, dB, before makeup");
        const auto& t = theme();
        const Rect plot {b.x + 46.0f, b.y + 44.0f, b.w - 46.0f - 18.0f, b.h - 44.0f - 30.0f};
        drawPlotFrame(plot);

        const auto xOf = [&](const float db) {
            return plot.x + (std::clamp(db, kPlotMinDb, kPlotMaxDb) - kPlotMinDb) / (kPlotMaxDb - kPlotMinDb) * plot.w;
        };
        const auto yOf = [&](const float db) {
            return plot.y + (kPlotMaxDb - std::clamp(db, kPlotMinDb, kPlotMaxDb)) / (kPlotMaxDb - kPlotMinDb) * plot.h;
        };

        for (int db = -60; db <= 0; db += 12) {
            beginPath();
            sc(t.border);
            strokeWidth(0.8f);
            moveTo(xOf(static_cast<float>(db)), plot.y);
            lineTo(xOf(static_cast<float>(db)), plot.y + plot.h);
            moveTo(plot.x, yOf(static_cast<float>(db)));
            lineTo(plot.x + plot.w, yOf(static_cast<float>(db)));
            stroke();
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", db);
            label(xOf(static_cast<float>(db)), plot.y + plot.h + 5.0f, buf, true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
            label(plot.x - 6.0f, yOf(static_cast<float>(db)), buf, true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        }
        label(plot.x + plot.w, plot.y + plot.h + 17.0f, "input dB", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);

        const core::Parameters p = currentParameters();

        // Unity diagonal.
        beginPath();
        sc(t.border);
        strokeWidth(1.2f);
        moveTo(xOf(kPlotMinDb), yOf(kPlotMinDb));
        lineTo(xOf(kPlotMaxDb), yOf(kPlotMaxDb));
        stroke();

        // Threshold marker.
        beginPath();
        strokeColor(kCurveAccent.r, kCurveAccent.g, kCurveAccent.b, 100);
        strokeWidth(1.2f);
        moveTo(xOf(p.thresholdDb), plot.y);
        lineTo(xOf(p.thresholdDb), plot.y + plot.h);
        stroke();

        // The static curve, from the same function the tests hold the audio to.
        beginPath();
        strokeColor(kCurveAccent.r, kCurveAccent.g, kCurveAccent.b, 255);
        strokeWidth(2.4f);
        const int steps = static_cast<int>(plot.w);
        for (int i = 0; i <= steps; ++i) {
            const float in = kPlotMinDb + (kPlotMaxDb - kPlotMinDb) * static_cast<float>(i) / static_cast<float>(steps);
            const float out = in + core::staticGainDb(p, in);
            if (i == 0) moveTo(xOf(in), yOf(out)); else lineTo(xOf(in), yOf(out));
        }
        stroke();

        // Live operating point.
        const float level = value(core::kStatusLevel);
        const float reduction = value(core::kStatusReduction);
        if (level > kPlotMinDb) {
            beginPath();
            fillColor(240, 236, 224, 255);
            circle(xOf(level), yOf(level - reduction), 5.0f);
            fill();
        }
    }

    void drawHistoryPanel(const Rect b)
    {
        drawPanel(b, "GAIN REDUCTION", kMeterAccent, "dB, last few seconds");
        const auto& t = theme();
        const float reduction = value(core::kStatusReduction);
        char big[24];
        std::snprintf(big, sizeof(big), "%.1f dB", static_cast<double>(reduction));
        label(b.x + 14.0f, b.y + 42.0f, big, false, 22.0f);

        const Rect plot {b.x + 14.0f, b.y + 78.0f, b.w - 28.0f, b.h - 78.0f - 16.0f};
        drawPlotFrame(plot);
        // Reduction hangs down from the top: 0 dB at the top, kMeterMaxDb at the bottom.
        for (int db = 6; db < static_cast<int>(kMeterMaxDb); db += 6) {
            const float y = plot.y + static_cast<float>(db) / kMeterMaxDb * plot.h;
            beginPath();
            sc(t.border);
            strokeWidth(0.8f);
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            stroke();
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", db);
            label(plot.x + 4.0f, y - 11.0f, buf, true, 10.0f);
        }
        beginPath();
        fillColor(kMeterAccent.r, kMeterAccent.g, kMeterAccent.b, 200);
        moveTo(plot.x, plot.y);
        for (std::size_t i = 0; i < kHistory; ++i) {
            const float v = std::clamp(history_[(head_ + i) % kHistory], 0.0f, kMeterMaxDb);
            lineTo(plot.x + static_cast<float>(i) / static_cast<float>(kHistory - 1) * plot.w,
                   plot.y + v / kMeterMaxDb * plot.h);
        }
        lineTo(plot.x + plot.w, plot.y);
        closePath();
        fill();
    }

    void drawCompressionPanel(const Rect b)
    {
        drawPanel(b, "COMPRESSION", kCompAccent);
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 3.0f * gap) / 4.0f;
        const float y = b.y + 48.0f;
        float x = b.x + 14.0f;
        slider(core::kThreshold, "Threshold", {x, y, w, 34.0f}, kCompAccent);
        x += w + gap;
        slider(core::kRatio, "Ratio", {x, y, w, 34.0f}, kCompAccent);
        x += w + gap;
        slider(core::kKnee, "Knee", {x, y, w, 34.0f}, kCompAccent);
        x += w + gap;
        slider(core::kRange, "Range (max cut)", {x, y, w, 34.0f}, kCompAccent);
    }

    void drawTimingPanel(const Rect b)
    {
        drawPanel(b, "TIMING AND DETECTION", kTimingAccent);
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        float x = b.x + 14.0f;
        float y = b.y + 44.0f;
        slider(core::kAttack, "Attack", {x, y, w, 34.0f}, kTimingAccent);
        const bool autoOn = intValue(core::kAutoRelease) == 1;
        setDim(autoOn);
        slider(core::kRelease, autoOn ? "Release (Auto is on)" : "Release", {x + w + gap, y, w, 34.0f}, kTimingAccent);
        setDim(false);
        static constexpr const char* kAutoNames[2] = {"Fixed", "Auto"};
        label(x + 2.0f * (w + gap), y, "Release mode");
        drawSegments(core::kAutoRelease, kAutoNames, 2, {x + 2.0f * (w + gap), y + 18.0f, w, 26.0f}, kTimingAccent);

        y += 54.0f;
        slider(core::kScHpf, "Sidechain HPF", {x, y, w, 34.0f}, kTimingAccent);
        static constexpr const char* kDetectorNames[2] = {"Peak", "RMS"};
        label(x + w + gap, y, "Detector");
        drawSegments(core::kDetector, kDetectorNames, 2, {x + w + gap, y + 18.0f, w, 26.0f}, kTimingAccent);
        static constexpr const char* kSourceNames[2] = {"Input", "Sidechain"};
        label(x + 2.0f * (w + gap), y, "Detector listens to");
        drawSegments(core::kSidechain, kSourceNames, 2, {x + 2.0f * (w + gap), y + 18.0f, w, 26.0f}, kTimingAccent);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kOutputAccent, "CC 1-4: Threshold, Ratio, Makeup, Mix");
        const float gap = 24.0f;
        const float w = (b.w - 28.0f - gap) / 2.0f;
        float x = b.x + 14.0f;
        float y = b.y + 44.0f;
        slider(core::kMakeup, "Makeup", {x, y, w, 34.0f}, kOutputAccent);
        slider(core::kMix, "Mix (parallel)", {x + w + gap, y, w, 34.0f}, kOutputAccent);
        y += 54.0f;
        static constexpr const char* kClipNames[2] = {"Off", "On"};
        label(x, y, "Soft clip");
        drawSegments(core::kSoftClip, kClipNames, 2, {x, y + 18.0f, w, 26.0f}, kOutputAccent);
        slider(core::kCcChannel, "CC channel", {x + w + gap, y, w, 34.0f}, kOutputAccent);
        y += 54.0f;
        slider(core::kLookahead, "Look-ahead", {x, y, w, 34.0f}, kOutputAccent);
    }

    void slider(const std::uint32_t parameter, const char* name, const Rect r, const Accent accent)
    {
        drawSlider(parameter, name, format(parameter).c_str(), r, accent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
        switch (parameter) {
        case core::kThreshold:
        case core::kKnee:
        case core::kRange:
            std::snprintf(buffer, sizeof(buffer), "%.1f dB", static_cast<double>(v));
            break;
        case core::kMakeup:
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        case core::kRatio:
            std::snprintf(buffer, sizeof(buffer), "%.1f : 1", static_cast<double>(v));
            break;
        case core::kAttack:
            std::snprintf(buffer, sizeof(buffer), v < 10.0f ? "%.2f ms" : "%.0f ms", static_cast<double>(v));
            break;
        case core::kRelease:
            std::snprintf(buffer, sizeof(buffer), "%.0f ms", static_cast<double>(v));
            break;
        case core::kLookahead:
            if (v < 0.05f)
                return "off";
            std::snprintf(buffer, sizeof(buffer), "%.1f ms", static_cast<double>(v));
            break;
        case core::kCcChannel:
            if (v < 0.5f)
                return "off";
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            break;
        case core::kScHpf:
            if (v <= core::kHpfOffHz + 0.5f)
                return "off";
            std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
            break;
        }
        return buffer;
    }

    std::array<float, kHistory> history_ {};
    std::size_t head_ = 0;
    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GlueUI)
};

UI* createUI()
{
    return new GlueUI();
}

END_NAMESPACE_DISTRHO
