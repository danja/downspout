#include "downspout/dpf/MagnetoKit.hpp"

#include "syrinx_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::syrinx;

namespace {

// The kit mirrors at most 64 parameters but Syrinx has 228 (ten presets of 21). The panel shows
// one preset at a time, so it works in "slots": the selected preset's 21 parameters, then the
// global ones. Slots are translated to real parameter indices on the way to and from the host.
constexpr std::uint32_t kPresetSlots = core::kParamsPerPreset;                 // 0-20
constexpr std::uint32_t kGlobalBase = core::kParamDistance;                    // first global parameter
constexpr std::uint32_t kSlotDistance = kPresetSlots + (core::kParamDistance - kGlobalBase);
constexpr std::uint32_t kSlotGain = kPresetSlots + (core::kParamMasterGain - kGlobalBase);
constexpr std::uint32_t kSlotPreset = kPresetSlots + (core::kParamSelectedPreset - kGlobalBase);
constexpr std::uint32_t kSlotSource = kPresetSlots + (core::kParamSource - kGlobalBase);
constexpr std::uint32_t kSlotSyllables = kPresetSlots + (core::kParamSyllables - kGlobalBase);
constexpr std::uint32_t kSlotChirp = kPresetSlots + (core::kParamChirp - kGlobalBase);
constexpr std::uint32_t kSlotVariation = kPresetSlots + (core::kParamVariation - kGlobalBase);
constexpr std::uint32_t kSlotGap = kPresetSlots + (core::kParamGap - kGlobalBase);
constexpr std::uint32_t kSlotPitchMult = kPresetSlots + (core::kParamInPitchMult - kGlobalBase);
constexpr std::uint32_t kSlotRange = kPresetSlots + (core::kParamInRange - kGlobalBase);
constexpr std::uint32_t kSlotGate = kPresetSlots + (core::kParamInGate - kGlobalBase);
constexpr std::uint32_t kSlotResponse = kPresetSlots + (core::kParamInResponse - kGlobalBase);
constexpr std::uint32_t kSlotUnvoiced = kPresetSlots + (core::kParamInUnvoiced - kGlobalBase);
constexpr std::uint32_t kSlotMix = kPresetSlots + (core::kParamInMix - kGlobalBase);
constexpr std::uint32_t kSlotOutLevel = kPresetSlots + (core::kParamOutLevel - kGlobalBase);
constexpr std::uint32_t kSlotOutInHz = kPresetSlots + (core::kParamOutInHz - kGlobalBase);
constexpr std::uint32_t kSlotOutBirdHz = kPresetSlots + (core::kParamOutBirdHz - kGlobalBase);
constexpr std::uint32_t kSlotOutVoiced = kPresetSlots + (core::kParamOutVoiced - kGlobalBase);
constexpr std::uint32_t kSlotCount = kPresetSlots + (core::kLegacyParameterCount - kGlobalBase);

static_assert(kSlotCount <= MagnetoUI::kMaxValues, "slot window must fit the kit");

constexpr kit::Accent kSourceAccent {92, 140, 156};   // cold steel
constexpr kit::Accent kTrackAccent {104, 148, 104};   // leaf
constexpr kit::Accent kPhraseAccent {198, 132, 58};   // amber
constexpr kit::Accent kVoiceAccent {150, 112, 150};   // plum
constexpr kit::Accent kMotionAccent {176, 112, 62};   // copper
constexpr kit::Accent kTractAccent {92, 140, 156};    // steel
constexpr kit::Accent kMasterAccent {116, 116, 128};  // slate

std::uint32_t slotToParameter(const std::uint32_t slot, const std::uint32_t preset)
{
    return slot < kPresetSlots ? core::presetParam(preset, slot) : kGlobalBase + (slot - kPresetSlots);
}

// -1 when the parameter belongs to a preset that is not on show.
int parameterToSlot(const std::uint32_t parameter, const std::uint32_t preset)
{
    if (parameter >= kGlobalBase && parameter < core::kLegacyParameterCount)
        return static_cast<int>(kPresetSlots + (parameter - kGlobalBase));
    if (core::presetOfParam(parameter) == static_cast<int>(preset))
        return static_cast<int>(parameter - core::presetParam(preset, 0));
    return -1;
}

struct Tables {
    std::array<kit::Range, kSlotCount> ranges {};
    std::array<float, kSlotCount> defaults {};
    Tables()
    {
        for (std::uint32_t s = 0; s < kSlotCount; ++s) {
            const auto spec = core::getParameterSpec(slotToParameter(s, 0));
            ranges[s] = {spec.minimum, spec.maximum, spec.integer || spec.boolean};
            defaults[s] = spec.defaultValue;
        }
        ranges[kSlotRange].logarithmic = true;
        ranges[kSlotPitchMult].logarithmic = true;
        ranges[core::kPresetParamFormant1].logarithmic = true;
        ranges[core::kPresetParamFormant2].logarithmic = true;
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

std::string pct(const float v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "%.0f%%", v * 100.0f);
    return b;
}

std::string hz(const float v)
{
    char b[24];
    if (v >= 1000.0f)
        std::snprintf(b, sizeof(b), "%.2f kHz", v / 1000.0f);
    else
        std::snprintf(b, sizeof(b), "%.0f Hz", v);
    return b;
}

}  // namespace

class SyrinxUI : public MagnetoUI {
public:
    SyrinxUI()
        : MagnetoUI(tables().ranges.data(), kSlotCount, tables().defaults.data())
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
            host_[i] = core::getParameterSpec(i).defaultValue;
        pitchIn_.fill(0.0f);
        pitchBird_.fill(0.0f);
    }

protected:
    enum Action { kRandomize = 0, kReset = 1 };

