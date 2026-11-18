#include "DistrhoPlugin.hpp"

#include "polyscribe_core_types.hpp"
#include "polyscribe_params.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::polyscribe;

struct EnumTable {
    const char* const* names;
    std::size_t count;
};

template <std::size_t N>
constexpr EnumTable table(const std::array<const char*, N>& names)
{
    return {names.data(), N};
}

constexpr EnumTable enumFor(const core::ParamId id)
{
    switch (id)
    {
    case core::ParamId::inputType: return table(core::kInputTypeNames);
    case core::ParamId::midiOut:
    case core::ParamId::audioThru: return table(core::kOnOffNames);
    default: return {nullptr, 0};
    }
}

}  // namespace

class PolyscribePlugin : public Plugin {
public:
    PolyscribePlugin()
        : Plugin(static_cast<std::uint32_t>(core::kParameterCount), 0, 0)
    {
        core::prepare(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Polyscribe"; }

    const char* getDescription() const override
    {
        return "Polyphonic audio-to-MIDI generator. Listens for melody, bass, chords or drums and "
               "emits note messages with sensitivity, note range, gate, release, velocity and "
               "dynamics controls.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('P', 's', 'c', 'r'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
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
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        const EnumTable names = enumFor(static_cast<core::ParamId>(index));
        if (names.count == 0)
            return;
        // DPF keeps this pointer; deleteLater lets it clean up with the parameter.
        auto* values = new ParameterEnumerationValue[names.count];
        for (std::size_t i = 0; i < names.count; ++i) {
            values[i].value = static_cast<float>(i);
            values[i].label = names.names[i];
        }
        parameter.enumValues.count = static_cast<uint8_t>(names.count);
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = values;
        parameter.enumValues.deleteLater = true;
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.*core::kParameterFields[index];

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::outCount: return static_cast<float>(core::activeNoteCount(engine_));
        case core::ParamId::outLastNote: return static_cast<float>(core::lastNote(engine_));
        case core::ParamId::outLevel: return core::inputLevel(engine_);
        case core::ParamId::outVelocity: return static_cast<float>(core::lastVelocity(engine_));
        case core::ParamId::outFlash: return core::flashLevel(engine_);
        default: return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // status outputs are read-only
        parameters_.*core::kParameterFields[index] = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // activate() clears analysis and note state only: the patch lives in parameters_.
    void activate() override { core::reset(engine_); }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::prepare(engine_, newSampleRate);
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        core::processBlock(engine_, parameters_, frames, inputs, outputs);

        const core::MidiOutEvent* events = core::midiOutEvents(engine_);
        for (std::size_t i = 0; i < core::midiOutCount(engine_); ++i) {
            MidiEvent out {};
            out.frame = events[i].frame;
            out.size = 3;
            std::memcpy(out.data, events[i].data, 3);
            writeMidiEvent(out);
        }
    }

private:
    core::Parameters parameters_;
    core::EngineState engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PolyscribePlugin)
};

Plugin* createPlugin()
{
    return new PolyscribePlugin();
}

END_NAMESPACE_DISTRHO
