#include "downspout/dpf/MagnetoKit.hpp"

#include "polyscribe_params.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::polyscribe;

constexpr kit::Accent kInputAccent {198, 132, 58};     // amber: what we listen for
constexpr kit::Accent kShapeAccent {120, 140, 92};     // olive: how notes are shaped
constexpr kit::Accent kRouteAccent {92, 140, 156};     // steel: where messages go
constexpr kit::Accent kStatusAccent {198, 120, 64};    // copper: live detection

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};

    Tables()
    {
        for (std::size_t i = 0; i < core::kParameterSpecs.size(); ++i) {
            const core::ParamSpec& spec = core::kParameterSpecs[i];
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

[[nodiscard]] std::string noteName(const int note)
{
    if (note < 0 || note > 127)
        return "—";
    constexpr const char* kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%s%d", kNames[note % 12], note / 12 - 1);
    return buffer;
}

}  // namespace

class PolyscribeUI : public MagnetoUI {
public:
    PolyscribeUI()
        : MagnetoUI(tables().ranges.data(), static_cast<std::uint32_t>(core::kParameterCount),
                    tables().defaults.data())
    {
    }

protected:
    void onAction(const int /*id*/) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("POLYSCRIBE", "Polyphonic audio to MIDI",
                   "Turn audio into notes. Choose the input type, trim the detected notes, then route the MIDI out.");

        const float full = static_cast<float>(getWidth()) - 48.0f;
        drawInputPanel({24.0f, 108.0f, full, 154.0f});
        drawShapePanel({24.0f, 276.0f, full, 214.0f});
        drawRoutePanel({24.0f, 504.0f, 420.0f, 192.0f});
        drawStatusPanel({464.0f, 504.0f, full - 420.0f, 192.0f});
    }

private:
    void drawInputPanel(const Rect b)
    {
        drawPanel(b, "INPUT LISTENING", kInputAccent, "What should be transcribed?");
        label(b.x + 16.0f, b.y + 38.0f, "Input Type", false, 12.0f);
        drawSegments(static_cast<std::uint32_t>(core::ParamId::inputType), core::kInputTypeNames.data(),
                     core::kInputTypeCount, {b.x + 16.0f, b.y + 56.0f, 516.0f, 30.0f}, kInputAccent);

        drawSlider(static_cast<std::uint32_t>(core::ParamId::sensitivity), "Sensitivity",
                   format(core::ParamId::sensitivity).c_str(), {b.x + 560.0f, b.y + 36.0f, b.w - 576.0f, 34.0f},
                   kInputAccent);

        drawStepper(static_cast<std::uint32_t>(core::ParamId::minNote), "Low Note",
                    format(core::ParamId::minNote).c_str(), {b.x + 16.0f, b.y + 104.0f, 168.0f, 38.0f});
        drawStepper(static_cast<std::uint32_t>(core::ParamId::maxNote), "High Note",
                    format(core::ParamId::maxNote).c_str(), {b.x + 198.0f, b.y + 104.0f, 168.0f, 38.0f});
        label(b.x + 392.0f, b.y + 108.0f, "Low / High narrow the pitched search band. Drums uses its own three bands.",
              true, 11.0f);
        label(b.x + 392.0f, b.y + 128.0f, "Detection: 4096-sample Hann window, 512-sample hop (stereo is summed to mono).",
              true, 11.0f);
    }

    void drawShapePanel(const Rect b)
    {
        drawPanel(b, "NOTE SHAPING", kShapeAccent, "Melody and Bass stay monophonic; Chords uses Max Notes");
        const float col = 200.0f;
        const float gap = 12.0f;
        const float x0 = b.x + 16.0f;

        drawStepper(static_cast<std::uint32_t>(core::ParamId::maxNotes), "Max Notes",
                    format(core::ParamId::maxNotes).c_str(), {x0, b.y + 42.0f, col, 38.0f});
        drawSlider(static_cast<std::uint32_t>(core::ParamId::gate), "Gate", format(core::ParamId::gate).c_str(),
                   {x0 + (col + gap), b.y + 40.0f, col, 34.0f}, kShapeAccent);
        drawSlider(static_cast<std::uint32_t>(core::ParamId::release), "Release",
                   format(core::ParamId::release).c_str(), {x0 + 2.0f * (col + gap), b.y + 40.0f, col, 34.0f},
                   kShapeAccent);
        drawSlider(static_cast<std::uint32_t>(core::ParamId::velocity), "Velocity",
                   format(core::ParamId::velocity).c_str(), {x0 + 3.0f * (col + gap), b.y + 40.0f, col, 34.0f},
                   kShapeAccent);

        drawSlider(static_cast<std::uint32_t>(core::ParamId::dynamics), "Dynamics",
                   format(core::ParamId::dynamics).c_str(), {x0, b.y + 118.0f, col, 34.0f}, kShapeAccent);
        drawStepper(static_cast<std::uint32_t>(core::ParamId::transpose), "Transpose",
                    format(core::ParamId::transpose).c_str(), {x0 + (col + gap), b.y + 120.0f, col, 38.0f});
        label(x0 + 2.0f * (col + gap), b.y + 122.0f, "Drums output on channel 10 by convention:", true, 11.0f);
        label(x0 + 2.0f * (col + gap), b.y + 144.0f, "36 Kick  ·  38 Snare  ·  42 Closed Hat", false, 12.0f);
        label(x0 + 2.0f * (col + gap), b.y + 170.0f,
              "Dynamics maps louder input to higher velocity; Gate holds a note after silence.", true, 11.0f);
    }

    void drawRoutePanel(const Rect b)
    {
        drawPanel(b, "ROUTING", kRouteAccent, "Where the notes go");
        drawStepper(static_cast<std::uint32_t>(core::ParamId::channel), "MIDI Channel",
                    format(core::ParamId::channel).c_str(), {b.x + 16.0f, b.y + 44.0f, 180.0f, 38.0f});
        label(b.x + 220.0f, b.y + 42.0f, "MIDI Out", false, 12.0f);
        drawSegments(static_cast<std::uint32_t>(core::ParamId::midiOut), core::kOnOffNames.data(), 2,
                     {b.x + 220.0f, b.y + 60.0f, 168.0f, 28.0f}, kRouteAccent);

        label(b.x + 16.0f, b.y + 108.0f, "Audio Thru", false, 12.0f);
        drawSegments(static_cast<std::uint32_t>(core::ParamId::audioThru), core::kOnOffNames.data(), 2,
                     {b.x + 16.0f, b.y + 126.0f, 180.0f, 28.0f}, kRouteAccent);
        label(b.x + 220.0f, b.y + 110.0f, "Thru copies the input to the outputs.", true, 11.0f);
        label(b.x + 220.0f, b.y + 130.0f, "GM drum kits normally listen on channel 10; set it above.", true, 11.0f);
        label(b.x + 16.0f, b.y + 170.0f, "No plugin state: every setting is a host automation parameter.", true, 11.0f);
    }

    void drawStatusPanel(const Rect b)
    {
        drawPanel(b, "LIVE STATUS", kStatusAccent, "Reported to the host");
        const auto& t = theme();
        const int note = intValue(static_cast<std::uint32_t>(core::ParamId::outLastNote));
        const std::string noteText = note > 0 ? noteName(note) : "—";
        const float flash = value(static_cast<std::uint32_t>(core::ParamId::outFlash));
        const float x = b.x + 16.0f;

        label(x, b.y + 42.0f, "LAST NOTE", true, 11.0f);
        fc(flash > 0.05f ? t.textPrimary : t.textDim);
        fontSize(42.0f);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        text(x, b.y + 58.0f, noteText.c_str(), nullptr);

        label(x + 130.0f, b.y + 46.0f, "Notes", true, 11.0f);
        label(x + 130.0f, b.y + 68.0f, format(core::ParamId::outCount).c_str(), false, 18.0f);
        label(x + 226.0f, b.y + 46.0f, "Velocity", true, 11.0f);
        label(x + 226.0f, b.y + 68.0f, format(core::ParamId::outVelocity).c_str(), false, 18.0f);

        label(x, b.y + 120.0f, "INPUT LEVEL", true, 11.0f);
        const float level = std::clamp(value(static_cast<std::uint32_t>(core::ParamId::outLevel)), 0.0f, 1.0f);
        const Rect bar {x, b.y + 140.0f, b.w - 32.0f, 10.0f};
        beginPath();
        fc(t.controlTrack);
        roundedRect(bar.x, bar.y, bar.w, bar.h, 3.0f);
        fill();
        if (level > 0.002f) {
            beginPath();
            fillColor(kStatusAccent.r, kStatusAccent.g, kStatusAccent.b, 255);
            roundedRect(bar.x, bar.y, bar.w * level, bar.h, 3.0f);
            fill();
        }
        label(x, b.y + 160.0f, format(core::ParamId::outLevel).c_str(), false, 11.0f);
        label(b.x + b.w - 96.0f, b.y + 160.0f, flash > 0.05f ? "DETECTED" : "idle", flash > 0.05f, 11.0f,
              ALIGN_RIGHT | ALIGN_TOP);
    }

    [[nodiscard]] std::string format(const core::ParamId id) const
    {
        char buffer[40];
        const float v = value(static_cast<std::uint32_t>(id));
        switch (id) {
        case core::ParamId::inputType:
            return core::kInputTypeNames[core::clampInt(v, 0, core::kInputTypeCount - 1)];
        case core::ParamId::sensitivity:
        case core::ParamId::dynamics:
        case core::ParamId::outLevel:
        case core::ParamId::outFlash:
            std::snprintf(buffer, sizeof(buffer), "%.0f%%", v * 100.0f);
            break;
        case core::ParamId::minNote:
        case core::ParamId::maxNote:
        case core::ParamId::outLastNote:
            return noteName(core::clampInt(v, 0, 127));
        case core::ParamId::maxNotes:
        case core::ParamId::outCount:
        case core::ParamId::velocity:
        case core::ParamId::outVelocity:
            std::snprintf(buffer, sizeof(buffer), "%d", core::clampInt(v, 0, 127));
            break;
        case core::ParamId::gate:
        case core::ParamId::release:
            std::snprintf(buffer, sizeof(buffer), "%d ms", core::clampInt(v, 0, 500));
            break;
        case core::ParamId::transpose:
            std::snprintf(buffer, sizeof(buffer), "%+d st", core::clampInt(v, -24, 24));
            break;
        case core::ParamId::channel:
            std::snprintf(buffer, sizeof(buffer), "Ch %d", core::clampInt(v, 1, 16));
            break;
        case core::ParamId::midiOut:
        case core::ParamId::audioThru:
            return core::kOnOffNames[core::clampInt(v, 0, 1)];
        case core::ParamId::count: break;
        }
        return buffer;
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyscribeUI)
};

UI* createUI()
{
    return new PolyscribeUI();
}

END_NAMESPACE_DISTRHO
