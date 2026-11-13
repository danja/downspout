#include "downspout/dpf/MagnetoKit.hpp"

#include "bore_params.hpp"

#include <array>
#include <cstdio>

START_NAMESPACE_DISTRHO

namespace core = downspout::bore;

namespace {

constexpr kit::Accent kMouthAccent {176, 88, 62};
constexpr kit::Accent kBoreAccent {92, 140, 156};
constexpr kit::Accent kPlayAccent {124, 148, 104};

struct Tables {
    std::array<kit::Range, core::kParameterCount> ranges {};
    std::array<float, core::kParameterCount> defaults {};
    Tables()
    {
        for (std::uint32_t i = 0; i < core::kParameterCount; ++i) {
            const core::ParamSpec& spec = core::kParameterSpecs[i];
            ranges[i] = {spec.minimum, spec.maximum, spec.integer};
            defaults[i] = spec.defaultValue;
        }
    }
};

const Tables& tables()
{
    static const Tables value;
    return value;
}

constexpr std::uint32_t idx(const core::ParamId parameter)
{
    return static_cast<std::uint32_t>(parameter);
}

}  // namespace

class BoreUI : public MagnetoUI {
public:
    BoreUI() : MagnetoUI(tables().ranges.data(), core::kParameterCount, tables().defaults.data()) {}

protected:
    void onAction(int) override {}

    void onNanoDisplay() override
    {
        beginFrame();
        drawHeader("BORE", "lip-reed / feedback bore",
                   "MIDI pitch sets the bore length; pressure drives a buzzing reed, and the bell opens the resonant tone.");
        drawInstrument({24.0f, 108.0f, 350.0f, 480.0f});
        drawMouthpiece({390.0f, 108.0f, 506.0f, 250.0f});
        drawPerformance({390.0f, 370.0f, 506.0f, 218.0f});
    }

private:
    void slider(const core::ParamId parameter, const char* title, const Rect bounds, const Accent accent)
    {
        char shown[32];
        const float v = value(idx(parameter));
        switch (parameter) {
        case core::ParamId::embouchure:
            std::snprintf(shown, sizeof(shown), "%.3fx", static_cast<double>(v));
            break;
        case core::ParamId::attack:
        case core::ParamId::release:
            std::snprintf(shown, sizeof(shown), "%.0f ms", static_cast<double>(v));
            break;
        default:
            std::snprintf(shown, sizeof(shown), "%d%%", static_cast<int>(v * 100.0f + 0.5f));
            break;
        }
        drawSlider(idx(parameter), title, shown, bounds, accent);
    }