    void parameterChanged(const uint32_t index, const float v) override
    {
        if (index >= core::kParameterCount)
            return;
        host_[index] = v;
        if (index == core::kParamSelectedPreset) {
            preset_ = static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(v)), 0, static_cast<int>(core::kPresetCount) - 1));
            showPreset();
        }
        const int slot = parameterToSlot(index, preset_);
        if (slot >= 0)
            setValue(static_cast<std::uint32_t>(slot), v);
        repaint();
    }

    void sendValue(const std::uint32_t slot, const float v) override
    {
        const std::uint32_t parameter = slotToParameter(slot, preset_);
        host_[parameter] = v;
        setParameterValue(parameter, v);
        if (parameter == core::kParamSelectedPreset) {
            preset_ = static_cast<std::uint32_t>(std::clamp(static_cast<int>(std::lround(v)), 0, static_cast<int>(core::kPresetCount) - 1));
            showPreset();
        }
    }

    void onAction(const int id) override
    {
        if (id == kRandomize) {
            randomize();
        } else if (id == kReset) {
            for (std::uint32_t s = 0; s < kPresetSlots; ++s)
                if (s != core::kPresetParamMute)
                    commit(s, core::kPresetDefaults[preset_][s]);
        }
    }

    // About 30 samples a second of the tracked pitch for the history plot.
    void uiIdle() override
    {
        const bool live = intValue(kSlotSource) != 0;
        if (!live)
            return;
        const float inHz = host_[core::kParamOutInHz];
        const float birdHz = host_[core::kParamOutBirdHz];
        head_ = (head_ + 1) % kHistory;
        pitchIn_[head_] = inHz;
        pitchBird_[head_] = birdHz;
        repaint();
    }

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        const float colW = (width - 48.0f - 2.0f * kColGap) / 3.0f;
        const float x1 = 24.0f, x2 = x1 + colW + kColGap, x3 = x2 + colW + kColGap;
        const int source = intValue(kSlotSource);

        beginFrame();
        drawHeader("Syrinx", "avian syrinx voice",
                   "MIDI plays phrases of chirping syllables; audio in is tracked for pitch and loudness "
                   "and steers the bird.");

        drawSourcePanel({x1, 108.0f, colW, 134.0f});
        drawTrackingPanel({x1, 254.0f, colW, 494.0f}, source == 0);
        drawPhrasePanel({x2, 108.0f, colW, 292.0f}, source == 1);
        drawVoicePanel({x2, 412.0f, colW, 336.0f});

        drawMotionPanel({x3, 108.0f, colW, 196.0f});
        drawTractPanel({x3, 316.0f, colW, 196.0f});
        drawMasterPanel({x3, 524.0f, colW, 224.0f});
    }

