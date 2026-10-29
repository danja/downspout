#include "downspout/dpf/MagnetoKit.hpp"

#include "octet_core.hpp"
#include "octet_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::octet;

namespace {

constexpr kit::Accent kPlotAccent {198, 132, 58};    // amber
constexpr kit::Accent kBandAccent {198, 120, 64};    // copper
constexpr kit::Accent kGlobalAccent {92, 140, 156};  // cold steel

constexpr float kPlotMinHz = 20.0f, kPlotMaxHz = 20000.0f;
constexpr float kPlotMaxDb = 24.0f;  // the plot runs +-24 dB; band gain stops at +-18

constexpr std::array<std::array<int, 3>, core::kBands> kBandColours = {{
    {214, 96, 84}, {222, 150, 70}, {214, 190, 80}, {132, 190, 96}, {80, 180, 160}, {84, 150, 210}, {140, 120, 214}, {204, 120, 186}}};

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kSpectrumFirst; ++i) {
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

class OctetUI : public MagnetoUI
{
public:
    // MagnetoUI holds at most 64 values, so it is given the controls only; the 64 spectrum outputs live in `spectrum_`.
    static_assert(core::kSpectrumFirst <= kMaxValues, "the controls must fit MagnetoUI's value table");

    OctetUI() : MagnetoUI(tables().ranges.data(), core::kSpectrumFirst, tables().defaults.data())
    {
        spectrum_.fill(core::kSpectrumFloorDb);
        // The standalone used for screenshots starts at the transparent defaults; show a plausible curve.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            auto set = [&](int band, bool on, core::BandType type, float f, float g, float q) {
                setValue(core::bandParameter(band, core::kOn), on ? 1.0f : 0.0f);
                setValue(core::bandParameter(band, core::kType), static_cast<float>(type));
                setValue(core::bandParameter(band, core::kFreq), f);
                setValue(core::bandParameter(band, core::kGain), g);
                setValue(core::bandParameter(band, core::kQ), q);
            };
            set(0, true, core::BandType::HighPass48, 40.0f, 0.0f, 0.71f);
            set(1, true, core::BandType::LowShelf, 120.0f, 3.0f, 0.71f);
            set(2, true, core::BandType::Bell, 320.0f, -4.0f, 1.2f);
            set(4, true, core::BandType::Bell, 2400.0f, 5.0f, 0.9f);
            set(5, true, core::BandType::Notch, 5200.0f, 0.0f, 6.0f);
            set(6, true, core::BandType::HighShelf, 9000.0f, 3.5f, 0.71f);
            selected_ = 4;
            // A plausible programme spectrum: a gentle tilt with a few peaks, so the screenshot shows the analyser.
            for (int k = 0; k < core::kSpectrumBins; ++k) {
                const double hz = std::sqrt(core::spectrumBandEdgeHz(k) * core::spectrumBandEdgeHz(k + 1));
                double db = -22.0 - 11.0 * std::log2(hz / 100.0) * (hz > 100.0 ? 1.0 : -0.4);
                db += 7.0 * std::sin(k * 1.7) * std::sin(k * 0.45) + (std::fabs(std::log2(hz / 2400.0)) < 0.25 ? 7.0 : 0.0);
                spectrum_[static_cast<size_t>(k)] = static_cast<float>(std::clamp(db, -90.0, 0.0));
            }
            setValue(core::kSpectrumSource, 2.0f);
            meter_ = {-9.0f, -11.5f};
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_)
            return;  // keep the demo curve
        if (core::isMeter(index)) {
            meter_[index - core::kMeterFirst] = v;
            repaint();
            return;
        }
        if (core::isSpectrumBin(index)) {
            spectrum_[index - core::kSpectrumFirst] = v;
            repaint();
            return;
        }
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(const int id) override
    {
        if (id >= 0 && id < core::kBands) {
            selected_ = id;
            repaint();
        }
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button == 1) {
            if (!ev.press) {
                dragging_ = -1;
                draggingOutput_ = false;
                return MagnetoUI::onMouse(ev);
            }
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
            if (outputHit_.contains(mx, my)) {
                draggingOutput_ = true;
                setOutputFromY(my);
                return true;
            }
            if (plot_.contains(mx, my)) {
                const int hit = nodeAt(mx, my);
                if (hit >= 0) {
                    selected_ = hit;
                    dragging_ = hit;
                    // Grabbing a band that is off turns it on, so a drag always has an audible result.
                    if (intValue(core::bandParameter(hit, core::kOn)) == 0)
                        commit(core::bandParameter(hit, core::kOn), 1.0f);
                    repaint();
                    return true;
                }
            }
        }
        return MagnetoUI::onMouse(ev);
    }

    bool onMotion(const MotionEvent& ev) override
    {
        if (draggingOutput_) {
            setOutputFromY(static_cast<float>(ev.pos.getY()));
            return true;
        }
        if (dragging_ >= 0) {
            drag(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    // The wheel over the plot changes the selected band's Q.
    bool onScroll(const ScrollEvent& ev) override
    {
        if (outputHit_.contains(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()))) {   // the wheel over the level slider: 0.5 dB a notch
            commit(core::kOutput, std::round((value(core::kOutput) + 0.5f * static_cast<float>(ev.delta.getY())) * 2.0f) * 0.5f);
            return true;
        }
        if (!plot_.contains(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY())))
            return MagnetoUI::onScroll(ev);
        const std::uint32_t pq = core::bandParameter(selected_, core::kQ);
        commit(pq, value(pq) * std::pow(1.12f, static_cast<float>(ev.delta.getY())));
        return true;
    }

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("OCTET", "eight-band equalizer",
                   "Drag a node: across for frequency, up and down for gain. Scroll over the plot to change Q. "
                   "Cuts and notches have no gain.");
        // The right-hand column is the output level; the three panels share the rest of the width.
        const float columnW = 96.0f, gap = 12.0f;
        const float full = static_cast<float>(getWidth()) - 48.0f - columnW - gap;
        drawPlotPanel({24.0f, 108.0f, full, 280.0f});
        drawBandPanel({24.0f, 400.0f, full, 188.0f});
        drawGlobalPanel({24.0f, 600.0f, full, 144.0f});
        drawOutputPanel({24.0f + full + gap, 108.0f, columnW, 636.0f});
    }

