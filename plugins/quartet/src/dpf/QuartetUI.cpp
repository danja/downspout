#include "downspout/dpf/MagnetoKit.hpp"

#include "quartet_core.hpp"
#include "quartet_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::quartet;

namespace {

constexpr kit::Accent kDiagramAccent {198, 132, 58};  // amber
constexpr kit::Accent kGlobalAccent {120, 132, 96};   // sage
constexpr std::array<kit::Accent, core::kOperators> kOpAccent = {{{198, 120, 64}, {92, 140, 156}, {140, 120, 170}, {176, 90, 70}}};
constexpr std::array<const char*, core::kOperators> kOpNames = {"A", "B", "C", "D"};
constexpr std::array<const char*, 3> kWaveNames = {"Sine", "Saw", "Square"};
constexpr std::array<const char*, 2> kModeNames = {"Ratio", "Fixed"};
constexpr std::array<const char*, 5> kFilterNames = {"LP 12", "LP 24", "HP 12", "BP", "Notch"};
constexpr std::array<const char*, 2> kOnNames = {"Off", "On"};
constexpr std::array<const char*, 2> kQualityNames = {"1x", "2x"};

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

class QuartetUI : public MagnetoUI
{
public:
    QuartetUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        // The standalone used for screenshots starts at the defaults; show a more characteristic patch.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            const std::pair<std::uint32_t, float> values[] = {
                {core::kAlgorithm, 3}, {core::kFeedback, 0.3f}, {core::kDepth, 1.2f},
                {core::operatorParameter(0, core::kRatio), 1.0f}, {core::operatorParameter(0, core::kLevel), 0.8f},
                {core::operatorParameter(1, core::kRatio), 3.5f}, {core::operatorParameter(1, core::kLevel), 0.35f},
                {core::operatorParameter(1, core::kDecay), 600.0f},
                {core::operatorParameter(2, core::kRatio), 2.0f}, {core::operatorParameter(2, core::kLevel), 0.3f},
                {core::operatorParameter(2, core::kWave), 1.0f}, {core::operatorParameter(2, core::kDecay), 180.0f},
                {core::operatorParameter(3, core::kRatio), 1.0f}, {core::operatorParameter(3, core::kLevel), 0.45f},
                {core::operatorParameter(3, core::kFine), 7.0f},
                {core::kPitchEnvOn, 1}, {core::kPitchEnvSemitones, 7.0f}, {core::kPitchDecay, 60.0f},
                {core::kFilterType, 1}, {core::kCutoff, 6000.0f}, {core::kResonance, 0.2f}, {core::kKeyTrack, 0.3f},
            };
            for (const auto& [id, v] : values) setValue(id, v);
        }
    }

protected:
    void parameterChanged(const uint32_t index, const float v) override
    {
        if (demo_)
            return;  // keep the demo patch
        MagnetoUI::parameterChanged(index, v);
    }

    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("QUARTET", "four-operator FM synthesizer",
                   "The algorithm decides which operators modulate which and which you hear. A modulator's Level is how hard it pushes.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawAlgorithmPanel({24.0f, 108.0f, full, 190.0f});
        const float gap = 12.0f;
        const float colW = (full - 3.0f * gap) / 4.0f;
        for (int o = 0; o < core::kOperators; ++o)
            drawOperatorPanel(o, {24.0f + static_cast<float>(o) * (colW + gap), 310.0f, colW, 504.0f});
        drawGlobalPanel({24.0f, 826.0f, full, 156.0f});
    }

