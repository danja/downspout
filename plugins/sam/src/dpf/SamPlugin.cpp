#include "DistrhoPlugin.hpp"

#include "sam_engine.hpp"
#include "sam_params.hpp"

#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::sam;

constexpr const char* kStateKeyText = "text";

core::Transport toTransport(const TimePosition& position)
{
    core::Transport transport;
    transport.playing = position.playing;
    if (position.bbt.valid)
    {
        transport.valid = true;
        transport.bar = static_cast<double>(position.bbt.bar) - 1.0;
        transport.barBeat = static_cast<double>(position.bbt.beat) - 1.0
                          + (position.bbt.ticksPerBeat > 0.0 ? position.bbt.tick / position.bbt.ticksPerBeat : 0.0);
        transport.beatsPerBar = position.bbt.beatsPerBar;
        transport.bpm = position.bbt.beatsPerMinute;
    }
    return transport;
}

}  // namespace

class SamPlugin : public Plugin {
public:
    SamPlugin() : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, 1)
    {
        engine_.activate(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Sam"; }

    const char* getDescription() const override
    {
        return "Software Automatic Mouth: the 1982 formant speech synthesiser as a MIDI instrument that "
               "speaks a word per note, or as text-to-speech with words locked to the host transport.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT (SAM itself: abandonware, see plugins/sam/third_party/sam)"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'a', 'm', 'v'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount)
            return;

        const core::ParamSpec& spec = core::kParameterSpecs[index];
        parameter.hints = spec.output ? kParameterIsOutput : kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        if (spec.trigger)
            parameter.hints |= kParameterIsTrigger;
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::mode:
            setEnumeration(parameter, core::kModeNames.data(), core::kModeNames.size());
            break;
        case core::ParamId::advance:
            setEnumeration(parameter, core::kAdvanceNames.data(), core::kAdvanceNames.size());
            break;
        case core::ParamId::step:
            setEnumeration(parameter, core::kStepNames.data(), core::kStepNames.size());
            break;
        case core::ParamId::midiCh:
            setEnumeration(parameter, core::kMidiChannelNames.data(), core::kMidiChannelNames.size());
            break;
        case core::ParamId::monotone:
        case core::ParamId::phonetic:
        case core::ParamId::loop:
            parameter.hints |= kParameterIsBoolean;
            break;
        default:
            break;
        }
    }

    // The text is the one piece of data that is not a parameter. It is saved in the
    // project (DISTRHO_PLUGIN_WANT_FULL_STATE) and edited from the panel.
    void initState(const uint32_t index, State& state) override
    {
        if (index != 0)
            return;
        state.key = kStateKeyText;
        state.label = "Text";
        state.defaultValue = core::kDefaultText;
        state.hints = kStateIsHostWritable;
    }

    String getState(const char* key) const override
    {
        if (key != nullptr && std::strcmp(key, kStateKeyText) == 0)
            return String(engine_.text().c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (key != nullptr && value != nullptr && std::strcmp(key, kStateKeyText) == 0)
            engine_.setText(value);
    }

    float getParameterValue(const uint32_t index) const override { return engine_.getParameter(index); }

    void setParameterValue(const uint32_t index, const float value) override { engine_.setParameter(index, value); }

    // Runtime state only: the text and the settings are the patch and must survive.
    void activate() override { engine_.activate(getSampleRate()); }

    void sampleRateChanged(const double newSampleRate) override { engine_.activate(newSampleRate); }

    void run(const float**, float** outputs, const uint32_t frames, const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        // Copy the host's events into the core's plain type; sysex and anything longer than
        // three bytes is of no use here.
        core::MidiEvent events[kMaxMidiEvents];
        uint32_t count = 0;
        for (uint32_t i = 0; i < midiEventCount && count < kMaxMidiEvents; ++i)
        {
            const MidiEvent& in = midiEvents[i];
            if (in.size == 0 || in.size > 3)
                continue;
            core::MidiEvent& out = events[count++];
            out.frame = in.frame;
            out.size = static_cast<std::uint8_t>(in.size);
            for (uint32_t b = 0; b < in.size; ++b)
                out.data[b] = in.data[b];
        }

        engine_.process(toTransport(getTimePosition()), events, count, frames, outputs[0], outputs[1]);
    }

private:
    static constexpr uint32_t kMaxMidiEvents = 256;

    static void setEnumeration(Parameter& parameter, const char* const* names, const std::size_t count)
    {
        // The tables are static for the life of the plugin; DPF only keeps the pointer.
        static ParameterEnumerationValue values[32];
        static std::size_t used = 0;
        if (used + count > 32)
            return;
        ParameterEnumerationValue* mine = values + used;
        for (std::size_t i = 0; i < count; ++i)
        {
            mine[i].value = static_cast<float>(i);
            mine[i].label = names[i];
        }
        used += count;
        parameter.enumValues.count = static_cast<uint8_t>(count);
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = mine;
        parameter.enumValues.deleteLater = false;
    }

    core::Engine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SamPlugin)
};

Plugin* createPlugin()
{
    return new SamPlugin();
}

END_NAMESPACE_DISTRHO
