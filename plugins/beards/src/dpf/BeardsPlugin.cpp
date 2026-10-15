#include "DistrhoPlugin.hpp"

#include "beards_engine.hpp"
#include "beards_params.hpp"

START_NAMESPACE_DISTRHO

namespace core = downspout::beards;

class BeardsPlugin : public Plugin {
public:
    BeardsPlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, 0)
    {
        core::activate(engineState_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Beards"; }

    const char* getDescription() const override
    {
        return "Granular texture processor after Mutable Instruments Beads: a four second stereo "
               "buffer played by up to 32 grains, with feedback, freeze and reverb. Controllable "
               "from Drift and Conductor.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('B', 'd', 'r', 'd'); }

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

        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::freeze:
            parameter.hints |= kParameterIsBoolean;
            break;

        case core::ParamId::grain:
            parameter.hints |= kParameterIsBoolean | kParameterIsTrigger;
            break;

        case core::ParamId::quality:
        {
            static ParameterEnumerationValue values[3];
            for (int i = 0; i < 3; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kQualityNames[static_cast<std::size_t>(i)];
            }
            setEnumerations(parameter, values, 3);
            break;
        }

        case core::ParamId::midiCh:
        {
            static ParameterEnumerationValue values[18];
            for (int i = 0; i < 18; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kMidiChannelNames[static_cast<std::size_t>(i)];
            }
            setEnumerations(parameter, values, 18);
            break;
        }

        case core::ParamId::conductorCh:
        {
            static ParameterEnumerationValue values[17];
            for (int i = 0; i < 17; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kMidiChannelNames[static_cast<std::size_t>(i)];
            }
            setEnumerations(parameter, values, 17);
            break;
        }

        default:
            break;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.value[index];
        if (index == static_cast<uint32_t>(core::ParamId::outGrains))
            return static_cast<float>(core::activeGrains(engineState_));
        return 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;
        parameters_.value[index] = core::clampParameter(static_cast<core::ParamId>(index), value);
    }

    // Clears runtime state only; the patch stays as the host last set it.
    void activate() override { core::activate(engineState_, getSampleRate()); }

    void sampleRateChanged(const double newSampleRate) override { core::activate(engineState_, newSampleRate); }

    void run(const float** inputs, float** outputs, const uint32_t frames, const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        // CCs are applied at the top of the block; the engine smooths the controls
        // that would otherwise click.
        for (uint32_t i = 0; i < midiEventCount; ++i)
        {
            const MidiEvent& event = midiEvents[i];
            if (event.size >= 3)
                core::applyMidi(parameters_, engineState_, event.data, event.size);
        }

        core::processBlock(engineState_, parameters_, frames, inputs[0], inputs[1], outputs[0], outputs[1]);
    }

private:
    static void setEnumerations(Parameter& parameter, ParameterEnumerationValue* values, const uint8_t count)
    {
        parameter.enumValues.count = count;
        parameter.enumValues.restrictedMode = true;
        parameter.enumValues.values = values;
        parameter.enumValues.deleteLater = false;
    }

    core::Parameters parameters_;
    core::EngineState engineState_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeardsPlugin)
};

Plugin* createPlugin()
{
    return new BeardsPlugin();
}

END_NAMESPACE_DISTRHO
