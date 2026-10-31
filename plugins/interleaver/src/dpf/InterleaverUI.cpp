#include "downspout/dpf/MagnetoKit.hpp"

#include "interleaver_engine.hpp"
#include "interleaver_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::interleaver;

namespace {

constexpr kit::Accent kSourceAccent {96, 120, 160};   // slate
constexpr kit::Accent kPatternAccent {112, 98, 150};  // dusk violet
constexpr kit::Accent kTimeAccent {198, 132, 58};     // amber
constexpr kit::Accent kSpectralAccent {70, 142, 158}; // steel teal
constexpr kit::Accent kAColour {214, 150, 62};        // source A
constexpr kit::Accent kBColour {64, 160, 176};        // source B

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

class InterleaverUI : public MagnetoUI
{
public:
    InterleaverUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("INTERLEAVER", "two stereo inputs in, one out",
                   "Input A (1/2) and input B (3/4) are woven together by slicing in time or by trading spectral bins. "
                   "Latency: 2048 samples.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawSourcePanel({24.0f, 108.0f, full, 84.0f});
        drawPatternPanel({24.0f, 204.0f, full, 170.0f});
        drawTimePanel({24.0f, 386.0f, full, 150.0f});
        drawSpectralPanel({24.0f, 548.0f, full, 130.0f});
    }

private:
    [[nodiscard]] bool spectral() const { return intValue(core::kMode) == core::kModeSpectral; }

    void drawSourcePanel(const Rect b)
    {
        drawPanel(b, "MIX", kSourceAccent, "A is amber, B is teal");
        static const char* const kModeNames[] = {"Time", "Spectral"};
        label(b.x + 14.0f, b.y + 42.0f, "Mode");
        drawSegments(core::kMode, kModeNames, 2, {b.x + 14.0f, b.y + 58.0f, 200.0f, 22.0f}, kSourceAccent);
        const float w = (b.w - 28.0f - 230.0f - 3.0f * 24.0f) / 3.0f;
        float x = b.x + 14.0f + 230.0f;
        drawSlider(core::kBalance, "Balance (A share)", format(core::kBalance).c_str(), {x, b.y + 44.0f, w, 34.0f}, kSourceAccent);
        x += w + 24.0f;
        drawSlider(core::kSplit, "Stereo split", format(core::kSplit).c_str(), {x, b.y + 44.0f, w, 34.0f}, kSourceAccent);
        x += w + 24.0f;
        drawSlider(core::kOutput, "Output", format(core::kOutput).c_str(), {x, b.y + 44.0f, w, 34.0f}, kSourceAccent);
    }

    void drawPatternPanel(const Rect b)
    {
        drawPanel(b, "PATTERN", kPatternAccent,
                  spectral() ? "source of each of bins 0-127 (left channel above, right below)"
                             : "which source is heard across three cycles (left channel above, right below)");
        const Rect plot {b.x + 40.0f, b.y + 44.0f, b.w - 56.0f, b.h - 58.0f};
        drawPlotFrame(plot);
        const float rowHeight = plot.h * 0.5f;
        const auto& t = theme();
        label(b.x + 16.0f, plot.y + rowHeight * 0.5f - 8.0f, "L", false, 14.0f);
        label(b.x + 16.0f, plot.y + rowHeight * 1.5f - 8.0f, "R", false, 14.0f);
        for (int c = 0; c < 2; ++c) {
            const float mid = plot.y + rowHeight * (static_cast<float>(c) + 0.5f);
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(plot.x, mid);
            lineTo(plot.x + plot.w, mid);
            stroke();
        }
        if (spectral())
            drawSpectralPattern(plot, rowHeight);
        else
            drawTimePattern(plot, rowHeight);
    }