private:
    static constexpr float kColGap = 14.0f;
    static constexpr std::size_t kHistory = 360;

    // A slider showing the slot's value in a unit of the caller's choosing.
    void slider(const std::uint32_t slot, const char* name, const std::string& shown, const float x, const float y,
                const float w, const kit::Accent accent)
    {
        drawSlider(slot, name, shown.c_str(), {x, y, w, 34.0f}, accent);
    }

    // Two columns of sliders inside a panel; `row` and `col` count from the panel's content area.
    struct Grid {
        float x, y, w;
        float cell(const int col) const { return x + static_cast<float>(col) * (w + 16.0f); }
        float row(const int r) const { return y + static_cast<float>(r) * 46.0f; }
    };
    static Grid grid(const Rect b, const float top = 46.0f)
    {
        return {b.x + 14.0f, b.y + top, (b.w - 28.0f - 16.0f) * 0.5f};
    }

    void showPreset()
    {
        for (std::uint32_t s = 0; s < kPresetSlots; ++s)
            setValue(s, host_[slotToParameter(s, preset_)]);
    }

    void drawSourcePanel(const Rect b)
    {
        drawPanel(b, "SOURCE", kSourceAccent, "what plays the bird");
        static const char* const kNames[] = {"MIDI", "Audio in", "Both"};
        drawSegments(kSlotSource, kNames, 3, {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, 28.0f}, kSourceAccent);
        slider(kSlotMix, "Bird mix  (0 = your voice, 1 = bird)", pct(value(kSlotMix)), b.x + 14.0f, b.y + 86.0f,
               b.w - 28.0f, kSourceAccent);
    }

    // The input and bird pitch over the last few seconds, plus what the tracker currently hears.
    void drawTrackingPanel(const Rect b, const bool inactive)
    {
        drawPanel(b, "AUDIO INPUT", kTrackAccent, inactive ? "inactive: Source is MIDI" : "pitch and loudness drive the bird");
        const auto& t = theme();

        const Rect plot {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, 190.0f};
        drawPlotFrame(plot);
        constexpr float kLo = 70.0f, kHi = 12000.0f;
        const auto py = [&](const float f) {
            return plot.y + plot.h - 8.0f - (std::log(f / kLo) / std::log(kHi / kLo)) * (plot.h - 16.0f);
        };
        static constexpr float kGrid[] = {100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f};
        for (const float g : kGrid) {
            beginPath();
            sc(t.border);
            strokeWidth(1.0f);
            moveTo(plot.x + 1.0f, py(g));
            lineTo(plot.x + plot.w - 1.0f, py(g));
            stroke();
            char axis[16];
            std::snprintf(axis, sizeof(axis), g >= 1000.0f ? "%.0f kHz" : "%.0f Hz", g >= 1000.0f ? g / 1000.0f : g);
            label(plot.x + 5.0f, py(g) - 12.0f, axis, true, 10.0f);
        }
        drawTrace(pitchIn_, plot, py, {170, 170, 176});
        drawTrace(pitchBird_, plot, py, kTrackAccent);
        label(plot.x + plot.w - 6.0f, plot.y + 5.0f, "input", true, 10.0f, ALIGN_RIGHT | ALIGN_TOP);
        fillColor(kTrackAccent.r, kTrackAccent.g, kTrackAccent.b, 255);
        fontSize(10.0f);
        textAlign(ALIGN_RIGHT | ALIGN_TOP);
        text(plot.x + plot.w - 6.0f, plot.y + 18.0f, "bird", nullptr);

        // Live readout
        const float inHz = host_[core::kParamOutInHz];
        const float birdHz = host_[core::kParamOutBirdHz];
        const float state = host_[core::kParamOutVoiced];
        const float level = host_[core::kParamOutLevel];
        const float ry = plot.y + plot.h + 14.0f;
        label(b.x + 14.0f, ry, "hearing");
        label(b.x + 14.0f, ry + 17.0f, state >= 0.75f ? "voiced" : (state > 0.25f ? "hiss / breath" : "silence"), false, 14.0f);
        label(b.x + 150.0f, ry, "input pitch");
        label(b.x + 150.0f, ry + 17.0f, inHz > 0.0f ? hz(inHz).c_str() : "-", false, 14.0f);
        label(b.x + 270.0f, ry, "bird pitch");
        label(b.x + 270.0f, ry + 17.0f, birdHz > 0.0f ? hz(birdHz).c_str() : "-", false, 14.0f);

        // Level bar
        const Rect bar {b.x + 14.0f, ry + 46.0f, b.w - 28.0f, 8.0f};
        beginPath();
        fc(t.controlTrack);
        roundedRect(bar.x, bar.y, bar.w, bar.h, 3.0f);
        fill();
        if (level > 0.01f) {
            beginPath();
            fillColor(kTrackAccent.r, kTrackAccent.g, kTrackAccent.b, 255);
            roundedRect(bar.x, bar.y, bar.w * std::clamp(level, 0.0f, 1.0f), bar.h, 3.0f);
            fill();
        }

        const Grid g = grid(b, 0.0f);
        const float top = ry + 66.0f;
        const float w = g.w;
        char buf[24];
        std::snprintf(buf, sizeof(buf), "x%.1f", value(kSlotPitchMult));
        slider(kSlotPitchMult, "Pitch multiplier", buf, g.cell(0), top, w, kTrackAccent);
        slider(kSlotRange, "Voice range", hz(value(kSlotRange)), g.cell(1), top, w, kTrackAccent);
        slider(kSlotGate, "Gate", pct(value(kSlotGate)), g.cell(0), top + 46.0f, w, kTrackAccent);
        std::snprintf(buf, sizeof(buf), "%.0f ms", 2.0f * std::pow(60.0f, value(kSlotResponse)));
        slider(kSlotResponse, "Pitch glide", buf, g.cell(1), top + 46.0f, w, kTrackAccent);
        slider(kSlotUnvoiced, "Hiss to turbulence", pct(value(kSlotUnvoiced)), g.cell(0), top + 92.0f, w, kTrackAccent);
        label(g.cell(1), top + 96.0f, "Voice range: set to the middle of your", true, 10.0f);
        label(g.cell(1), top + 109.0f, "speaking pitch; the tracker covers 0.5-2.5x.", true, 10.0f);
    }

    void drawTrace(const std::array<float, kHistory>& data, const Rect plot, const std::function<float(float)>& py,
                   const kit::Accent colour)
    {
        beginPath();
        strokeColor(colour.r, colour.g, colour.b, 255);
        strokeWidth(1.6f);
        bool pen = false;
        for (std::size_t i = 0; i < kHistory; ++i) {
            const float f = data[(head_ + 1 + i) % kHistory];
            const float x = plot.x + 2.0f + (plot.w - 4.0f) * static_cast<float>(i) / static_cast<float>(kHistory - 1);
            if (f < 70.0f) {
                pen = false;
                continue;
            }
            const float y = py(std::min(f, 12000.0f));
            if (pen) lineTo(x, y); else moveTo(x, y);
            pen = true;
        }
        stroke();
    }

    // A schematic of the phrase the sliders describe: syllable length, gaps and sweep, drawn from
    // the same formulas the engine uses (mean values; the engine adds Variation on top).
    void drawPhrasePanel(const Rect b, const bool audioOnly)
    {
        drawPanel(b, "PHRASE", kPhraseAccent, audioOnly ? "audio mode: Chirp sets the onset kick" : "one note-on = a run of chirps");
        const auto& t = theme();
        const float chirp = value(kSlotChirp);
        const float gapAmt = value(kSlotGap);
        const int n = intValue(kSlotSyllables);
        const float dur = 0.05f + value(core::kPresetParamDuration) * 1.95f;
        const float bend = value(core::kPresetParamBend);

        const float len = std::max(0.025f, dur * std::pow(0.12f, chirp));
        const float gap = len * (0.15f + 1.6f * gapAmt);
        const float total = static_cast<float>(n) * len + static_cast<float>(n - 1) * gap;
        const float sweep = chirp * 2.2f;

        const Rect plot {b.x + 14.0f, b.y + 44.0f, b.w - 28.0f, 126.0f};
        drawPlotFrame(plot);
        const float mid = plot.y + plot.h * 0.5f;
        const float octPx = (plot.h - 24.0f) / 3.2f;   // pixels per octave
        beginPath();
        sc(t.border);
        strokeWidth(1.0f);
        moveTo(plot.x + 1.0f, mid);
        lineTo(plot.x + plot.w - 1.0f, mid);
        stroke();
        const float pxPerSec = (plot.w - 20.0f) / std::max(total, 0.05f);
        for (int k = 0; k < n; ++k) {
            const float t0 = static_cast<float>(k) * (len + gap);
            const int kind = k % 3;   // ramp the way Bend leans, the opposite ramp, then an arch
            const float dir = (bend >= 0.0f) == (kind != 1) ? 1.0f : -1.0f;
            beginPath();
            strokeColor(kPhraseAccent.r, kPhraseAccent.g, kPhraseAccent.b, 255);
            strokeWidth(2.4f);
            constexpr int kSteps = 24;
            for (int i = 0; i <= kSteps; ++i) {
                const float u = static_cast<float>(i) / kSteps;
                const float shape = kind == 2 ? std::sin(3.14159265f * u) - 0.5f : u - 0.5f;
                const float x = plot.x + 10.0f + (t0 + u * len) * pxPerSec;
                const float y = mid - dir * sweep * shape * octPx;
                if (i == 0) moveTo(x, y); else lineTo(x, y);
            }
            stroke();
        }
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%.0f ms syllables, %.0f ms apart, %.1f oct sweep, %.2f s phrase", len * 1000.0f,
                      gap * 1000.0f, sweep, total);
        label(b.x + 14.0f, plot.y + plot.h + 8.0f, buf, true, 11.0f);

        const Grid g = grid(b, 44.0f + plot.h + 32.0f);
        std::snprintf(buf, sizeof(buf), "%d", n);
        slider(kSlotSyllables, "Syllables per note", buf, g.cell(0), g.row(0), g.w, kPhraseAccent);
        slider(kSlotChirp, "Chirp  (speed, sweep depth)", pct(chirp), g.cell(1), g.row(0), g.w, kPhraseAccent);
        slider(kSlotVariation, "Variation", pct(value(kSlotVariation)), g.cell(0), g.row(1), g.w, kPhraseAccent);
        slider(kSlotGap, "Gap", pct(gapAmt), g.cell(1), g.row(1), g.w, kPhraseAccent);
    }

    void drawVoicePanel(const Rect b)
    {
        const auto& color = kPresetColors[preset_];
        drawPanel(b, "VOICE", kVoiceAccent, "the selected bird preset");
        const float w = b.w - 28.0f;
        drawStepper(kSlotPreset, "Preset", core::kPresetNames[preset_], {b.x + 14.0f, b.y + 44.0f, w * 0.5f, 22.0f});
        static const char* const kMute[] = {"Sounding", "Muted"};
        label(b.x + 14.0f + w * 0.5f + 16.0f, b.y + 44.0f, "Mute");
        drawSegments(core::kPresetParamMute, kMute, 2,
                     {b.x + 14.0f + w * 0.5f + 16.0f, b.y + 59.0f, w * 0.5f - 16.0f, 22.0f}, color);

        const Grid g = grid(b, 96.0f);
        char buf[24];
        slider(core::kPresetParamLevel, "Level", pct(value(core::kPresetParamLevel) / 1.4f), g.cell(0), g.row(0), g.w, color);
        slider(core::kPresetParamTimbre, "Timbre", pct(value(core::kPresetParamTimbre)), g.cell(1), g.row(0), g.w, color);
        slider(core::kPresetParamNoise, "Noise", pct(value(core::kPresetParamNoise)), g.cell(0), g.row(1), g.w, color);
        slider(core::kPresetParamRoughness, "Roughness", pct(value(core::kPresetParamRoughness)), g.cell(1), g.row(1), g.w, color);
        slider(core::kPresetParamRegime, "Regime  (tonal to pulse)", pct(value(core::kPresetParamRegime)), g.cell(0), g.row(2), g.w, color);
        slider(core::kPresetParamHarmonic, "Harmonic", pct(value(core::kPresetParamHarmonic)), g.cell(1), g.row(2), g.w, color);
        std::snprintf(buf, sizeof(buf), "%+.1f st", value(core::kPresetParamPitch) * 12.0f);
        slider(core::kPresetParamPitch, "Pitch offset", buf, g.cell(0), g.row(3), g.w, color);
        std::snprintf(buf, sizeof(buf), "%.2f s", 0.05f + value(core::kPresetParamDuration) * 1.95f);
        slider(core::kPresetParamDuration, "Duration", buf, g.cell(1), g.row(3), g.w, color);
    }

    void drawMotionPanel(const Rect b)
    {
        drawPanel(b, "MOTION", kMotionAccent, "vibrato, bend, tremolo, breath");
        const Grid g = grid(b);
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%.1f Hz", value(core::kPresetParamVibRate) * 20.0f);
        slider(core::kPresetParamVibRate, "Vibrato rate", buf, g.cell(0), g.row(0), g.w, kMotionAccent);
        std::snprintf(buf, sizeof(buf), "%.0f ct", value(core::kPresetParamVibDepth) * 300.0f);
        slider(core::kPresetParamVibDepth, "Vibrato depth", buf, g.cell(1), g.row(0), g.w, kMotionAccent);
        std::snprintf(buf, sizeof(buf), "%+.2f", value(core::kPresetParamBend));
        slider(core::kPresetParamBend, "Bend  (phrase lean)", buf, g.cell(0), g.row(1), g.w, kMotionAccent);
        slider(core::kPresetParamRespiration, "Respiration", pct(value(core::kPresetParamRespiration)), g.cell(1), g.row(1), g.w, kMotionAccent);
        std::snprintf(buf, sizeof(buf), "%.1f Hz", value(core::kPresetParamAMRate) * 30.0f);
        slider(core::kPresetParamAMRate, "Tremolo rate", buf, g.cell(0), g.row(2), g.w, kMotionAccent);
        slider(core::kPresetParamAMDepth, "Tremolo depth", pct(value(core::kPresetParamAMDepth)), g.cell(1), g.row(2), g.w, kMotionAccent);
    }

    void drawTractPanel(const Rect b)
    {
        drawPanel(b, "TRACT", kTractAccent, "trachea resonances, second voice");
        const Grid g = grid(b);
        char buf[24];
        slider(core::kPresetParamFormant1, "Formant 1", hz(value(core::kPresetParamFormant1)), g.cell(0), g.row(0), g.w, kTractAccent);
        slider(core::kPresetParamFormant2, "Formant 2", hz(value(core::kPresetParamFormant2)), g.cell(1), g.row(0), g.w, kTractAccent);
        std::snprintf(buf, sizeof(buf), "%.1f", value(core::kPresetParamFormantQ));
        slider(core::kPresetParamFormantQ, "Formant Q", buf, g.cell(0), g.row(1), g.w, kTractAccent);
        slider(core::kPresetParamCoupling, "Coupling", pct(value(core::kPresetParamCoupling)), g.cell(1), g.row(1), g.w, kTractAccent);
        slider(core::kPresetParamVoiceOffset, "Voice offset", pct(value(core::kPresetParamVoiceOffset)), g.cell(0), g.row(2), g.w, kTractAccent);
        std::snprintf(buf, sizeof(buf), "%.1f cm", value(core::kPresetParamTracheaCm));
        slider(core::kPresetParamTracheaCm, "Trachea length", buf, g.cell(1), g.row(2), g.w, kTractAccent);
    }

    void drawMasterPanel(const Rect b)
    {
        drawPanel(b, "MASTER", kMasterAccent);
        const Grid g = grid(b);
        slider(kSlotDistance, "Distance", pct(value(kSlotDistance)), g.cell(0), g.row(0), g.w, kMasterAccent);
        slider(kSlotGain, "Master gain", pct(value(kSlotGain)), g.cell(1), g.row(0), g.w, kMasterAccent);
        drawButton(kRandomize, "Randomize preset", {g.cell(0), g.row(1) + 6.0f, g.w, 30.0f}, kMasterAccent);
        drawButton(kReset, "Reset preset", {g.cell(1), g.row(1) + 6.0f, g.w, 30.0f}, kMasterAccent, false);
        label(b.x + 14.0f, g.row(2) + 10.0f, "CC: 1 vibrato  11 level  71 rough  74 timbre  77 noise", true, 10.0f);
        label(b.x + 14.0f, g.row(2) + 24.0f, "81 syllables  82 chirp  84 variation  86 gap  87 mix  91 distance", true, 10.0f);
        label(b.x + 14.0f, g.row(2) + 38.0f, "Presets are chosen with the Selected Preset parameter.", true, 10.0f);
    }

    void randomize()
    {
        std::srand(static_cast<unsigned>(std::lround(host_[core::kParamOutBirdHz])) * 2654435761u
                   + static_cast<unsigned>(rand_++));
        const auto r = [](const float lo, const float hi) {
            return lo + (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * (hi - lo);
        };
        commit(core::kPresetParamLevel, r(0.5f, 1.2f));
        commit(core::kPresetParamNoise, r(0.0f, 0.4f));
        commit(core::kPresetParamRoughness, r(0.0f, 0.3f));
        commit(core::kPresetParamTimbre, r(0.0f, 0.8f));
        commit(core::kPresetParamVibRate, r(0.1f, 0.8f));
        commit(core::kPresetParamVibDepth, r(0.0f, 0.4f));
        commit(core::kPresetParamBend, r(-0.7f, 0.7f));
        commit(core::kPresetParamHarmonic, r(0.0f, 1.0f));
        commit(core::kPresetParamAMRate, r(0.0f, 0.3f));
        commit(core::kPresetParamPitch, r(-0.25f, 0.25f));
        commit(core::kPresetParamDuration, r(0.04f, 0.55f));
        commit(core::kPresetParamRespiration, r(0.0f, 0.3f));
        commit(core::kPresetParamAMDepth, r(0.0f, 0.8f));
        commit(core::kPresetParamFormant1, r(400.0f, 4000.0f));
        commit(core::kPresetParamFormant2, r(800.0f, 7000.0f));
        commit(core::kPresetParamFormantQ, r(2.0f, 12.0f));
        commit(core::kPresetParamCoupling, r(0.0f, 0.4f));
        commit(core::kPresetParamVoiceOffset, r(0.0f, 0.5f));
    }

    static constexpr std::array<kit::Accent, core::kPresetCount> kPresetColors = {{
        {165, 200, 110}, {120, 170, 210}, {215, 185, 90}, {205, 115, 75}, {150, 200, 135},
        {120, 120, 160}, {175, 150, 195}, {100, 200, 175}, {205, 165, 90}, {155, 155, 155}, {96, 98, 120},
    }};

    std::array<float, core::kParameterCount> host_ {};
    std::uint32_t preset_ = 0;
    std::array<float, kHistory> pitchIn_ {}, pitchBird_ {};
    std::size_t head_ = 0;
    unsigned rand_ = 1;
};

UI* createUI() { return new SyrinxUI(); }

END_NAMESPACE_DISTRHO
