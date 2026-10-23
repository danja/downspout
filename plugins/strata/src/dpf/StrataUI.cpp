#include "downspout/dpf/MagnetoKit.hpp"

#include "strata_core.hpp"
#include "strata_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::strata;

namespace {

constexpr kit::Accent kOsc1Accent {198, 120, 64};     // copper
constexpr kit::Accent kOsc2Accent {198, 132, 58};     // amber
constexpr kit::Accent kUnisonAccent {176, 90, 70};    // brick
constexpr kit::Accent kFilterAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kEnvAccent {120, 132, 96};      // sage
constexpr kit::Accent kModAccent {140, 120, 170};     // heather

constexpr std::array<const char*, 5> kTableNames = {"Basic", "Harmonics", "Formant", "Digital", "Vintage"};
constexpr std::array<const char*, 4> kWarpNames = {"None", "Bend", "Sync", "Fold"};
constexpr std::array<const char*, 3> kUnisonNames = {"Classic", "Shimmer", "Noise"};
constexpr std::array<const char*, 5> kFilterNames = {"LP 12", "LP 24", "HP 12", "BP", "Notch"};
constexpr std::array<const char*, 2> kSubNames = {"-1 oct", "-2 oct"};
constexpr std::array<const char*, 3> kPlayNames = {"Poly", "Mono", "Legato"};
constexpr std::array<const char*, 5> kLfoNames = {"Sine", "Triangle", "Saw", "Square", "S&H"};

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

class StrataUI : public MagnetoUI
{
public:
    StrataUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data())
    {
        (void)core::bank();
        // The standalone used for screenshots starts at the defaults; show a more characteristic patch.
        if (std::getenv("DOWNSPOUT_SCREENSHOT_DEMO") != nullptr) {
            demo_ = true;
            const std::pair<std::uint32_t, float> values[] = {
                {core::kOsc1Table, 0}, {core::kOsc1Position, 0.62f}, {core::kOsc1Warp, 2}, {core::kOsc1WarpAmount, 0.30f},
                {core::kOsc1Level, 0.8f}, {core::kOsc2Table, 4}, {core::kOsc2Position, 0.55f}, {core::kOsc2Warp, 1},
                {core::kOsc2WarpAmount, 0.45f}, {core::kOsc2Transpose, -12}, {core::kOsc2Fine, 7}, {core::kOsc2Level, 0.6f},
                {core::kSubLevel, 0.25f}, {core::kUnisonMode, 0}, {core::kUnisonVoices, 5}, {core::kUnisonAmount, 0.35f},
                {core::kFilterType, 1}, {core::kCutoff, 1800.0f}, {core::kResonance, 0.35f}, {core::kKeyTrack, 0.4f},
                {core::kEnvAmount, 0.35f}, {core::kEnvPosition, 0.2f}, {core::kLfoRate, 0.6f}, {core::kLfoPosition, 0.25f},
                {core::kLfoCutoff, 0.15f},
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
        drawHeader("STRATA", "wavetable synthesizer",
                   "Each oscillator reads a band-limited table: Position scrolls through its eight frames, Warp bends the cycle. "
                   "The plot shows the waveform you are setting.");
        const float full = static_cast<float>(getWidth()) - 48.0f;
        const float half = (full - 12.0f) * 0.5f;
        drawOscPanel(0, {24.0f, 108.0f, half, 352.0f});
        drawOscPanel(1, {24.0f + half + 12.0f, 108.0f, half, 352.0f});
        const float third = (full - 24.0f) / 3.0f;
        drawUnisonPanel({24.0f, 472.0f, third, 318.0f});
        drawFilterPanel({24.0f + third + 12.0f, 472.0f, third, 318.0f});
        drawEnvelopePanel({24.0f + 2.0f * (third + 12.0f), 472.0f, third, 318.0f});
        drawModPanel({24.0f, 802.0f, full, 152.0f});
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

    void drawOscPanel(const int o, const Rect b)
    {
        const Accent a = o == 0 ? kOsc1Accent : kOsc2Accent;
        drawPanel(b, o == 0 ? "OSCILLATOR 1" : "OSCILLATOR 2", a);
        const std::uint32_t base = o == 0 ? core::kOsc1Table : core::kOsc2Table;
        const float pad = 14.0f;
        const float w = b.w - 2.0f * pad;

        // The waveform being set: the table frame at this position, through the warp.
        const Rect plot {b.x + pad, b.y + 42.0f, w, 70.0f};
        drawPlotFrame(plot);
        const int table = intValue(base + 0);
        const double position = value(base + 1);
        const auto mode = static_cast<core::WarpMode>(intValue(base + 2));
        const double amount = value(base + 3);
        const auto& bk = core::bank();
        beginPath();
        strokeColor(a.r, a.g, a.b, 255);
        strokeWidth(2.0f);
        const int steps = static_cast<int>(plot.w);
        float prevFold = 0.0f;
        for (int i = 0; i <= steps; ++i) {
            const double ph = static_cast<double>(i) / steps;
            const double p = core::warpPhase(mode, amount, ph);
            float s = bk.read(table, position, 3, p);
            if (mode == core::WarpMode::Fold)
                s = std::sin(static_cast<float>((1.0 + amount * 6.0) * 0.5 * 3.14159265358979) * s);
            (void)prevFold;
            const float x = plot.x + static_cast<float>(i) / static_cast<float>(steps) * plot.w;
            const float y = plot.y + plot.h * 0.5f - s * plot.h * 0.5f * 0.92f;
            if (i == 0) moveTo(x, y); else lineTo(x, y);
        }
        stroke();

        float y = b.y + 138.0f;
        segments(base + 0, "Table", kTableNames.data(), 5, {b.x + pad, y, w, 24.0f}, a);
        y += 36.0f;
        slider(base + 1, "Position (the eight frames)", {b.x + pad, y, w, 34.0f}, a);
        y += 54.0f;
        segments(base + 2, "Warp", kWarpNames.data(), 4, {b.x + pad, y, w, 24.0f}, a);
        y += 36.0f;
        setDim(mode == core::WarpMode::None);
        slider(base + 3, "Warp amount", {b.x + pad, y, w, 34.0f}, a);
        setDim(false);
        y += 52.0f;
        const float gap = 20.0f;
        const float cw = (w - 2.0f * gap) / 3.0f;
        slider(base + 4, "Transpose", {b.x + pad, y, cw, 34.0f}, a);
        slider(base + 5, "Fine", {b.x + pad + cw + gap, y, cw, 34.0f}, a);
        slider(base + 6, "Level", {b.x + pad + 2.0f * (cw + gap), y, cw, 34.0f}, a);
    }

    void drawUnisonPanel(const Rect b)
    {
        drawPanel(b, "SUB AND UNISON", kUnisonAccent);
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        float y = b.y + 46.0f;
        slider(core::kSubLevel, "Sub level (sine)", {b.x + pad, y, w, 34.0f}, kUnisonAccent);
        y += 62.0f;
        segments(core::kSubOctave, "Sub octave", kSubNames.data(), 2, {b.x + pad, y, w, 24.0f}, kUnisonAccent);
        y += 52.0f;
        segments(core::kUnisonMode, "Unison mode", kUnisonNames.data(), 3, {b.x + pad, y, w, 24.0f}, kUnisonAccent);
        y += 40.0f;
        slider(core::kUnisonVoices, "Unison voices", {b.x + pad, y, w, 34.0f}, kUnisonAccent);
        y += 48.0f;
        slider(core::kUnisonAmount, "Unison amount (detune, width)", {b.x + pad, y, w, 34.0f}, kUnisonAccent);
    }

    void drawFilterPanel(const Rect b)
    {
        drawPanel(b, "FILTER", kFilterAccent);
        const float pad = 14.0f, w = b.w - 2.0f * pad;
        float y = b.y + 62.0f;
        segments(core::kFilterType, "Type", kFilterNames.data(), 5, {b.x + pad, y, w, 24.0f}, kFilterAccent);
        y += 40.0f;
        slider(core::kCutoff, "Cutoff", {b.x + pad, y, w, 34.0f}, kFilterAccent);
        y += 44.0f;
        slider(core::kResonance, "Resonance", {b.x + pad, y, w, 34.0f}, kFilterAccent);
        y += 44.0f;
        slider(core::kKeyTrack, "Key track", {b.x + pad, y, w, 34.0f}, kFilterAccent);
        y += 44.0f;
        slider(core::kEnvAmount, "Filter envelope to cutoff", {b.x + pad, y, w, 34.0f}, kFilterAccent);
        y += 44.0f;
        slider(core::kEnvPosition, "Filter envelope to position", {b.x + pad, y, w, 34.0f}, kFilterAccent);
    }

    void drawEnvelopePanel(const Rect b)
    {
        drawPanel(b, "ENVELOPES", kEnvAccent);
        const float pad = 14.0f, gap = 14.0f;
        const float w = (b.w - 2.0f * pad - gap) * 0.5f;
        const char* const names[4] = {"Attack", "Decay", "Sustain", "Release"};
        const std::uint32_t amp[4] = {core::kAmpAttack, core::kAmpDecay, core::kAmpSustain, core::kAmpRelease};
        const std::uint32_t flt[4] = {core::kFltAttack, core::kFltDecay, core::kFltSustain, core::kFltRelease};
        label(b.x + pad, b.y + 44.0f, "Amplitude", false);
        label(b.x + pad + w + gap, b.y + 44.0f, "Filter", false);
        float y = b.y + 66.0f;
        for (int i = 0; i < 4; ++i) {
            slider(amp[i], names[i], {b.x + pad, y, w, 34.0f}, kEnvAccent);
            slider(flt[i], names[i], {b.x + pad + w + gap, y, w, 34.0f}, kEnvAccent);
            y += 56.0f;
        }
    }

    void drawModPanel(const Rect b)
    {
        drawPanel(b, "LFO AND PERFORMANCE", kModAccent);
        const float pad = 14.0f, gap = 12.0f;
        const float w = (b.w - 2.0f * pad - 7.0f * gap) / 8.0f;
        const std::uint32_t ids[8] = {core::kLfoRate, core::kLfoShape, core::kLfoPosition, core::kLfoPitch, core::kLfoCutoff,
                                      core::kMaster, core::kVelocity, core::kBendRange};
        const char* const names[8] = {"LFO rate", "LFO shape", "to Position", "to Pitch", "to Cutoff", "Master", "Velocity", "Bend range"};
        for (int i = 0; i < 8; ++i)
            slider(ids[static_cast<size_t>(i)], names[i], {b.x + pad + static_cast<float>(i) * (w + gap), b.y + 44.0f, w, 34.0f}, kModAccent);
        // Voicing: Poly plays every key as its own voice; Mono and Legato play one voice, last note wins. Glide slides from the previous note.
        segments(core::kPlayMode, "Play mode", kPlayNames.data(), 3, {b.x + pad, b.y + 116.0f, 2.0f * w + gap, 24.0f}, kModAccent);
        slider(core::kGlide, "Glide (portamento from the previous note)", {b.x + pad + 2.0f * (w + gap), b.y + 106.0f, 3.0f * w + 2.0f * gap, 34.0f}, kModAccent);
    }

    [[nodiscard]] std::string format(const std::uint32_t id) const
    {
        char buffer[32];
        const float v = value(id);
        switch (id) {
        case core::kOsc1Transpose:
        case core::kOsc2Transpose:
        case core::kBendRange:
            std::snprintf(buffer, sizeof(buffer), "%+d st", static_cast<int>(std::lround(v)));
            if (id == core::kBendRange) std::snprintf(buffer, sizeof(buffer), "%d st", static_cast<int>(std::lround(v)));
            break;
        case core::kOsc1Fine:
        case core::kOsc2Fine:
            std::snprintf(buffer, sizeof(buffer), "%+.0f ct", static_cast<double>(v));
            break;
        case core::kUnisonVoices:
            std::snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(std::lround(v)));
            break;
        case core::kCutoff:
            if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f kHz", static_cast<double>(v) / 1000.0);
            else std::snprintf(buffer, sizeof(buffer), "%.0f Hz", static_cast<double>(v));
            break;
        case core::kAmpAttack: case core::kAmpDecay: case core::kAmpRelease:
        case core::kFltAttack: case core::kFltDecay: case core::kFltRelease:
            if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v) / 1000.0);
            else std::snprintf(buffer, sizeof(buffer), v < 10.0f ? "%.1f ms" : "%.0f ms", static_cast<double>(v));
            break;
        case core::kGlide:
            if (v < 0.5f) std::snprintf(buffer, sizeof(buffer), "off");
            else if (v >= 1000.0f) std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v) / 1000.0);
            else std::snprintf(buffer, sizeof(buffer), "%.0f ms", static_cast<double>(v));
            break;
        case core::kLfoRate:
            std::snprintf(buffer, sizeof(buffer), "%.2f Hz", static_cast<double>(v));
            break;
        case core::kLfoShape:
            std::snprintf(buffer, sizeof(buffer), "%s", kLfoNames[static_cast<size_t>(std::clamp(static_cast<int>(std::lround(v)), 0, 4))]);
            break;
        case core::kEnvAmount:
        case core::kEnvPosition:
            std::snprintf(buffer, sizeof(buffer), "%+.2f", static_cast<double>(v));
            break;
        default:
            std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
            break;
        }
        return buffer;
    }

    bool demo_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StrataUI)
};

UI* createUI()
{
    return new StrataUI();
}

END_NAMESPACE_DISTRHO