    void drawTimePattern(const Rect plot, const float rowHeight)
    {
        const float balance = value(core::kBalance) * 0.01f;
        const float split = value(core::kSplit) * 0.01f;
        const auto shape = static_cast<core::Shape>(std::clamp(intValue(core::kShape), 0, core::kShapeCount - 1));
        const float width = core::edgeWidth(balance, value(core::kCrossfade), shape);
        constexpr float kCycles = 3.0f;
        const int points = std::max(2, static_cast<int>(plot.w / 1.5f));
        const auto& t = theme();

        for (int cycle = 1; cycle < static_cast<int>(kCycles); ++cycle) {
            const float x = plot.x + plot.w * static_cast<float>(cycle) / kCycles;
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(x, plot.y + 2.0f);
            lineTo(x, plot.y + plot.h - 2.0f);
            stroke();
        }
        for (int c = 0; c < 2; ++c) {
            const float mid = plot.y + rowHeight * (static_cast<float>(c) + 0.5f);
            const float amplitude = rowHeight * 0.40f;
            const float offset = c == 0 ? 0.0f : split * 0.5f;
            for (int source = 0; source < 2; ++source) {
                beginPath();
                moveTo(plot.x, mid);
                for (int i = 0; i <= points; ++i) {
                    const float u = static_cast<float>(i) / static_cast<float>(points);
                    float phase = u * kCycles + offset;
                    phase -= std::floor(phase);
                    const float m = core::timeMask(phase, balance, width, shape);
                    const float level = source == 0 ? std::cos(m * 1.5707963f) : std::sin(m * 1.5707963f);
                    const float y = source == 0 ? mid - amplitude * level : mid + amplitude * level;
                    lineTo(plot.x + plot.w * u, y);
                }
                lineTo(plot.x + plot.w, mid);
                closePath();
                const kit::Accent& colour = source == 0 ? kAColour : kBColour;
                fillColor(colour.r, colour.g, colour.b, 215);
                fill();
            }
        }
    }

    void drawSpectralPattern(const Rect plot, const float rowHeight)
    {
        const int scheme = std::clamp(intValue(core::kScheme), 0, core::kSchemeCount - 1);
        if (scheme == core::kSchemeLoudest) {
            label(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f - 8.0f,
                  "every bin goes to whichever source is louder there", false, 14.0f, ALIGN_CENTER | ALIGN_TOP);
            label(plot.x + plot.w * 0.5f, plot.y + plot.h * 0.5f + 12.0f,
                  "Balance tilts the contest by up to 24 dB", true, 12.0f, ALIGN_CENTER | ALIGN_TOP);
            return;
        }
        const int bins = 128;
        const int binWidth = intValue(core::kBinWidth);
        const float balance = value(core::kBalance) * 0.01f;
        const float split = value(core::kSplit) * 0.01f;
        const float cell = plot.w / static_cast<float>(bins);
        for (int c = 0; c < 2; ++c) {
            const float mid = plot.y + rowHeight * (static_cast<float>(c) + 0.5f);
            const float amplitude = rowHeight * 0.40f;
            const float offset = c == 0 ? 0.0f : split * 0.5f * 2.0f * static_cast<float>(binWidth);
            for (int k = 0; k < bins; ++k) {
                const bool useB = core::spectralPicksB(k, offset, binWidth, balance);
                const float x = plot.x + cell * static_cast<float>(k);
                const float inset = cell > 3.0f ? 0.5f : 0.0f;
                // In Bins mode a cell is wholly one source. In Mag/Phase it is that source's size, wearing the
                // other source's phase, drawn as a pale bar on the far side.
                const kit::Accent& own = useB ? kBColour : kAColour;
                const kit::Accent& other = useB ? kAColour : kBColour;
                beginPath();
                if (useB)
                    rect(x + inset, mid, cell - 2.0f * inset, amplitude);
                else
                    rect(x + inset, mid - amplitude, cell - 2.0f * inset, amplitude);
                fillColor(own.r, own.g, own.b, 215);
                fill();
                if (scheme == core::kSchemeMagPhase) {
                    beginPath();
                    if (useB)
                        rect(x + inset, mid - amplitude, cell - 2.0f * inset, amplitude);
                    else
                        rect(x + inset, mid, cell - 2.0f * inset, amplitude);
                    fillColor(other.r, other.g, other.b, 70);
                    fill();
                }
            }
        }
    }