private:
    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kSpectrumFirst; ++i)
            core::field(p, i) = value(i);
        return p;
    }

    [[nodiscard]] core::Band band(const int b) const
    {
        core::Parameters p = currentParameters();
        return p.bands[static_cast<size_t>(b)];
    }

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
    [[nodiscard]] float yOf(const float db) const
    {
        return plot_.y + (kPlotMaxDb - std::clamp(db, -kPlotMaxDb, kPlotMaxDb)) / (2.0f * kPlotMaxDb) * plot_.h;
    }
    [[nodiscard]] float dbOf(const float y) const
    {
        return kPlotMaxDb - (y - plot_.y) / std::max(1.0f, plot_.h) * 2.0f * kPlotMaxDb;
    }

    [[nodiscard]] float nodeY(const core::Band& b) const { return yOf(core::bandUsesGain(b) ? b.gainDb : 0.0f); }

    [[nodiscard]] int nodeAt(const float mx, const float my) const
    {
        int best = -1;
        float bestDist = 16.0f * 16.0f;
        for (int i = 0; i < core::kBands; ++i) {
            const core::Band b = band(i);
            const float dx = xOf(b.freqHz) - mx, dy = nodeY(b) - my;
            const float d = dx * dx + dy * dy;
            // Prefer the selected band when nodes overlap.
            if (d < bestDist || (d == bestDist && i == selected_)) {
                bestDist = d;
                best = i;
            }
        }
        return best;
    }

    void drag(const float mx, const float my)
    {
        commit(core::bandParameter(dragging_, core::kFreq), hzOf(mx));
        if (core::bandUsesGain(band(dragging_)))
            commit(core::bandParameter(dragging_, core::kGain), std::clamp(dbOf(my), -18.0f, 18.0f));
    }

    void drawPlotPanel(const Rect b)
    {
        drawPanel(b, "RESPONSE", kPlotAccent, "dB; each band's curve, the total, and the spectrum behind them");
        const auto& t = theme();
        plot_ = {b.x + 46.0f, b.y + 44.0f, b.w - 46.0f - 18.0f, b.h - 44.0f - 28.0f};
        drawPlotFrame(plot_);

        for (int db = -24; db <= 24; db += 6) {
            beginPath();
            sc(t.border);
            strokeWidth(db == 0 ? 1.6f : 0.8f);
            moveTo(plot_.x, yOf(static_cast<float>(db)));
            lineTo(plot_.x + plot_.w, yOf(static_cast<float>(db)));
            stroke();
            if (db % 12 == 0) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%+d", db);
                label(plot_.x - 8.0f, yOf(static_cast<float>(db)), buf, true, 10.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
            }
        }
        static constexpr float kMarks[] = {50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f};
        for (const float hz : kMarks) {
            beginPath();
            sc(t.border);
            strokeWidth(0.8f);
            moveTo(xOf(hz), plot_.y);
            lineTo(xOf(hz), plot_.y + plot_.h);
            stroke();
            char buf[16];
            if (hz >= 1000.0f) std::snprintf(buf, sizeof(buf), "%g k", static_cast<double>(hz) / 1000.0);
            else std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(hz));
            label(xOf(hz), plot_.y + plot_.h + 6.0f, buf, true, 10.0f, ALIGN_CENTER | ALIGN_TOP);
        }

        drawSpectrum();

        const double sr = getSampleRate() > 0.0 ? getSampleRate() : 48000.0;
        const core::Parameters p = currentParameters();
        const int points = static_cast<int>(plot_.w);

        // One faint curve per active band, then the total.
        for (int i = 0; i < core::kBands; ++i) {
            const core::Band bd = p.bands[static_cast<size_t>(i)];
            if (!core::bandIsActive(bd)) continue;
            beginPath();
            strokeColor(kBandColours[static_cast<size_t>(i)][0], kBandColours[static_cast<size_t>(i)][1],
                        kBandColours[static_cast<size_t>(i)][2], i == selected_ ? 200 : 90);
            strokeWidth(i == selected_ ? 1.8f : 1.2f);
            bool pen = false;
            for (int k = 0; k <= points; ++k) {
                const float n = static_cast<float>(k) / static_cast<float>(points);
                const double hz = kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
                const double db = core::bandResponseDb(bd, hz, sr);
                if (db < -kPlotMaxDb) { pen = false; continue; }
                const float x = plot_.x + n * plot_.w, y = yOf(static_cast<float>(db));
                if (pen) lineTo(x, y); else moveTo(x, y);
                pen = true;
            }
            stroke();
        }
        beginPath();
        strokeColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 255);
        strokeWidth(2.6f);
        bool pen = false;
        for (int k = 0; k <= points; ++k) {
            const float n = static_cast<float>(k) / static_cast<float>(points);
            const double hz = kPlotMinHz * std::pow(kPlotMaxHz / kPlotMinHz, n);
            const double db = core::responseDb(p, hz, sr);
            if (db < -kPlotMaxDb) { pen = false; continue; }
            const float x = plot_.x + n * plot_.w, y = yOf(static_cast<float>(db));
            if (pen) lineTo(x, y); else moveTo(x, y);
            pen = true;
        }
        stroke();

        // Nodes.
        for (int i = 0; i < core::kBands; ++i) {
            const core::Band bd = p.bands[static_cast<size_t>(i)];
            const auto& c = kBandColours[static_cast<size_t>(i)];
            const bool on = bd.on >= 0.5f;
            const float x = xOf(bd.freqHz), y = nodeY(bd);
            const float r = i == selected_ ? 9.0f : 7.0f;
            beginPath();
            circle(x, y, r);
            if (on) {
                fillColor(c[0], c[1], c[2], 255);
                fill();
            } else {
                fc(t.panel);
                fill();
                strokeColor(c[0], c[1], c[2], 200);
                strokeWidth(1.6f);
                stroke();
            }
            if (i == selected_) {
                beginPath();
                circle(x, y, r + 3.0f);
                strokeColor(240, 236, 224, 255);
                strokeWidth(1.5f);
                stroke();
            }
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            fillColor(on ? 20 : c[0], on ? 20 : c[1], on ? 20 : c[2], 255);
            fontSize(10.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(x, y, num, nullptr);
        }
    }

    // The analyser: one bar per band from the floor, behind the curves. The 64 levels are read-only outputs measured from
    // the audio (pre- or post-EQ, per the Spectrum switch); the plot's dB grid is the EQ's, so the bars have their own scale.
    void drawSpectrum()
    {
        if (intValue(core::kSpectrumSource) == 0)
            return;
        const auto& t = theme();
        const float barW = plot_.w / static_cast<float>(core::kSpectrumBins);
        const bool pre = intValue(core::kSpectrumSource) == 1;
        for (int k = 0; k < core::kSpectrumBins; ++k) {
            const float db = spectrum_[static_cast<size_t>(k)];
            const float n = std::clamp((db - core::kSpectrumFloorDb) / (-core::kSpectrumFloorDb), 0.0f, 1.0f);
            if (n <= 0.0f)
                continue;
            // The bands are log-spaced, so equal widths match the plot's frequency axis.
            const float h = n * plot_.h * 0.9f;
            beginPath();
            rect(plot_.x + static_cast<float>(k) * barW + 0.5f, plot_.y + plot_.h - h, barW - 1.0f, h);
            if (pre) fillColor(t.textDim.r, t.textDim.g, t.textDim.b, 95);
            else fillColor(kPlotAccent.r, kPlotAccent.g, kPlotAccent.b, 90);
            fill();
        }
    }

    void drawBandPanel(const Rect b)
    {
        drawPanel(b, "BAND", kBandAccent, "the selected band");
        const auto& c = kBandColours[static_cast<size_t>(selected_)];
        const kit::Accent accent {c[0], c[1], c[2]};
        const std::uint32_t pOn = core::bandParameter(selected_, core::kOn);
        const std::uint32_t pType = core::bandParameter(selected_, core::kType);
        const std::uint32_t pFreq = core::bandParameter(selected_, core::kFreq);
        const std::uint32_t pGain = core::bandParameter(selected_, core::kGain);
        const std::uint32_t pQ = core::bandParameter(selected_, core::kQ);
        const std::uint32_t pChannel = core::bandParameter(selected_, core::kChannel);
        const core::Band bd = band(selected_);

        // Band buttons: 1-8, filled for the selected band.
        float x = b.x + 14.0f;
        const float y = b.y + 44.0f;
        for (int i = 0; i < core::kBands; ++i) {
            char num[4];
            std::snprintf(num, sizeof(num), "%d", i + 1);
            const auto& bc = kBandColours[static_cast<size_t>(i)];
            const bool on = band(i).on >= 0.5f;
            setDim(!on);
            drawButton(i, num, {x, y, 38.0f, 28.0f}, {bc[0], bc[1], bc[2]}, i == selected_);
            setDim(false);
            x += 44.0f;
        }
        x += 18.0f;
        static constexpr const char* kOnNames[2] = {"Off", "On"};
        char bandLabel[32];
        std::snprintf(bandLabel, sizeof(bandLabel), "Band %d", selected_ + 1);
        label(x, y - 2.0f, bandLabel);
        drawSegments(pOn, kOnNames, 2, {x, y + 12.0f, 120.0f, 24.0f}, accent);
        x += 144.0f;

        const int mode = intValue(core::kMode);
        static constexpr const char* kChanStereo[3] = {"L + R", "Left", "Right"};
        static constexpr const char* kChanMid[3] = {"M + S", "Mid", "Side"};
        const char* const* chanNames = mode == 2 ? kChanMid : kChanStereo;
        setDim(mode == 0);
        label(x, y - 2.0f, mode == 0 ? "Acts on (Stereo mode: both)" : "Acts on");
        drawSegments(pChannel, chanNames, 3, {x, y + 12.0f, b.x + b.w - 14.0f - x, 24.0f}, accent);
        setDim(false);

        static constexpr const char* kTypeNames[core::kBandTypeCount] = {"HP 12", "HP 48", "Lo Shelf", "Bell",
                                                                         "Notch", "Hi Shelf", "LP 12", "LP 48"};
        label(b.x + 14.0f, b.y + 90.0f, "Type");
        drawSegments(pType, kTypeNames, core::kBandTypeCount, {b.x + 14.0f, b.y + 106.0f, b.w - 28.0f, 24.0f}, accent);

        const float gap = 24.0f;
        const float w = (b.w - 28.0f - 2.0f * gap) / 3.0f;
        const float sy = b.y + 142.0f;
        drawSlider(pFreq, "Frequency", format(pFreq).c_str(), {b.x + 14.0f, sy, w, 34.0f}, accent);
        setDim(!core::bandUsesGain(bd));
        drawSlider(pGain, core::bandUsesGain(bd) ? "Gain" : "Gain (not used by this type)", format(pGain).c_str(),
                   {b.x + 14.0f + w + gap, sy, w, 34.0f}, accent);
        setDim(!core::bandUsesQ(bd));
        drawSlider(pQ, core::bandUsesQ(bd) ? "Q" : "Q (not used by this type)", format(pQ).c_str(),
                   {b.x + 14.0f + 2.0f * (w + gap), sy, w, 34.0f}, accent);
        setDim(false);
    }

    void drawGlobalPanel(const Rect b)
    {
        drawPanel(b, "STEREO, SPECTRUM AND MIDI", kGlobalAccent, "CC 1-4: band Freq, Gain, Q; Output");
        static constexpr const char* kModeNames[3] = {"Stereo", "Left / Right", "Mid / Side"};
        label(b.x + 14.0f, b.y + 40.0f, "Mode");
        drawSegments(core::kMode, kModeNames, 3, {b.x + 14.0f, b.y + 52.0f - 6.0f + 4.0f, 290.0f, 22.0f}, kGlobalAccent);
        // The analyser behind the response curve: before or after the EQ.
        static constexpr const char* kSpectrumNames[3] = {"Off", "Pre", "Post"};
        label(b.x + 330.0f, b.y + 40.0f, "Spectrum (before / after the EQ)");
        drawSegments(core::kSpectrumSource, kSpectrumNames, 3, {b.x + 330.0f, b.y + 52.0f - 6.0f + 4.0f, 220.0f, 22.0f}, kGlobalAccent);
        // MIDI: Drift's CC 1-4 drive one band's Frequency, Gain and Q, and the Output.
        const float w = (b.w - 28.0f - 24.0f) / 2.0f;
        drawSlider(core::kCcChannel, "CC channel (0 = ignore MIDI)", format(core::kCcChannel).c_str(), {b.x + 14.0f, b.y + 98.0f, w, 34.0f}, kGlobalAccent);
        drawSlider(core::kCcBand, "CC band (the band CC 1-3 drive)", format(core::kCcBand).c_str(), {b.x + 14.0f + w + 24.0f, b.y + 98.0f, w, 34.0f}, kGlobalAccent);
    }

    // The output level: a large vertical slider (+12 dB at the top, 0 dB marked) with a small stereo peak meter under it.
    static constexpr float kOutputMinDb = -12.0f, kOutputMaxDb = 12.0f;

    void setOutputFromY(const float y)
    {
        const float n = 1.0f - (y - outputTrack_.y) / std::max(1.0f, outputTrack_.h);
        commit(core::kOutput, kOutputMinDb + std::clamp(n, 0.0f, 1.0f) * (kOutputMaxDb - kOutputMinDb));
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "OUTPUT", kGlobalAccent);
        const auto& t = theme();
        const float cx = b.x + b.w * 0.5f;
        label(cx, b.y + 44.0f, format(core::kOutput).c_str(), false, 13.0f, ALIGN_CENTER | ALIGN_TOP);

        outputTrack_ = {cx - 6.0f, b.y + 74.0f, 12.0f, 392.0f};
        outputHit_ = {b.x + 4.0f, outputTrack_.y - 12.0f, b.w - 8.0f, outputTrack_.h + 24.0f};
        const auto yOfDb = [&](const float db) {
            return outputTrack_.y + (1.0f - (db - kOutputMinDb) / (kOutputMaxDb - kOutputMinDb)) * outputTrack_.h;
        };
        beginPath();
        fc(t.controlTrack);
        roundedRect(outputTrack_.x, outputTrack_.y, outputTrack_.w, outputTrack_.h, 4.0f);
        fill();
        const float v = std::clamp(value(core::kOutput), kOutputMinDb, kOutputMaxDb);
        const float thumbY = yOfDb(v);
        // The fill runs from the 0 dB mark to the thumb, so a cut and a boost read as different directions.
        const float zeroY = yOfDb(0.0f);
        beginPath();
        fillColor(kGlobalAccent.r, kGlobalAccent.g, kGlobalAccent.b, 255);
        rect(outputTrack_.x, std::min(thumbY, zeroY), outputTrack_.w, std::max(2.0f, std::fabs(zeroY - thumbY)));
        fill();
        for (int db = -12; db <= 12; db += 6) {
            const float y = yOfDb(static_cast<float>(db));
            beginPath();
            sc(db == 0 ? t.textDim : t.border);
            strokeWidth(db == 0 ? 1.6f : 1.0f);
            moveTo(outputTrack_.x - 16.0f, y);
            lineTo(outputTrack_.x - 8.0f, y);
            moveTo(outputTrack_.x + outputTrack_.w + 8.0f, y);
            lineTo(outputTrack_.x + outputTrack_.w + 16.0f, y);
            stroke();
            char buf[8];
            std::snprintf(buf, sizeof(buf), db == 0 ? "0" : "%+d", db);
            label(b.x + 6.0f, y, buf, true, 10.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        }
        beginPath();
        fc(t.buttonFace);
        roundedRect(cx - 20.0f, thumbY - 8.0f, 40.0f, 16.0f, downspout::laf::kRadiusSmall);
        fill();
        beginPath();
        strokeColor(kGlobalAccent.r, kGlobalAccent.g, kGlobalAccent.b, 255);
        strokeWidth(2.0f);
        roundedRect(cx - 20.0f, thumbY - 8.0f, 40.0f, 16.0f, downspout::laf::kRadiusSmall);
        stroke();
        beginPath();
        fc(t.textPrimary);
        rect(cx - 12.0f, thumbY - 1.0f, 24.0f, 2.0f);
        fill();

        // Meter: two peak bars, green to amber to red, floor to +12 dB.
        label(cx, b.y + 484.0f, "Level", true, 11.0f, ALIGN_CENTER | ALIGN_TOP);
        const Rect m {cx - 20.0f, b.y + 502.0f, 40.0f, 96.0f};
        const float barW = 16.0f;
        for (int c = 0; c < core::kMeterChannels; ++c) {
            const float x = m.x + static_cast<float>(c) * (m.w - barW);
            beginPath();
            fc(t.controlTrack);
            roundedRect(x, m.y, barW, m.h, 3.0f);
            fill();
            const auto at = [&](const float db) { return m.y + m.h - std::clamp((db - core::kMeterFloorDb) / (kOutputMaxDb - core::kMeterFloorDb), 0.0f, 1.0f) * m.h; };
            const float top = at(meter_[static_cast<size_t>(c)]);
            struct Zone { float lo, hi; int r, g, bl; };
            static constexpr Zone kZones[] = {{core::kMeterFloorDb, -12.0f, 132, 190, 96}, {-12.0f, 0.0f, 222, 170, 70}, {0.0f, kOutputMaxDb, 214, 96, 84}};
            for (const Zone& z : kZones) {
                const float y0 = std::max(top, at(z.hi)), y1 = at(z.lo);
                if (y1 <= y0) continue;
                beginPath();
                fillColor(z.r, z.g, z.bl, 255);
                rect(x, y0, barW, y1 - y0);
                fill();
            }
        }
        const float peak = std::max(meter_[0], meter_[1]);
        char buf[16];
        if (peak <= core::kMeterFloorDb + 0.05f) std::snprintf(buf, sizeof(buf), "-inf");
        else std::snprintf(buf, sizeof(buf), "%.1f dB", static_cast<double>(peak));
        label(cx, m.y + m.h + 8.0f, buf, true, 11.0f, ALIGN_CENTER | ALIGN_TOP);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
        if (parameter == core::kCcChannel) {   // appended after the band controls, so decode it before the per-band branch
            if (v < 0.5f)
                return "off";
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            return buffer;
        }
        if (parameter == core::kSpectrumSource) {   // appended after the band controls, so decode before the per-band branch
            static constexpr const char* kNames[3] = {"off", "pre", "post"};
            return kNames[std::clamp(static_cast<int>(std::lround(v)), 0, 2)];
        }
        if (parameter == core::kCcBand) {
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            return buffer;
        }
        if (parameter == core::kOutput) {
            std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
        } else if (parameter >= core::kFirstBand) {
            switch ((parameter - core::kFirstBand) % core::kPerBand) {
            case core::kFreq:
                if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f kHz", static_cast<double>(v) / 1000.0);
                else std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
                break;
            case core::kGain:
                std::snprintf(buffer, sizeof(buffer), "%+.1f dB", static_cast<double>(v));
                break;
            case core::kQ:
                std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
                break;
            default:
                std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
                break;
            }
        } else {
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
        }
        return buffer;
    }

    Rect plot_ {};
    Rect outputTrack_ {}, outputHit_ {};
    std::array<float, core::kMeterChannels> meter_ {core::kMeterFloorDb, core::kMeterFloorDb};
    bool draggingOutput_ = false;
    std::array<float, core::kSpectrumBins> spectrum_ {};
    int selected_ = 2;
    int dragging_ = -1;
    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctetUI)
};

UI* createUI()
{
    return new OctetUI();
}

END_NAMESPACE_DISTRHO
