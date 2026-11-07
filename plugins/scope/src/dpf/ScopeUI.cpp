#include "downspout/dpf/MagnetoKit.hpp"

#include "scope_engine.hpp"
#include "scope_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>

START_NAMESPACE_DISTRHO

namespace core = downspout::scope;

namespace {

struct Tables {
    std::array<kit::Range, core::kParamCount> ranges {};
    std::array<float, core::kParamCount> defaults {};
    Tables()
    {
        const auto& table = core::parameterTable();
        for (int i = 0; i < core::kParamCount; ++i) {
            const auto& s = table.specs[static_cast<std::size_t>(i)];
            ranges[static_cast<std::size_t>(i)] = {s.minimum, s.maximum, (bool)s.integer};
            defaults[static_cast<std::size_t>(i)] = s.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

constexpr kit::Accent kAccent {100, 150, 200};
constexpr kit::Accent kMeterAccent {120, 190, 120};
constexpr float kLow = 20.0f;
constexpr float kHigh = 20000.0f;

const char* const kRangeNames[] = {"60", "90", "120"};
const char* const kHoldNames[] = {"OFF", "ON"};
const char* const kModeNames[] = {"SPECTRUM", "LEVELS"};

constexpr float kLabelFreqs[] = {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f};
const char* const kLabelTexts[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k"};

float floorDb(const float rangeIndex)
{
    const int i = std::clamp(static_cast<int>(std::lround(rangeIndex)), 0, 2);
    return -core::kRangeDb[i];
}

}  // namespace

class ScopeUI : public MagnetoUI {
public:
    ScopeUI()
        : MagnetoUI(tables().ranges.data(), core::kControlCount, tables().defaults.data())
    {
        bands_.fill(-120.0f);
        holds_.fill(-120.0f);
        demo_ = std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr;
        if (demo_) {
            setValue(core::kRange, 0.0f);
            setValue(core::kSpeed, 0.5f);
            setValue(core::kHold, 1.0f);
            setValue(core::kMode, 0.0f);
            // A plausible programme spectrum so catalog screenshots show the analyser.
            const float humps[][3] = {{80.0f, 9.0f, 0.20f}, {240.0f, 5.0f, 0.35f},
                                      {900.0f, 3.0f, 0.30f}, {3200.0f, 6.0f, 0.25f},
                                      {9000.0f, 3.0f, 0.40f}};
            for (int i = 0; i < core::kSpecCount; ++i) {
                const float hz = core::bandCentreHz(i);
                float db = -16.0f - 5.2f * std::log2(hz / 100.0f);
                for (const auto& h : humps)
                    db += h[1] * std::exp(-std::pow(std::log2(hz / h[0]) / h[2], 2.0f));
                db += 1.8f * std::sin(static_cast<float>(i) * 1.7f) * std::sin(static_cast<float>(i) * 0.42f);
                bands_[static_cast<std::size_t>(i)] = std::clamp(db, -60.0f, 0.0f);
                holds_[static_cast<std::size_t>(i)] = std::clamp(db - 6.0f, -60.0f, 0.0f);
            }
            peakDb_ = {-9.4f, -11.2f};
            rmsDb_ = {-14.8f, -16.1f};
            corr_ = 0.71f;
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_ && index >= static_cast<uint32_t>(core::kPeakL))
            return;   // keep the demo data for the screenshot
        if (index >= static_cast<uint32_t>(core::kSpec0)) {
            bands_[static_cast<std::size_t>(index - core::kSpec0)] = v;
            repaint();
            return;
        }
        if (index >= static_cast<uint32_t>(core::kPeakL) && index < static_cast<uint32_t>(core::kSpec0)) {
            switch (index) {
            case core::kPeakL: peakDb_[0] = v; break;
            case core::kPeakR: peakDb_[1] = v; break;
            case core::kRmsL: rmsDb_[0] = v; break;
            case core::kRmsR: rmsDb_[1] = v; break;
            case core::kCorr: corr_ = v; break;
            default: break;
            }
            repaint();
            return;
        }
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("Scope", "spectrum analyser", "64 log-spaced bands | peak / RMS / correlation");

        drawControls();
        drawAnalysis();
    }

private:
    void drawControls()
    {
        const Rect b {10.0f, 104.0f, static_cast<float>(getWidth()) - 20.0f, 96.0f};
        drawPanel(b, "CONTROLS", kAccent);

        const float y = b.y + 40.0f;
        label(24.0f, y, "Range (dB)");
        drawSegments(core::kRange, kRangeNames, 3, {24.0f, y + 15.0f, 168.0f, 26.0f}, kAccent);

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.2f", value(core::kSpeed));
        drawSlider(core::kSpeed, "Speed", buf, {216.0f, y, 176.0f, 40.0f}, kAccent);

        label(416.0f, y, "Peak hold");
        drawSegments(core::kHold, kHoldNames, 2, {416.0f, y + 15.0f, 104.0f, 26.0f}, kAccent);

        label(544.0f, y, "Display");
        drawSegments(core::kMode, kModeNames, 2, {544.0f, y + 15.0f, 240.0f, 26.0f}, kAccent);
    }

    void drawAnalysis()
    {
        const Rect b {10.0f, 208.0f, static_cast<float>(getWidth()) - 20.0f,
                      static_cast<float>(getHeight()) - 218.0f};
        const bool levels = intValue(core::kMode) == 1;
        drawPanel(b, levels ? "LEVELS" : "SPECTRUM", levels ? kMeterAccent : kAccent,
                  levels ? "peak / RMS per channel" : "20 Hz - 20 kHz");

        plot_ = {b.x + 54.0f, b.y + 42.0f, b.w - 74.0f, b.h - 104.0f};
        drawPlotFrame(plot_);

        if (levels)
            drawLevels();
        else
            drawSpectrum();

        drawReadout(b);
    }

    void drawSpectrum()
    {
        const auto& t = theme();
        const float floor = floorDb(value(core::kRange));
        const float span = -floor;

        // Horizontal dB grid.
        for (int g = 0; g <= 3; ++g) {
            const float db = floor * (1.0f - static_cast<float>(g) / 3.0f);
            const float y = plot_.y + (1.0f - (db - floor) / span) * plot_.h;
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(plot_.x, y);
            lineTo(plot_.x + plot_.w, y);
            stroke();

            char buf[16];
            if (g == 0)
                std::snprintf(buf, sizeof(buf), "%.0f", floor);
            else if (g == 3)
                std::snprintf(buf, sizeof(buf), "0");
            else
                std::snprintf(buf, sizeof(buf), "%.0f", db);
            fc(t.textDim);
            fontSize(10.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(plot_.x - 6.0f, y, buf, nullptr);
        }

        // Vertical frequency grid with labels.
        for (std::size_t i = 0; i < std::size(kLabelFreqs); ++i) {
            const float x = freqX(kLabelFreqs[i]);
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(x, plot_.y);
            lineTo(x, plot_.y + plot_.h);
            stroke();
            fc(t.textDim);
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            text(x, plot_.y + plot_.h + 6.0f, kLabelTexts[i], nullptr);
        }
        fc(t.textDim);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(plot_.x + plot_.w, plot_.y + plot_.h + 6.0f, "20k", nullptr);

        // Bars.
        const float barW = plot_.w / static_cast<float>(core::kSpecCount);
        const bool holdOn = intValue(core::kHold) == 1;
        for (int i = 0; i < core::kSpecCount; ++i) {
            const float db = bands_[static_cast<std::size_t>(i)];
            const float n = std::clamp((db - floor) / span, 0.0f, 1.0f);
            const float h = n * plot_.h;
            if (h > 0.5f) {
                beginPath();
                fillColor(kAccent.r, kAccent.g, kAccent.b, 210);
                rect(plot_.x + static_cast<float>(i) * barW + 0.5f,
                     plot_.y + plot_.h - h, std::max(1.0f, barW - 1.0f), h);
                fill();
            }

            float& held = holds_[static_cast<std::size_t>(i)];
            if (holdOn)
                held = std::max(held - 0.7f, db);
            else
                held = db;
            if (holdOn && held > floor && held > db + 0.5f) {
                const float hn = std::clamp((held - floor) / span, 0.0f, 1.0f);
                beginPath();
                fillColor(250, 248, 242, 220);
                rect(plot_.x + static_cast<float>(i) * barW + 0.5f,
                     plot_.y + plot_.h - hn * plot_.h - 1.0f,
                     std::max(1.0f, barW - 1.0f), 2.0f);
                fill();
            }
        }
    }

    void drawLevels()
    {
        const auto& t = theme();
        const float floor = floorDb(value(core::kRange));
        const float span = -floor;
        const float trackX = plot_.x + 28.0f;
        const float trackW = plot_.w - 28.0f - 170.0f;

        const float peaks[2] = {meterPeak(0), meterPeak(1)};
        const float rms[2] = {meterRms(0), meterRms(1)};
        const bool holdOn = intValue(core::kHold) == 1;

        for (int ch = 0; ch < 2; ++ch) {
            const float y = plot_.y + 22.0f + static_cast<float>(ch) * 56.0f;
            fc(t.textPrimary);
            fontSize(16.0f);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(plot_.x + 4.0f, y + 12.0f, ch == 0 ? "L" : "R", nullptr);

            beginPath();
            fc(t.controlTrack);
            roundedRect(trackX, y, trackW, 24.0f, 3.0f);
            fill();

            const float pn = std::clamp((peaks[ch] - floor) / span, 0.0f, 1.0f);
            if (pn > 0.001f) {
                beginPath();
                fillColor(kMeterAccent.r, kMeterAccent.g, kMeterAccent.b, 220);
                roundedRect(trackX, y, trackW * pn, 24.0f, 3.0f);
                fill();
            }
            const float rn = std::clamp((rms[ch] - floor) / span, 0.0f, 1.0f);
            if (rn > 0.001f) {
                beginPath();
                fillColor(250, 248, 242, 230);
                rect(trackX + trackW * rn - 1.0f, y, 2.0f, 24.0f);
                fill();
            }

            float& held = holdLevel_[static_cast<std::size_t>(ch)];
            if (holdOn)
                held = std::max(held - 0.6f, peaks[ch]);
            else
                held = peaks[ch];
            if (holdOn && held > floor && held > peaks[ch] + 0.5f) {
                const float hn = std::clamp((held - floor) / span, 0.0f, 1.0f);
                beginPath();
                fillColor(250, 248, 242, 220);
                rect(trackX + trackW * hn - 1.0f, y, 2.0f, 24.0f);
                fill();
            }

            char buf[48];
            std::snprintf(buf, sizeof(buf), "%6.1f / %6.1f dB", peaks[ch], rms[ch]);
            fc(t.textPrimary);
            fontSize(12.0f);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(plot_.x + plot_.w, y + 12.0f, buf, nullptr);
        }

        // Correlation: -1 (out of phase) .. +1 (mono).
        const float corr = meterCorr();
        const float cy = plot_.y + 132.0f;
        label(plot_.x, cy - 14.0f, "CORRELATION");
        const float cx = plot_.x + 120.0f;
        const float cw = plot_.w - 120.0f - 60.0f;
        beginPath();
        fc(t.controlTrack);
        roundedRect(cx, cy - 6.0f, cw, 12.0f, 3.0f);
        fill();
        beginPath();
        fillColor(kMeterAccent.r, kMeterAccent.g, kMeterAccent.b, 200);
        const float centre = cx + cw * 0.5f;
        const float half = cw * 0.5f * std::fabs(corr);
        rect(corr >= 0.0f ? centre : centre - half, cy - 6.0f, std::max(1.0f, half), 12.0f);
        fill();
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%+.2f", corr);
        fc(t.textPrimary);
        fontSize(12.0f);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(plot_.x + plot_.w, cy, buf, nullptr);
    }

    void drawReadout(const Rect& b)
    {
        char line[128];
        std::snprintf(line, sizeof(line), "Peak   L %6.1f   R %6.1f dB", meterPeak(0), meterPeak(1));
        fillColor(240, 240, 240, 255);
        fontSize(12.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(b.x + 54.0f, b.y + b.h - 40.0f, line, nullptr);

        std::snprintf(line, sizeof(line), "RMS    L %6.1f   R %6.1f dB      Corr %+.2f",
                      meterRms(0), meterRms(1), meterCorr());
        text(b.x + 54.0f, b.y + b.h - 22.0f, line, nullptr);
    }

    float meterPeak(const int ch) const { return peakDb_[static_cast<std::size_t>(ch)]; }
    float meterRms(const int ch) const { return rmsDb_[static_cast<std::size_t>(ch)]; }
    float meterCorr() const { return corr_; }

    float freqX(const float hz) const
    {
        return plot_.x + std::log(hz / kLow) / std::log(kHigh / kLow) * plot_.w;
    }

    std::array<float, core::kSpecCount> bands_ {};
    std::array<float, core::kSpecCount> holds_ {};
    std::array<float, 2> holdLevel_ {-120.0f, -120.0f};
    Rect plot_ {};
    bool demo_ = false;
    std::array<float, 2> peakDb_ {-120.0f, -120.0f};
    std::array<float, 2> rmsDb_ {-120.0f, -120.0f};
    float corr_ = 0.0f;
};

UI* createUI() { return new ScopeUI(); }

END_NAMESPACE_DISTRHO
