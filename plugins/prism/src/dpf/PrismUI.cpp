#include "downspout/dpf/MagnetoKit.hpp"

#include "prism_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::prism;

namespace {

constexpr kit::Accent kPlotAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kBandAccent {198, 132, 58};   // amber
constexpr kit::Accent kControlAccent {128, 152, 104};  // sage
constexpr kit::Accent kLeftColour {208, 118, 58};
constexpr kit::Accent kRightColour {70, 132, 168};

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        const auto& specs = core::parameterSpecs();
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            ranges[i] = {specs[i].minimum, specs[i].maximum, specs[i].integer, specs[i].logarithmic};
            defaults[i] = specs[i].defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

std::string delayText(const float ms)
{
    char buffer[24];
    if (ms >= 1000.0f)
        std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(ms) / 1000.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(ms)));
    return buffer;
}

std::string hzText(const float hz)
{
    char buffer[24];
    if (hz >= 1000.0f)
        std::snprintf(buffer, sizeof(buffer), "%.1f kHz", static_cast<double>(hz) / 1000.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%d Hz", static_cast<int>(std::lround(hz)));
    return buffer;
}

// Short frequency label for the plot axis and band rows.
std::string axisHz(const float hz)
{
    char buffer[16];
    if (hz >= 1000.0f)
        std::snprintf(buffer, sizeof(buffer), hz >= 9950.0f ? "%.0fk" : "%.1fk", static_cast<double>(hz) / 1000.0);
    else
        std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(hz)));
    return buffer;
}

}  // namespace

class PrismUI : public MagnetoUI {
public:
    PrismUI()
        : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
    }

protected:
    // Buttons 0-7 solo a band.
    void onAction(const int id) override
    {
        if (id >= 0 && id < static_cast<int>(core::kMaxBands)) {
            const auto parameter = core::bandParam(static_cast<std::size_t>(id), core::kSolo);
            commit(parameter, value(parameter) >= 0.5f ? 0.0f : 1.0f);
        }
    }

    // With L/R linked the engine uses the left value for both sides; the panel keeps the right
    // controls in step so what is shown is what is heard.
    void sendValue(const std::uint32_t parameter, const float v) override
    {
        MagnetoUI::sendValue(parameter, v);
        const bool linked = value(core::kLink) >= 0.5f;
        if (parameter == core::kLink && v >= 0.5f) {
            commit(core::kSyncR, value(core::kSyncL));
            for (std::size_t b = 0; b < core::kMaxBands; ++b) {
                commit(core::bandParam(b, core::kDelayR), value(core::bandParam(b, core::kDelayL)));
                commit(core::bandParam(b, core::kDivR), value(core::bandParam(b, core::kDivL)));
            }
        } else if (linked && parameter == core::kSyncL) {
            commit(core::kSyncR, v);
        } else if (linked && parameter >= core::kGlobalCount && parameter < core::kInputParameterCount) {
            const std::uint32_t rel = (parameter - core::kGlobalCount) % core::kFieldsPerBand;
            if (rel == core::kDelayL || rel == core::kDivL) {
                const std::uint32_t right = parameter + 1;  // R follows L in the band layout
                if (std::fabs(value(right) - v) > 0.001f)
                    commit(right, v);
            }
        }
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("Prism", "banded spectral delay",
                   "splits the sound into 1-8 frequency bands; each band is delayed separately on the left and right, with its own feedback.");
        drawPlotPanel({24.0f, 108.0f, 560.0f, 178.0f});
        drawBandsPanel({24.0f, 298.0f, 560.0f, 350.0f});
        drawControlsPanel({600.0f, 108.0f, 296.0f, 540.0f});
    }

