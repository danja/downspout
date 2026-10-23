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

class OctetUI : public MagnetoUI
{
public:
    OctetUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
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
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_)
            return;  // keep the demo curve
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
                return MagnetoUI::onMouse(ev);
            }
            const float mx = static_cast<float>(ev.pos.getX());
            const float my = static_cast<float>(ev.pos.getY());
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
        if (dragging_ >= 0) {
            drag(static_cast<float>(ev.pos.getX()), static_cast<float>(ev.pos.getY()));
            return true;
        }
        return MagnetoUI::onMotion(ev);
    }

    // The wheel over the plot changes the selected band's Q.
    bool onScroll(const ScrollEvent& ev) override
    {
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
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawPlotPanel({24.0f, 108.0f, full, 280.0f});
        drawBandPanel({24.0f, 400.0f, full, 188.0f});
        drawGlobalPanel({24.0f, 600.0f, full, 80.0f});
    }

private:
    [[nodiscard]] core::Parameters currentParameters() const
    {
        core::Parameters p;
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
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
        drawPanel(b, "RESPONSE", kPlotAccent, "dB; each band's curve, and the total");
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
        drawPanel(b, "STEREO AND OUTPUT", kGlobalAccent);
        static constexpr const char* kModeNames[3] = {"Stereo", "Left / Right", "Mid / Side"};
        label(b.x + 14.0f, b.y + 40.0f, "Mode");
        drawSegments(core::kMode, kModeNames, 3, {b.x + 14.0f, b.y + 52.0f - 6.0f + 4.0f, 380.0f, 22.0f}, kGlobalAccent);
        drawSlider(core::kOutput, "Output", format(core::kOutput).c_str(), {b.x + 430.0f, b.y + 40.0f, b.w - 430.0f - 14.0f, 34.0f},
                   kGlobalAccent);
    }

    [[nodiscard]] std::string format(const std::uint32_t parameter) const
    {
        char buffer[32];
        const float v = value(parameter);
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
