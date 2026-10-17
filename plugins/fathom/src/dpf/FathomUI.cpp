#include "downspout/dpf/MagnetoKit.hpp"

#include "fathom_generator.hpp"
#include "fathom_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace core = downspout::fathom;

namespace {

constexpr kit::Accent kGeneratorAccent {198, 132, 58};  // amber (magneto DRIVE)
constexpr kit::Accent kFilterAccent {92, 140, 156};     // cold steel (magneto EXHAUST)
constexpr kit::Accent kProcessorAccent {176, 88, 62};   // hot metal (magneto ENGINE)
constexpr kit::Accent kOutputAccent {124, 148, 104};    // olive (magneto LISTEN)

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i)
        {
            const auto& spec = core::kParameterSpecs[i];
            ranges[i] = {spec.minimum, spec.maximum, spec.integer};
            defaults[i] = spec.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

constexpr std::uint32_t idx(const core::ParamId id)
{
    return static_cast<std::uint32_t>(id);
}

// What each shared generator control does in each algorithm. Order: warp, span,
// morph, seed, fuse. The same five sliders mean different things per algorithm,
// so the panel says which.
constexpr std::array<std::array<const char*, 5>, core::kAlgorithmCount> kControlMeanings = {{
    {{"FM of saw by pulse", "pulse detune", "saw/pulse to sine", "unused", "pulse width"}},
    {{"phase modulation", "modulator ratio", "to ring mod", "unused", "wavefold"}},
    {{"bit crush", "partial drift", "spectral tilt", "drift pattern", "saturation"}},
    {{"neighbour phase mod", "tuning fan", "wavetable scan", "frame offsets", "wavefold"}},
    {{"ring modulation", "interval width", "scan window", "which intervals", "sine to saw"}},
}};

constexpr std::array<const char*, core::kAlgorithmCount> kAlgorithmDescriptions = {{
    "Analog: a saw and a pulse; the pulse frequency-modulates the saw.",
    "Shaper: a sine carrier phase-modulated, ring-modulated and folded by a second sine.",
    "Sines: eight sines, near-harmonic, one a hair off the fundamental so the drone beats.",
    "Table: four wavetable oscillators scanning eight frames, each offset by the seed.",
    "Cluster: four oscillators on diatonic steps around the note; Morph scans across them.",
}};

constexpr std::array<const char*, 2> kProcessorDescriptions = {{
    "Metallic: four delay lines through a mixing matrix; the shifter sits in the loop.",
    "Diffuse: a stereo ping-pong delay smeared by allpass diffusers; the shifter sits in the loop.",
}};

std::string percent(const float v)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 100.0f)));
    return buffer;
}

std::string milliseconds(const float v)
{
    char buffer[24];
    if (v < 1000.0f)
        std::snprintf(buffer, sizeof(buffer), "%d ms", static_cast<int>(std::lround(v)));
    else
        std::snprintf(buffer, sizeof(buffer), "%.2f s", static_cast<double>(v) / 1000.0);
    return buffer;
}

std::string formatValue(const core::ParamId id, const float v)
{
    char buffer[32];
    switch (id)
    {
    case core::ParamId::field:
    {
        const double hz = core::fieldToHertz(v);
        if (hz < 1000.0)
            std::snprintf(buffer, sizeof(buffer), "%d Hz", static_cast<int>(std::lround(hz)));
        else
            std::snprintf(buffer, sizeof(buffer), "%.1f kHz", hz / 1000.0);
        return buffer;
    }
    case core::ParamId::time:
        return milliseconds(static_cast<float>(core::timeToSeconds(v) * 1000.0));
    case core::ParamId::shift:
    {
        const double hz = core::shiftToHertz(v);
        if (std::fabs(hz) < 0.05)
            return "off";
        std::snprintf(buffer, sizeof(buffer), "%+.1f Hz", hz);
        return buffer;
    }
    case core::ParamId::basis:
        std::snprintf(buffer, sizeof(buffer), "%+d st", static_cast<int>(std::lround(v)));
        return buffer;
    case core::ParamId::glide:
        return v < 0.5f ? "off" : milliseconds(v);
    case core::ParamId::attack:
    case core::ParamId::release:
        return milliseconds(v);
    default:
        return percent(v);
    }
}

}  // namespace

class FathomUI : public MagnetoUI {
public:
    FathomUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(int) override {}