private:
    [[nodiscard]] int bandCount() const { return std::clamp(intValue(core::kBands), 1, 8); }

    [[nodiscard]] bool synced(const bool right) const
    {
        return value(right && value(core::kLink) < 0.5f ? core::kSyncR : core::kSyncL) >= 0.5f;
    }

    [[nodiscard]] float tempo() const { return std::max(20.0f, value(core::kOutTempo)); }

    // A band's delay in milliseconds as the engine will use it, tempo sync included.
    [[nodiscard]] float effectiveMs(const std::size_t band, const bool right) const
    {
        const bool useRight = right && value(core::kLink) < 0.5f;
        if (synced(right))
            return std::min(core::divisionMs(intValue(core::bandParam(band, useRight ? core::kDivR : core::kDivL)), tempo()),
                            core::kMaxDelayMs);
        return value(core::bandParam(band, useRight ? core::kDelayR : core::kDelayL));
    }

    // The delay applied to a frequency: band values interpolated in log frequency, scaled by
    // the two cuts. The same calculation the engine makes for each bin.
    [[nodiscard]] float delayAt(const double hz, const bool right) const
    {
        const int bands = bandCount();
        const auto pos = core::bandPosition(hz, bands);
        const int hi = std::min(pos.band + 1, bands - 1);
        const float lo = effectiveMs(static_cast<std::size_t>(pos.band), right);
        const float up = effectiveMs(static_cast<std::size_t>(hi), right);
        const float cut = core::lowCutWeight(hz, value(core::kLowCut)) * core::highCutWeight(hz, value(core::kHighCut));
        return (lo * (1.0f - pos.frac) + up * pos.frac) * cut;
    }

    void drawPlotPanel(const Rect b)
    {
        drawPanel(b, "DELAY BY FREQUENCY", kPlotAccent, "what each part of the spectrum is delayed by");
        const auto& t = theme();
        const Rect plot {b.x + 52.0f, b.y + 44.0f, b.w - 52.0f - 16.0f, b.h - 44.0f - 28.0f};
        drawPlotFrame(plot);

        const int bands = bandCount();
        float largest = 0.0f;
        for (int i = 0; i < bands; ++i) {
            largest = std::max(largest, effectiveMs(static_cast<std::size_t>(i), false));
            largest = std::max(largest, effectiveMs(static_cast<std::size_t>(i), true));
        }
        float top = 100.0f;
        for (const float step : {100.0f, 250.0f, 500.0f, 1000.0f, 2000.0f}) {
            top = step;
            if (largest <= step)
                break;
        }

        const double lowHz = 20.0, highHz = 20000.0;
        const auto xOf = [&](const double hz) {
            return plot.x + static_cast<float>(std::log(hz / lowHz) / std::log(highHz / lowHz)) * plot.w;
        };
        const auto yOf = [&](const float ms) { return plot.y + plot.h - std::min(ms / top, 1.0f) * (plot.h - 6.0f) - 3.0f; };

        // Horizontal guides and their delay labels.
        for (int g = 0; g <= 2; ++g) {
            const float ms = top * static_cast<float>(g) / 2.0f;
            const float y = yOf(ms);
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(plot.x, y);
            lineTo(plot.x + plot.w, y);
            stroke();
            label(plot.x - 6.0f, y, delayText(ms).c_str(), true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        }

        // Cut regions, shaded: nothing there is delayed.
        const float lowCut = value(core::kLowCut);
        const float highCut = value(core::kHighCut);
        if (lowCut > core::kMinLowCutHz) {
            beginPath();
            fillColor(0, 0, 0, 46);
            rect(plot.x, plot.y, std::max(0.0f, xOf(lowCut) - plot.x), plot.h);
            fill();
        }
        if (highCut < core::kMaxHighCutHz) {
            beginPath();
            fillColor(0, 0, 0, 46);
            rect(xOf(highCut), plot.y, std::max(0.0f, plot.x + plot.w - xOf(highCut)), plot.h);
            fill();
        }

        // Band centres: a tick and the frequency at the foot of the plot.
        for (int i = 0; i < bands; ++i) {
            const float hz = core::bandCentreHz(i, bands);
            const float x = bands == 1 ? plot.x + plot.w * 0.5f : xOf(hz);
            const bool soloed = value(core::bandParam(static_cast<std::size_t>(i), core::kSolo)) >= 0.5f;
            beginPath();
            sc(t.border);
            strokeWidth(soloed ? 2.0f : 1.0f);
            moveTo(x, plot.y);
            lineTo(x, plot.y + plot.h);
            stroke();
            label(x, plot.y + plot.h + 5.0f, bands == 1 ? "all" : axisHz(hz).c_str(), !soloed, 10.0f, ALIGN_CENTER | ALIGN_TOP);
        }
        label(plot.x + plot.w, plot.y + plot.h + 17.0f, "Hz", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);

        // The curves: right first so left, usually the one being edited, sits on top.
        for (const bool right : {true, false}) {
            const kit::Accent colour = right ? kRightColour : kLeftColour;
            beginPath();
            strokeColor(colour.r, colour.g, colour.b, 255);
            strokeWidth(2.5f);
            constexpr int kPoints = 200;
            for (int i = 0; i < kPoints; ++i) {
                const double hz = lowHz * std::pow(highHz / lowHz, static_cast<double>(i) / (kPoints - 1));
                const float x = plot.x + plot.w * static_cast<float>(i) / (kPoints - 1);
                const float y = yOf(delayAt(hz, right));
                if (i == 0)
                    moveTo(x, y);
                else
                    lineTo(x, y);
            }
            stroke();
        }

        // Legend, top right inside the plot.
        float lx = plot.x + 12.0f;
        for (const bool right : {false, true}) {
            const kit::Accent colour = right ? kRightColour : kLeftColour;
            beginPath();
            fillColor(colour.r, colour.g, colour.b, 255);
            roundedRect(lx, plot.y + 10.0f, 16.0f, 5.0f, 2.5f);
            fill();
            label(lx + 22.0f, plot.y + 6.0f, right ? "Right" : "Left", false, 11.0f);
            lx += 50.0f;
        }
    }

    void drawBandsPanel(const Rect b)
    {
        char caption[48];
        std::snprintf(caption, sizeof(caption), "%d band%s, centres 31 Hz - 16 kHz", bandCount(), bandCount() == 1 ? "" : "s");
        drawPanel(b, "BANDS", kBandAccent, caption);

        const float x0 = b.x + 14.0f;
        const float sliderX = x0 + 98.0f;
        const float gap = 14.0f;
        const float sliderW = (b.x + b.w - 14.0f - sliderX - 2.0f * gap) / 3.0f;
        const bool linked = value(core::kLink) >= 0.5f;

        label(x0, b.y + 42.0f, "solo   band");
        label(sliderX, b.y + 42.0f, synced(false) ? "Left delay (note)" : "Left delay");
        label(sliderX + sliderW + gap, b.y + 42.0f,
              linked ? "Right delay (follows left)" : synced(true) ? "Right delay (note)" : "Right delay");
        label(sliderX + 2.0f * (sliderW + gap), b.y + 42.0f, "Feedback");

        const int bands = bandCount();
        const float rowTop = b.y + 62.0f;
        const float pitch = 35.0f;
        for (int i = 0; i < static_cast<int>(core::kMaxBands); ++i) {
            const auto band = static_cast<std::size_t>(i);
            const float y = rowTop + static_cast<float>(i) * pitch;
            const bool active = i < bands;
            setDim(!active);

            const bool soloed = value(core::bandParam(band, core::kSolo)) >= 0.5f;
            drawButton(i, "S", {x0, y + 6.0f, 26.0f, 22.0f}, kBandAccent, soloed);

            char name[24];
            if (bands == 1)
                std::snprintf(name, sizeof(name), "all");
            else
                std::snprintf(name, sizeof(name), "%d  %s", i + 1, hzText(core::bandCentreHz(i, bands)).c_str());
            label(x0 + 34.0f, y + 11.0f, name, !active, 12.0f);

            drawDelaySlider(band, false, {sliderX, y, sliderW, 30.0f});
            setDim(!active || linked);
            drawDelaySlider(band, true, {sliderX + sliderW + gap, y, sliderW, 30.0f});
            setDim(!active);
            char fb[16];
            std::snprintf(fb, sizeof(fb), "%d%%", static_cast<int>(std::lround(value(core::bandParam(band, core::kFeedback)))));
            drawSlider(core::bandParam(band, core::kFeedback), "", fb,
                       {sliderX + 2.0f * (sliderW + gap), y, sliderW, 30.0f}, kBandAccent);
        }
        setDim(false);
    }

    // One side's delay for a band: milliseconds, or a note division with its length alongside.
    void drawDelaySlider(const std::size_t band, const bool right, const Rect b)
    {
        const kit::Accent colour = right ? kRightColour : kLeftColour;
        const bool useRight = right && value(core::kLink) < 0.5f;
        if (!synced(right)) {
            const auto parameter = core::bandParam(band, useRight ? core::kDelayR : core::kDelayL);
            drawSlider(parameter, "", delayText(value(parameter)).c_str(), b, colour);
            return;
        }
        const auto parameter = core::bandParam(band, useRight ? core::kDivR : core::kDivL);
        const float natural = core::divisionMs(intValue(parameter), tempo());
        char shown[48];
        if (natural > core::kMaxDelayMs)
            std::snprintf(shown, sizeof(shown), "%s  (limited to 2 s)", core::kDivisionNames[static_cast<std::size_t>(intValue(parameter))]);
        else
            std::snprintf(shown, sizeof(shown), "%s  %d ms", core::kDivisionNames[static_cast<std::size_t>(intValue(parameter))],
                          static_cast<int>(std::lround(natural)));
        drawSlider(parameter, "", shown, b, colour);
    }

    void drawControlsPanel(const Rect b)
    {
        drawPanel(b, "SPECTRUM", kControlAccent, "bands, resolution, cuts");
        const float x = b.x + 14.0f;
        const float w = b.w - 28.0f;
        float y = b.y + 46.0f;

        char count[16];
        std::snprintf(count, sizeof(count), "%d", bandCount());
        drawSlider(core::kBands, "Bands", count, {x, y, w, 34.0f}, kControlAccent);
        y += 50.0f;

        label(x, y, "Resolution (FFT size)");
        drawSegments(core::kSize, core::kFftSizeNames.data(), static_cast<int>(core::kFftSizeCount), {x, y + 17.0f, w, 28.0f},
                     kControlAccent);
        const int sizeIndex = std::clamp(intValue(core::kSize), 0, 3);
        char latency[96];
        std::snprintf(latency, sizeof(latency), "latency %d ms: larger = smoother, slower to change",
                      static_cast<int>(std::lround(1000.0 * core::kFftSizes[static_cast<std::size_t>(sizeIndex)] / std::max(8000.0, getSampleRate()))));
        label(x, y + 50.0f, latency, true, 11.0f);
        y += 80.0f;

        label(x, y, "Link left and right");
        drawSegments(core::kLink, core::kLinkNames.data(), 2, {x, y + 17.0f, w, 28.0f}, kControlAccent);
        y += 64.0f;

        char tempoLine[64];
        std::snprintf(tempoLine, sizeof(tempoLine), "Tempo sync (host tempo %d BPM)", static_cast<int>(std::lround(tempo())));
        label(x, y, tempoLine);
        const float half = (w - 12.0f) * 0.5f;
        drawSegments(core::kSyncL, core::kSyncNames.data(), 2, {x, y + 17.0f, half, 28.0f}, kLeftColour);
        drawSegments(core::kSyncR, core::kSyncNames.data(), 2, {x + half + 12.0f, y + 17.0f, half, 28.0f}, kRightColour);
        label(x, y + 48.0f, "Left", true, 10.0f);
        label(x + half + 12.0f, y + 48.0f, "Right", true, 10.0f);
        y += 76.0f;

        const float lowCut = value(core::kLowCut);
        const float highCut = value(core::kHighCut);
        drawSlider(core::kLowCut, "Low cut", lowCut <= core::kMinLowCutHz ? "off" : hzText(lowCut).c_str(), {x, y, w, 34.0f},
                   kControlAccent);
        y += 44.0f;
        drawSlider(core::kHighCut, "High cut", highCut >= core::kMaxHighCutHz ? "off" : hzText(highCut).c_str(), {x, y, w, 34.0f},
                   kControlAccent);
        y += 44.0f;
        char mix[16];
        std::snprintf(mix, sizeof(mix), "%d%%", static_cast<int>(std::lround(value(core::kMix) * 100.0f)));
        drawSlider(core::kMix, "Mix (dry / delayed)", mix, {x, y, w, 34.0f}, kControlAccent);
        y += 52.0f;

        label(x, y, "Delays blend smoothly between band centres.", true, 11.0f);
        label(x, y + 14.0f, "Cuts leave everything beyond them undelayed.", true, 11.0f);
        label(x, y + 28.0f, "S solos one band, delayed sound only.", true, 11.0f);
    }
};

UI* createUI() { return new PrismUI(); }

END_NAMESPACE_DISTRHO