    void drawInstrument(const Rect b)
    {
        drawPanel(b, "THE AIR COLUMN", kBoreAccent, "one delay line per voice");
        const float cx = b.x + b.w * 0.5f;
        const float y = b.y + 144.0f;
        const float pressure = value(idx(core::ParamId::pressure));
        const float bell = value(idx(core::ParamId::bell));
        const float tubeW = 92.0f + bell * 24.0f;
        const float tubeEnd = b.x + b.w - 46.0f;

        beginPath();
        fillColor(54, 65, 69, 255);
        moveTo(b.x + 42.0f, y - 18.0f);
        lineTo(tubeEnd - 60.0f, y - 18.0f);
        lineTo(tubeEnd, y - tubeW * 0.5f);
        lineTo(tubeEnd, y + tubeW * 0.5f);
        lineTo(tubeEnd - 60.0f, y + 18.0f);
        lineTo(b.x + 42.0f, y + 18.0f);
        closePath();
        fill();
        beginPath();
        strokeColor(kBoreAccent.r, kBoreAccent.g, kBoreAccent.b, 255);
        strokeWidth(2.0f);
        moveTo(b.x + 42.0f, y - 18.0f);
        lineTo(tubeEnd - 60.0f, y - 18.0f);
        lineTo(tubeEnd, y - tubeW * 0.5f);
        moveTo(tubeEnd, y + tubeW * 0.5f);
        lineTo(tubeEnd - 60.0f, y + 18.0f);
        lineTo(b.x + 42.0f, y + 18.0f);
        stroke();

        beginPath();
        fillColor(kMouthAccent.r, kMouthAccent.g, kMouthAccent.b, 255);
        roundedRect(b.x + 30.0f, y - 28.0f, 28.0f, 56.0f, 6.0f);
        fill();
        beginPath();
        fillColor(203, 169, 91, static_cast<int>(90 + pressure * 150.0f));
        circle(cx, y, 4.0f + pressure * 10.0f);
        fill();

        label(b.x + 36.0f, y + 64.0f, "LIPS", false, 11.0f, ALIGN_CENTER | ALIGN_MIDDLE);
        label(tubeEnd - 8.0f, y + tubeW * 0.5f + 16.0f, "BELL", false, 11.0f, ALIGN_RIGHT | ALIGN_MIDDLE);
        label(b.x + 18.0f, b.y + 254.0f, "MIDI NOTE  >  LIP REED  >  TUNED BORE  >  BELL", false, 11.0f);

        char status[48];
        std::snprintf(status, sizeof(status), "Voices sounding: %d / %d",
                      intValue(idx(core::ParamId::outVoices)), static_cast<int>(core::kVoiceCount));
        label(b.x + 18.0f, b.y + 306.0f, status, false, 14.0f);
        label(b.x + 18.0f, b.y + 344.0f, "Pressure raises loop sustain and reed bite.", true, 12.0f);
        label(b.x + 18.0f, b.y + 364.0f, "Embouchure detunes the lip against the bore.", true, 12.0f);
        label(b.x + 18.0f, b.y + 384.0f, "Bell changes bore damping; Breath adds air.", true, 12.0f);
        label(b.x + 18.0f, b.y + 425.0f, "A designed waveguide voice, not a sampled horn.", true, 11.0f);
        label(b.x + 18.0f, b.y + 443.0f, "Eight-note polyphony. CC 1-4 shape the tone.", true, 11.0f);
    }

    void drawMouthpiece(const Rect b)
    {
        drawPanel(b, "MOUTHPIECE  /  BORE", kMouthAccent, "CC 1-4");
        const float left = b.x + 16.0f;
        const float right = b.x + b.w * 0.5f + 4.0f;
        const float width = b.w * 0.5f - 28.0f;
        slider(core::ParamId::pressure, "Pressure  (CC 1)", {left, b.y + 52.0f, width, 42.0f}, kMouthAccent);
        slider(core::ParamId::embouchure, "Embouchure  (CC 2)", {right, b.y + 52.0f, width, 42.0f}, kMouthAccent);
        slider(core::ParamId::bell, "Bell  (CC 3)", {left, b.y + 126.0f, width, 42.0f}, kBoreAccent);
        slider(core::ParamId::breath, "Breath  (CC 4)", {right, b.y + 126.0f, width, 42.0f}, kBoreAccent);
        label(left, b.y + 202.0f, "Pressure excites the feedback loop; lip ratio colours the pitch.", true, 11.0f);
    }

    void drawPerformance(const Rect b)
    {
        drawPanel(b, "PERFORMANCE", kPlayAccent);
        const float left = b.x + 16.0f;
        const float right = b.x + b.w * 0.5f + 4.0f;
        const float width = b.w * 0.5f - 28.0f;
        slider(core::ParamId::attack, "Attack", {left, b.y + 44.0f, width, 40.0f}, kPlayAccent);
        slider(core::ParamId::release, "Release", {right, b.y + 44.0f, width, 40.0f}, kPlayAccent);
        slider(core::ParamId::velocity, "Velocity", {left, b.y + 100.0f, width, 40.0f}, kPlayAccent);
        slider(core::ParamId::level, "Level  (CC 7)", {right, b.y + 100.0f, width, 40.0f}, kPlayAccent);

        const int channel = std::clamp(intValue(idx(core::ParamId::midiCh)), 0, 17);
        drawStepper(idx(core::ParamId::midiCh), "MIDI input",
                    core::kMidiChannelNames[static_cast<std::size_t>(channel)], {left, b.y + 158.0f, width, 40.0f});
        label(right, b.y + 179.0f, "CC 120: all sound off   /   CC 123: all notes off", true, 10.0f, ALIGN_LEFT | ALIGN_MIDDLE);
    }
};

UI* createUI()
{
    return new BoreUI();
}

END_NAMESPACE_DISTRHO