    void onNanoDisplay() override
    {
        const float width = static_cast<float>(getWidth());
        beginFrame();
        drawHeader("Fathom", "drone synthesizer",
                   "clustered oscillators into a notch filter, then a delay network with a frequency shifter in its loop.");

        const float gap = 16.0f;
        const float leftW = 590.0f;
        const float rightX = 24.0f + leftW + gap;
        const float rightW = width - 24.0f - rightX;

        drawGeneratorPanel({24.0f, 108.0f, leftW, 272.0f});
        drawProcessorPanel({24.0f, 396.0f, leftW, 288.0f});
        drawFilterPanel({rightX, 108.0f, rightW, 140.0f});
        drawOutputPanel({rightX, 264.0f, rightW, 420.0f});
    }

private:
    void slider(const core::ParamId id, const std::string& name, const Rect b, const Accent accent)
    {
        drawSlider(idx(id), name.c_str(), formatValue(id, value(idx(id))).c_str(), b, accent);
    }

    static std::string named(const char* name, const char* meaning, const int cc)
    {
        char buffer[96];
        std::snprintf(buffer, sizeof(buffer), "%s  (CC %d)  %s", name, cc, meaning);
        return buffer;
    }

    void drawGeneratorPanel(const Rect b)
    {
        drawPanel(b, "GENERATOR", kGeneratorAccent, "CC 1-4 are the numbers Drift's lanes emit");

        const int algorithm = std::clamp(intValue(idx(core::ParamId::algorithm)), 0, core::kAlgorithmCount - 1);
        drawSegments(idx(core::ParamId::algorithm), core::kAlgorithmNames.data(), core::kAlgorithmCount,
                     {b.x + 14.0f, b.y + 42.0f, b.w - 28.0f, 28.0f}, kGeneratorAccent);

        const float colW = (b.w - 28.0f - 24.0f) / 2.0f;
        const float colA = b.x + 14.0f;
        const float colB = colA + colW + 24.0f;
        const float top = b.y + 80.0f;
        const float rowH = 46.0f;
        const auto& meaning = kControlMeanings[static_cast<std::size_t>(algorithm)];

        slider(core::ParamId::warp, named("Warp", meaning[0], 1), {colA, top, colW, rowH}, kGeneratorAccent);
        slider(core::ParamId::span, named("Span", meaning[1], 2), {colA, top + rowH, colW, rowH}, kGeneratorAccent);
        slider(core::ParamId::morph, named("Morph", meaning[2], 3), {colA, top + 2.0f * rowH, colW, rowH}, kGeneratorAccent);
        slider(core::ParamId::seed, named("Seed", meaning[3], 4), {colB, top, colW, rowH}, kGeneratorAccent);
        slider(core::ParamId::fuse, named("Fuse", meaning[4], 5), {colB, top + rowH, colW, rowH}, kGeneratorAccent);

        drawScope({colB, top + 2.0f * rowH + 4.0f, colW, rowH - 8.0f});

        label(colA, top + 3.0f * rowH + 8.0f, kAlgorithmDescriptions[static_cast<std::size_t>(algorithm)], true, 10.5f);
        label(colA, top + 3.0f * rowH + 24.0f,
              "The same five sliders do something different in each algorithm; their names show what.", true, 10.5f);
    }

    // The waveform the current settings make at a fixed pitch, from the same
    // generator class the synth runs.
    void drawScope(const Rect plot)
    {
        drawPlotFrame(plot);

        core::GeneratorControls controls;
        controls.algorithm = std::clamp(intValue(idx(core::ParamId::algorithm)), 0, core::kAlgorithmCount - 1);
        controls.warp = value(idx(core::ParamId::warp));
        controls.span = value(idx(core::ParamId::span));
        controls.morph = value(idx(core::ParamId::morph));
        controls.seed = value(idx(core::ParamId::seed));
        controls.fuse = value(idx(core::ParamId::fuse));

        core::Generator generator;
        generator.setSampleRate(48000.0);
        generator.setControls(controls);
        generator.snap();
        for (int i = 0; i < 480; ++i)  // let the phases settle
            (void)generator.process(110.0);

        constexpr int kSamples = 872;  // two periods of 110 Hz
        std::array<float, kSamples> wave {};
        float peak = 0.05f;
        for (float& s : wave)
        {
            s = generator.process(110.0);
            peak = std::max(peak, std::fabs(s));
        }

        beginPath();
        strokeColor(kGeneratorAccent.r, kGeneratorAccent.g, kGeneratorAccent.b, 255);
        strokeWidth(1.2f);
        for (int i = 0; i < kSamples; ++i)
        {
            const float x = plot.x + 4.0f + (plot.w - 8.0f) * static_cast<float>(i) / (kSamples - 1);
            const float y = plot.y + plot.h * 0.5f - (plot.h * 0.5f - 4.0f) * wave[static_cast<std::size_t>(i)] / peak;
            if (i == 0)
                moveTo(x, y);
            else
                lineTo(x, y);
        }
        stroke();
    }