private:
    void slider(const std::uint32_t id, const char* name, const Rect r, const Accent a)
    {
        drawSlider(id, name, format(id).c_str(), r, a);
    }

    void segments(const std::uint32_t id, const char* name, const char* const* names, const int count, const Rect r, const Accent a)
    {
        label(r.x, r.y - 16.0f, name);
        drawSegments(id, names, count, r, a);
    }

    [[nodiscard]] bool isCarrier(const int op) const
    {
        return (core::algorithm(intValue(core::kAlgorithm)).carriers >> op) & 1u;
    }

    void arrow(const float x0, const float y0, const float x1, const float y1, const kit::Accent a)
    {
        beginPath();
        strokeColor(a.r, a.g, a.b, 230);
        strokeWidth(2.0f);
        moveTo(x0, y0);
        lineTo(x1, y1);
        stroke();
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::max(std::sqrt(dx * dx + dy * dy), 1.0f);
        const float ux = dx / len, uy = dy / len;
        beginPath();
        fillColor(a.r, a.g, a.b, 230);
        moveTo(x1, y1);
        lineTo(x1 - ux * 9.0f - uy * 5.0f, y1 - uy * 9.0f + ux * 5.0f);
        lineTo(x1 - ux * 9.0f + uy * 5.0f, y1 - uy * 9.0f - ux * 5.0f);
        closePath();
        fill();
    }

    void drawAlgorithmPanel(const Rect b)
    {
        drawPanel(b, "ALGORITHM", kDiagramAccent, "who modulates whom, and who is heard");
        const auto& t = theme();
        const int index = intValue(core::kAlgorithm);
        const core::Algorithm& alg = core::algorithm(index);

        // Routing diagram, drawn from the table the audio uses: carriers along the bottom, modulators above.
        const Rect area {b.x + 14.0f, b.y + 42.0f, b.w * 0.62f, b.h - 52.0f};
        drawPlotFrame(area);
        int layers = 0;
        std::array<int, core::kOperators> layer {};
        for (int o = 0; o < core::kOperators; ++o) { layer[static_cast<size_t>(o)] = core::operatorLayer(alg, o); layers = std::max(layers, layer[static_cast<size_t>(o)]); }
        std::array<float, core::kOperators> cx {}, cy {};
        const float boxW = 86.0f, boxH = 28.0f, rowPitch = 33.0f, outMargin = 66.0f;
        // Layers run left to right: the deepest modulators on the left, the carriers against the OUT bar on the right.
        const float usableW = area.w - outMargin - boxW;
        for (int l = 0; l <= layers; ++l) {
            std::array<int, core::kOperators> members {};
            int count = 0;
            for (int o = 0; o < core::kOperators; ++o) if (layer[static_cast<size_t>(o)] == l) members[static_cast<size_t>(count++)] = o;
            for (int k = 0; k < count; ++k) {
                const int o = members[static_cast<size_t>(k)];
                cx[static_cast<size_t>(o)] = area.x + 10.0f + boxW * 0.5f + (layers == 0 ? usableW : usableW * static_cast<float>(layers - l) / static_cast<float>(layers));
                cy[static_cast<size_t>(o)] = area.y + area.h * 0.5f + (static_cast<float>(k) - (static_cast<float>(count) - 1.0f) * 0.5f) * rowPitch;
            }
        }
        const float outX = area.x + area.w - 12.0f;
        // Arrows first, so the boxes sit on top.
        for (int target = 0; target < core::kOperators; ++target)
            for (int m = target + 1; m < core::kOperators; ++m)
                if ((alg.modulators[static_cast<size_t>(target)] >> m) & 1u)
                    arrow(cx[static_cast<size_t>(m)] + boxW * 0.5f, cy[static_cast<size_t>(m)], cx[static_cast<size_t>(target)] - boxW * 0.5f - 1.0f, cy[static_cast<size_t>(target)], kOpAccent[static_cast<size_t>(m)]);
        beginPath();
        sc(t.border);
        strokeWidth(3.0f);
        moveTo(outX, area.y + 8.0f);
        lineTo(outX, area.y + area.h - 8.0f);
        stroke();
        label(outX - 4.0f, area.y + 12.0f, "OUT", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);
        for (int o = 0; o < core::kOperators; ++o) {
            const auto& a = kOpAccent[static_cast<size_t>(o)];
            const bool carrier = (alg.carriers >> o) & 1u;
            if (carrier) arrow(cx[static_cast<size_t>(o)] + boxW * 0.5f, cy[static_cast<size_t>(o)], outX - 1.0f, cy[static_cast<size_t>(o)], a);
            beginPath();
            fillColor(a.r, a.g, a.b, 255);
            roundedRect(cx[static_cast<size_t>(o)] - boxW * 0.5f, cy[static_cast<size_t>(o)] - boxH * 0.5f, boxW, boxH, 6.0f);
            fill();
            char caption[32];
            const std::uint32_t base = static_cast<std::uint32_t>(o) * core::kPerOperator;
            if (intValue(base + core::kFixedMode) == 1) std::snprintf(caption, sizeof(caption), "%s  %.0f Hz", kOpNames[static_cast<size_t>(o)], static_cast<double>(value(base + core::kFixedHz)));
            else std::snprintf(caption, sizeof(caption), "%s  x%.2f", kOpNames[static_cast<size_t>(o)], static_cast<double>(value(base + core::kRatio)));
            fillColor(250, 248, 242, 255);
            fontSize(13.0f);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(cx[static_cast<size_t>(o)], cy[static_cast<size_t>(o)], caption, nullptr);
        }
        // Feedback loop on D.
        if (value(core::kFeedback) > 0.001f) {
            const float x = cx[3], y = cy[3] - boxH * 0.5f;
            beginPath();
            strokeColor(kOpAccent[3].r, kOpAccent[3].g, kOpAccent[3].b, 230);
            strokeWidth(2.0f);
            moveTo(x - 14.0f, y);
            bezierTo(x - 22.0f, y - 20.0f, x + 22.0f, y - 20.0f, x + 14.0f, y);
            stroke();
            label(x + 26.0f, y - 12.0f, "fb", true, 10.0f, ALIGN_LEFT | ALIGN_MIDDLE);
        }

        // Controls to the right of the diagram.
        const float rx = area.x + area.w + 20.0f, rw = b.x + b.w - 14.0f - rx;
        char shown[32];
        std::snprintf(shown, sizeof(shown), "%d of %d", index + 1, core::kAlgorithms);
        drawStepper(core::kAlgorithm, "Algorithm (click left or right)", shown, {rx, b.y + 44.0f, rw, 36.0f});
        slider(core::kFeedback, "Feedback (operator D into itself)", {rx, b.y + 88.0f, rw, 34.0f}, kDiagramAccent);
        slider(core::kDepth, "FM depth", {rx, b.y + 124.0f, rw * 0.5f - 8.0f, 34.0f}, kDiagramAccent);
        static constexpr const char* const* kQ = kQualityNames.data();
        label(rx + rw * 0.5f + 8.0f, b.y + 124.0f, "Oversampling");
        drawSegments(core::kQuality, kQ, 2, {rx + rw * 0.5f + 8.0f, b.y + 142.0f, rw * 0.5f - 8.0f, 24.0f}, kDiagramAccent);
    }

    void drawOperatorPanel(const int o, const Rect b)
    {
        const kit::Accent a = kOpAccent[static_cast<size_t>(o)];
        const bool carrier = isCarrier(o);
        char title[40];
        std::snprintf(title, sizeof(title), "OPERATOR %s", kOpNames[static_cast<size_t>(o)]);
        drawPanel(b, title, a, carrier ? "carrier (heard)" : "modulator");
        const std::uint32_t base = static_cast<std::uint32_t>(o) * core::kPerOperator;
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        float y = b.y + 58.0f;
        segments(base + core::kWave, "Waveform", kWaveNames.data(), 3, {b.x + pad, y, w, 24.0f}, a);
        y += 46.0f;
        segments(base + core::kFixedMode, "Frequency", kModeNames.data(), 2, {b.x + pad, y, w, 24.0f}, a);
        y += 40.0f;
        const bool fixed = intValue(base + core::kFixedMode) == 1;
        setDim(fixed);
        slider(base + core::kRatio, "Ratio (x the note)", {b.x + pad, y, w, 34.0f}, a);
        y += 44.0f;
        slider(base + core::kFine, "Fine", {b.x + pad, y, w, 34.0f}, a);
        setDim(!fixed);
        y += 44.0f;
        slider(base + core::kFixedHz, "Fixed frequency", {b.x + pad, y, w, 34.0f}, a);
        setDim(false);
        y += 44.0f;
        slider(base + core::kLevel, carrier ? "Level (output)" : "Level (FM amount)", {b.x + pad, y, w, 34.0f}, a);
        y += 50.0f;
        for (const auto& [id, name] : {std::pair<std::uint32_t, const char*>{core::kAttack, "Attack"}, {core::kDecay, "Decay"},
                                       {core::kSustain, "Sustain"}, {core::kRelease, "Release"}}) {
            slider(base + id, name, {b.x + pad, y, w, 34.0f}, a);
            y += 44.0f;
        }
    }

    void drawGlobalPanel(const Rect b)
    {
        drawPanel(b, "PITCH ENVELOPE, FILTER AND PERFORMANCE", kGlobalAccent);
        const float pad = 14.0f, gap = 14.0f;
        // Row 1: the pitch envelope.
        {
            const float w = (b.w - 2.0f * pad - 5.0f * gap) / 6.0f;
            float x = b.x + pad;
            const float y = b.y + 56.0f;
            label(x, y - 16.0f, "Pitch envelope");
            drawSegments(core::kPitchEnvOn, kOnNames.data(), 2, {x, y, w, 24.0f}, kGlobalAccent);
            const bool on = intValue(core::kPitchEnvOn) == 1;
            setDim(!on);
            const std::uint32_t ids[5] = {core::kPitchEnvSemitones, core::kPitchAttack, core::kPitchDecay, core::kPitchSustain, core::kPitchRelease};
            const char* const names[5] = {"Amount", "Attack", "Decay", "Sustain", "Release"};
            for (int i = 0; i < 5; ++i) {
                x += w + gap;
                slider(ids[i], names[i], {x, y - 6.0f, w, 34.0f}, kGlobalAccent);
            }
            setDim(false);
        }
        // Row 2: the filter and the performance controls.
        {
            const float segW = 270.0f;
            const float w = (b.w - 2.0f * pad - segW - 6.0f * gap) / 6.0f;
            float x = b.x + pad;
            const float y = b.y + 114.0f;
            label(x, y - 16.0f, "Filter");
            drawSegments(core::kFilterType, kFilterNames.data(), 5, {x, y, segW, 24.0f}, kGlobalAccent);
            x += segW;
            const std::uint32_t ids[6] = {core::kCutoff, core::kResonance, core::kKeyTrack, core::kMaster, core::kVelocity, core::kBendRange};
            const char* const names[6] = {"Cutoff", "Resonance", "Key track", "Master", "Velocity", "Bend range"};
            for (int i = 0; i < 6; ++i) {
                x += gap;
                slider(ids[i], names[i], {x, y - 6.0f, w, 34.0f}, kGlobalAccent);
                x += w;
            }
        }
    }

    [[nodiscard]] std::string format(const std::uint32_t id) const
    {
        char buffer[32];
        const float v = value(id);
        if (id < core::kGlobalBase) {
            switch (id % core::kPerOperator) {
            case core::kRatio:   std::snprintf(buffer, sizeof(buffer), "x %.3f", static_cast<double>(v)); break;
            case core::kFine:    std::snprintf(buffer, sizeof(buffer), "%+.0f ct", static_cast<double>(v)); break;
            case core::kFixedHz: std::snprintf(buffer, sizeof(buffer), v >= 1000.0f ? "%.2f kHz" : "%.0f Hz", v >= 1000.0f ? static_cast<double>(v) / 1000.0 : static_cast<double>(v)); break;
            case core::kAttack: case core::kDecay: case core::kRelease:
                if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v) / 1000.0);
                else std::snprintf(buffer, sizeof(buffer), v < 10.0f ? "%.1f ms" : "%.0f ms", static_cast<double>(v));
                break;
            default:             std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v)); break;
            }
            return buffer;
        }
        switch (id) {
        case core::kPitchEnvSemitones: std::snprintf(buffer, sizeof(buffer), "%+.0f st", static_cast<double>(v)); break;
        case core::kPitchAttack: case core::kPitchDecay: case core::kPitchRelease:
            if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v) / 1000.0);
            else std::snprintf(buffer, sizeof(buffer), v < 10.0f ? "%.1f ms" : "%.0f ms", static_cast<double>(v));
            break;
        case core::kCutoff:
            if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f kHz", static_cast<double>(v) / 1000.0);
            else std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
            break;
        case core::kBendRange: std::snprintf(buffer, sizeof(buffer), "%.0f st", static_cast<double>(v)); break;
        default:               std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v)); break;
        }
        return buffer;
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuartetUI)
};

UI* createUI()
{
    return new QuartetUI();
}

END_NAMESPACE_DISTRHO
