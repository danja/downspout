#include "DistrhoPlugin.hpp"

#include "floozy_engine.hpp"
#include "floozy_params.hpp"

#include <algorithm>
#include <array>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

using downspout::floozy::FloozyEngine;
using downspout::floozy::MidiMessage;
using downspout::floozy::kInterfaceTypeNames;
using downspout::floozy::kParameterCount;
using downspout::floozy::kParameterSpecs;
using downspout::floozy::kSourceAlgorithmNames;

ParameterEnumerationValue kSourceEnumValues[] = {
    {0.0f, kSourceAlgorithmNames[0]},
    {1.0f, kSourceAlgorithmNames[1]},
    {2.0f, kSourceAlgorithmNames[2]},
    {3.0f, kSourceAlgorithmNames[3]},
    {4.0f, kSourceAlgorithmNames[4]},
    {5.0f, kSourceAlgorithmNames[5]},
    {6.0f, kSourceAlgorithmNames[6]},
};

ParameterEnumerationValue kInterfaceEnumValues[] = {
    {0.0f, kInterfaceTypeNames[0]},
    {1.0f, kInterfaceTypeNames[1]},
    {2.0f, kInterfaceTypeNames[2]},
    {3.0f, kInterfaceTypeNames[3]},
    {4.0f, kInterfaceTypeNames[4]},
    {5.0f, kInterfaceTypeNames[5]},
    {6.0f, kInterfaceTypeNames[6]},
    {7.0f, kInterfaceTypeNames[7]},
    {8.0f, kInterfaceTypeNames[8]},
    {9.0f, kInterfaceTypeNames[9]},
    {10.0f, kInterfaceTypeNames[10]},
    {11.0f, kInterfaceTypeNames[11]},
    {12.0f, kInterfaceTypeNames[12]},
};

MidiMessage toCoreMidiMessage(const MidiEvent& event)
{
    MidiMessage message {};
    message.frame = event.frame;
    message.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, message.data.size()));
    const std::uint8_t* const bytes = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < message.size; ++i)
        message.data[i] = bytes[i];
    return message;
}

} // namespace

class FloozyPlugin : public Plugin
{
public:
    FloozyPlugin()
        : Plugin(kParameterCount, 0, 0)
        , engine_(static_cast<float>(getSampleRate()))
    {
    }

protected:
    const char* getLabel() const override
    {
        return "Floozy";
    }

    const char* getDescription() const override
    {
        return "Hybrid physical/modulation synthesizer with selectable 1-8 voices (default 4), derived from floozy-poly.";
    }

    const char* getMaker() const override
    {
        return "danja";
    }

    const char* getHomePage() const override
    {
        return "https://danja.github.io/downspout/";
    }

    const char* getLicense() const override
    {
        return "MIT";
    }

    uint32_t getVersion() const override
    {
        return d_version(0, 1, 0);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('F', 'l', 'z', 'y');
    }

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
        if (index >= kParameterCount)
            return;

        const auto& spec = kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        if (index == static_cast<uint32_t>(downspout::floozy::ParamId::sourceAlgorithm))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kSourceEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kSourceEnumValues;
            parameter.enumValues.deleteLater = false;
        }
        else if (index == static_cast<uint32_t>(downspout::floozy::ParamId::interfaceType))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kInterfaceEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kInterfaceEnumValues;
            parameter.enumValues.deleteLater = false;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return engine_.getParameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        engine_.setParameter(index, value);
    }

    void activate() override
    {
        engine_.reset();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        engine_.setSampleRate(static_cast<float>(newSampleRate));
    }

    void run(const float** inputs,
             float** outputs,
             const uint32_t frames,
             const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        std::array<MidiMessage, 256> stackMessages {};
        std::uint32_t messageCount = 0;
        for (std::uint32_t i = 0; i < midiEventCount && messageCount < stackMessages.size(); ++i)
            stackMessages[messageCount++] = toCoreMidiMessage(midiEvents[i]);

        engine_.processBlock(outputs[0], outputs[1], frames, stackMessages.data(), messageCount,
                             inputs[0], inputs[1]);
    }

private:
    FloozyEngine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloozyPlugin)
};

Plugin* createPlugin()
{
    return new FloozyPlugin();
}

END_NAMESPACE_DISTRHO