    void drawFilterPanel(const Rect b)
    {
        drawPanel(b, "FILTER", kFilterAccent, "per voice, before the envelope");
        const float x = b.x + 14.0f;
        const float w = b.w - 28.0f;
        slider(core::ParamId::field, "Field  (CC 6)  low-pass centre", {x, b.y + 44.0f, w, 42.0f}, kFilterAccent);
        slider(core::ParamId::notch, "Notch  (CC 8)  carves two notches", {x, b.y + 90.0f, w, 42.0f}, kFilterAccent);
    }

    void drawProcessorPanel(const Rect b)
    {
        drawPanel(b, "PROCESSOR", kProcessorAccent, "delay network with a shifter in the feedback loop");

        const int kind = std::clamp(intValue(idx(core::ParamId::processor)), 0, 1);
        drawSegments(idx(core::ParamId::processor), core::kProcessorNames.data(), 2,
                     {b.x + 14.0f, b.y + 42.0f, b.w - 28.0f, 28.0f}, kProcessorAccent);

        const float colW = (b.w - 28.0f - 24.0f) / 2.0f;
        const float colA = b.x + 14.0f;
        const float colB = colA + colW + 24.0f;
        const float top = b.y + 80.0f;
        const float rowH = 48.0f;

        slider(core::ParamId::time, "Time  (CC 10)  delay length", {colA, top, colW, rowH}, kProcessorAccent);
        slider(core::ParamId::form, "Form  (CC 11)  echoes to wash", {colA, top + rowH, colW, rowH}, kProcessorAccent);
        slider(core::ParamId::feed, "Feed  (CC 12)  how long it rings", {colA, top + 2.0f * rowH, colW, rowH}, kProcessorAccent);
        slider(core::ParamId::shift, "Shift  (CC 13)  pitch step per repeat", {colB, top, colW, rowH}, kProcessorAccent);
        slider(core::ParamId::mix, "Mix  (CC 9)  dry to wet", {colB, top + rowH, colW, rowH}, kProcessorAccent);

        label(colA, top + 3.0f * rowH + 8.0f, kProcessorDescriptions[static_cast<std::size_t>(kind)], true, 10.5f);
        label(colA, top + 3.0f * rowH + 24.0f,
              "Shift moves every repeat up or down in pitch, so the tail spirals. Centre is off.", true, 10.5f);
    }

    void drawOutputPanel(const Rect b)
    {
        drawPanel(b, "VOICES  /  OUTPUT", kOutputAccent);

        const float x = b.x + 14.0f;
        const float w = b.w - 28.0f;
        float y = b.y + 42.0f;
        const float step = 44.0f;
        slider(core::ParamId::basis, "Basis  (CC 14)  transpose", {x, y, w, 40.0f}, kOutputAccent);
        y += step;
        slider(core::ParamId::glide, "Glide  between notes", {x, y, w, 40.0f}, kOutputAccent);
        y += step;
        slider(core::ParamId::attack, "Attack", {x, y, w, 40.0f}, kOutputAccent);
        y += step;
        slider(core::ParamId::release, "Release", {x, y, w, 40.0f}, kOutputAccent);
        y += step;
        slider(core::ParamId::level, "Level  (CC 7)", {x, y, w, 40.0f}, kOutputAccent);
        y += step + 4.0f;

        drawStepper(idx(core::ParamId::hold), "Hold  (sustain pedal, CC 64)",
                    intValue(idx(core::ParamId::hold)) != 0 ? "on: notes keep sounding" : "off", {x, y, w, 40.0f});
        y += step;
        const int channel = std::clamp(intValue(idx(core::ParamId::midiCh)), 0, 17);
        drawStepper(idx(core::ParamId::midiCh), "MIDI in channel",
                    core::kMidiChannelNames[static_cast<std::size_t>(channel)], {x, y, w, 40.0f});
        y += step + 4.0f;

        char voices[48];
        std::snprintf(voices, sizeof(voices), "Voices sounding: %d of %d", intValue(idx(core::ParamId::outVoices)),
                      static_cast<int>(core::kVoiceCount));
        label(x, y, voices, false, 12.0f);
        label(x, y + 18.0f, "Four voices, panned apart. Velocity shades", true, 10.5f);
        label(x, y + 32.0f, "the level a little; pitch bend is +-2 semitones.", true, 10.5f);
    }
};

UI* createUI()
{
    return new FathomUI();
}

END_NAMESPACE_DISTRHO