    void drawTimePanel(const Rect b)
    {
        const bool inactive = spectral();
        const int sync = intValue(core::kSync);
        const float hz = value(core::kDetectedHz);
        char status[96];
        if (sync != core::kSyncFree && hz > 0.0f)
            std::snprintf(status, sizeof(status), "tracking %.1f Hz, cycle %.2f ms", static_cast<double>(hz), static_cast<double>(value(core::kCycleMs)));
        else if (sync != core::kSyncFree)
            std::snprintf(status, sizeof(status), "no pitch found, using Slice");
        else
            std::snprintf(status, sizeof(status), "cycle %.2f ms = A segment + B segment", static_cast<double>(2.0f * value(core::kSlice)));
        drawPanel(b, "TIME", kTimeAccent, status);

        static const char* const kSyncNames[] = {"Free", "Track A", "Track B"};
        static const char* const kShapeNames[] = {"Hard", "Linear", "Cosine"};
        const float w = (b.w - 28.0f - 2.0f * 24.0f) / 3.0f;
        const float x0 = b.x + 14.0f, x1 = x0 + w + 24.0f, x2 = x1 + w + 24.0f;
        const float y1 = b.y + 44.0f, y2 = b.y + 98.0f;

        setDim(inactive);
        label(x0, y1 - 2.0f, "Cycle length from");
        drawSegments(core::kSync, kSyncNames, 3, {x0, y1 + 14.0f, w, 22.0f}, kTimeAccent);
        setDim(inactive || (sync != core::kSyncFree && hz > 0.0f));
        drawSlider(core::kSlice, "Slice (free)", format(core::kSlice).c_str(), {x1, y1, w, 34.0f}, kTimeAccent);
        setDim(inactive || sync == core::kSyncFree);
        drawSlider(core::kPeriods, "Periods per slice", format(core::kPeriods).c_str(), {x2, y1, w, 34.0f}, kTimeAccent);

        setDim(inactive);
        label(x0, y2 - 2.0f, "Edge shape");
        drawSegments(core::kShape, kShapeNames, 3, {x0, y2 + 14.0f, w, 22.0f}, kTimeAccent);
        setDim(inactive || intValue(core::kShape) == core::kShapeHard);
        drawSlider(core::kCrossfade, "Crossfade", format(core::kCrossfade).c_str(), {x1, y2, w, 34.0f}, kTimeAccent);
        setDim(inactive);
        drawSlider(core::kJitter, "Jitter", format(core::kJitter).c_str(), {x2, y2, w, 34.0f}, kTimeAccent);
        setDim(false);
    }

    void drawSpectralPanel(const Rect b)
    {
        const bool inactive = !spectral();
        const int scheme = std::clamp(intValue(core::kScheme), 0, core::kSchemeCount - 1);
        drawPanel(b, "SPECTRAL", kSpectralAccent, "2048-point phase vocoder, 4x overlap");
        setDim(inactive);

        static const char* const kSchemeNames[] = {"Bins", "Mag/Phase", "Loudest"};
        static const char* const kSchemeNotes[] = {
            "Bins: complete bins (magnitude and phase) alternate between A and B in groups of Bin Width.",
            "Mag/Phase: each group takes its size from one source and its phase from the other; the roles swap every group.",
            "Loudest: per bin, whichever source is louder wins. Bin Width and Drift are not used.",
        };
        const float w = (b.w - 28.0f - 2.0f * 24.0f) / 3.0f;
        const float x0 = b.x + 14.0f, x1 = x0 + w + 24.0f, x2 = x1 + w + 24.0f;
        const float y1 = b.y + 44.0f;
        label(x0, y1 - 2.0f, "Scheme");
        drawSegments(core::kScheme, kSchemeNames, 3, {x0, y1 + 14.0f, w, 22.0f}, kSpectralAccent);
        setDim(inactive || scheme == core::kSchemeLoudest);
        drawSlider(core::kBinWidth, "Bin width", format(core::kBinWidth).c_str(), {x1, y1, w, 34.0f}, kSpectralAccent);
        drawSlider(core::kDrift, "Drift (mask rotation)", format(core::kDrift).c_str(), {x2, y1, w, 34.0f}, kSpectralAccent);
        setDim(inactive);
        label(x0, b.y + 100.0f, kSchemeNotes[scheme], true, 12.0f);
        setDim(false);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[40];
        const float v = value(parameter);
        switch (parameter) {
        case core::kBalance: {
            const int a = static_cast<int>(std::lround(v));
            std::snprintf(buffer, sizeof(buffer), "A %d / B %d", a, 100 - a);
            break;
        }
        case core::kSlice:
            std::snprintf(buffer, sizeof(buffer), v < 1.0f ? "%.2f ms" : v < 20.0f ? "%.1f ms" : "%.0f ms", static_cast<double>(v));
            break;
        case core::kPeriods:
            std::snprintf(buffer, sizeof(buffer), "%.2f x", static_cast<double>(v));
            break;
        case core::kOutput:
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
            break;
        case core::kBinWidth:
            std::snprintf(buffer, sizeof(buffer), "%d bins", intValue(parameter));
            break;
        case core::kDrift:
            std::snprintf(buffer, sizeof(buffer), "%+.0f bins/s", static_cast<double>(v));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", static_cast<double>(v));
            break;
        }
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InterleaverUI)
};

UI* createUI()
{
    return new InterleaverUI();
}

END_NAMESPACE_DISTRHO
