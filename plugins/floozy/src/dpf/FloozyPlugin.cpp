#include "DistrhoPlugin.hpp"

#include "floozy_engine.hpp"
#include "floozy_params.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

using downspout::floozy::FloozyEngine;
using downspout::floozy::MidiMessage;
using downspout::floozy::kBodyTypeNames;
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
    {7.0f, kSourceAlgorithmNames[7]},
};

ParameterEnumerationValue kBodyTypeEnumValues[] = {
    {0.0f, kBodyTypeNames[0]},
    {1.0f, kBodyTypeNames[1]},
    {2.0f, kBodyTypeNames[2]},
    {3.0f, kBodyTypeNames[3]},
    {4.0f, kBodyTypeNames[4]},
    {5.0f, kBodyTypeNames[5]},
    {6.0f, kBodyTypeNames[6]},
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
        else if (index == static_cast<uint32_t>(downspout::floozy::ParamId::bodyType))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kBodyTypeEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kBodyTypeEnumValues;
            parameter.enumValues.deleteLater = false;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index == static_cast<uint32_t>(downspout::floozy::ParamId::conductorChannel))
            return static_cast<float>(conductorChannel_);
        return engine_.getParameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index == static_cast<uint32_t>(downspout::floozy::ParamId::conductorChannel))
            conductorChannel_ = std::max(0, std::min(16, static_cast<int>(std::lround(value))));
        else
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
        if (conductorChannel_ > 0 && midiEvents != nullptr) {
            const int ch = conductorChannel_ - 1;
            for (uint32_t i = 0; i < midiEventCount; ++i) {
                const auto& ev = midiEvents[i];
                if (ev.size >= 3 && (ev.data[0] & 0xf0) == 0xb0 && (ev.data[0] & 0x0f) == ch) {
                    const uint8_t cc  = ev.data[1];
                    const uint8_t val = ev.data[2];
                    using PI = downspout::floozy::ParamId;
                    switch (cc) {
                    case 20: engine_.setParameter(PI::bodyType,
                                 static_cast<float>((val * 7) / 128)); break;
                    case 21: engine_.setParameter(PI::sourceLevel,  val / 127.0f); break;
                    case 22: engine_.setParameter(PI::masterGain,   val / 127.0f); break;
                    case 23: engine_.setParameter(PI::interfaceIntensity, val / 127.0f); break;
                    case 24: if (val >= 64) engine_.reset(); break;
                    default: break;
                    }
                }
            }
        }

        std::array<MidiMessage, 256> stackMessages {};
        std::uint32_t messageCount = 0;
        for (std::uint32_t i = 0; i < midiEventCount && messageCount < stackMessages.size(); ++i)
            stackMessages[messageCount++] = toCoreMidiMessage(midiEvents[i]);

        engine_.processBlock(outputs[0], outputs[1], frames, stackMessages.data(), messageCount,
                             inputs[0], inputs[1]);
    }

private:
    FloozyEngine engine_;
    int conductorChannel_ = 0;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FloozyPlugin)
};

Plugin* createPlugin()
{
    return new FloozyPlugin();
}

END_NAMESPACE_DISTRHO
