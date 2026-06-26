#include "DistrhoPlugin.hpp"

#include "basilico_engine.hpp"
#include "basilico_params.hpp"

#include <array>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

using downspout::basilico::BasilicoEngine;
using downspout::basilico::ParamId;
using downspout::basilico::TransportSnapshot;
using downspout::basilico::kDriveTypeNames;
using downspout::basilico::kModelNames;
using downspout::basilico::kParameterCount;
using downspout::basilico::kParameterSpecs;
using downspout::basilico::kWaveformNames;
using downspout::basilico::kWobbleDivisionNames;
using downspout::basilico::kWobbleShapeNames;
using downspout::basilico::kWobbleSyncNames;

ParameterEnumerationValue kModelEnumValues[] = {
    {0.0f, kModelNames[0]},
    {1.0f, kModelNames[1]},
    {2.0f, kModelNames[2]},
    {3.0f, kModelNames[3]},
    {4.0f, kModelNames[4]},
};

ParameterEnumerationValue kWaveEnumValues[] = {
    {0.0f, kWaveformNames[0]},
    {1.0f, kWaveformNames[1]},
    {2.0f, kWaveformNames[2]},
    {3.0f, kWaveformNames[3]},
    {4.0f, kWaveformNames[4]},
};

ParameterEnumerationValue kDriveEnumValues[] = {
    {0.0f, kDriveTypeNames[0]},
    {1.0f, kDriveTypeNames[1]},
    {2.0f, kDriveTypeNames[2]},
    {3.0f, kDriveTypeNames[3]},
};

ParameterEnumerationValue kWobbleSyncEnumValues[] = {
    {0.0f, kWobbleSyncNames[0]},
    {1.0f, kWobbleSyncNames[1]},
};

ParameterEnumerationValue kWobbleDivisionEnumValues[] = {
    {0.0f, kWobbleDivisionNames[0]},
    {1.0f, kWobbleDivisionNames[1]},
    {2.0f, kWobbleDivisionNames[2]},
    {3.0f, kWobbleDivisionNames[3]},
    {4.0f, kWobbleDivisionNames[4]},
    {5.0f, kWobbleDivisionNames[5]},
    {6.0f, kWobbleDivisionNames[6]},
    {7.0f, kWobbleDivisionNames[7]},
};

ParameterEnumerationValue kWobbleShapeEnumValues[] = {
    {0.0f, kWobbleShapeNames[0]},
    {1.0f, kWobbleShapeNames[1]},
    {2.0f, kWobbleShapeNames[2]},
    {3.0f, kWobbleShapeNames[3]},
    {4.0f, kWobbleShapeNames[4]},
};

void handleMidiEvent(BasilicoEngine& engine, const MidiEvent& event)
{
    const std::uint8_t* const data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    engine.handleMidi(data, event.size);
}

TransportSnapshot toTransport(const TimePosition& timePos)
{
    TransportSnapshot transport {};
    transport.valid = timePos.bbt.valid;
    transport.playing = timePos.playing;
    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1) +
                            (timePos.bbt.ticksPerBeat > 0.0
                                 ? timePos.bbt.tick / timePos.bbt.ticksPerBeat
                                 : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.bpm = timePos.bbt.beatsPerMinute;
    }
    return transport;
}

} // namespace

class BasilicoPlugin : public Plugin
{
public:
    BasilicoPlugin()
        : Plugin(kParameterCount, 0, 0)
        , engine_(static_cast<float>(getSampleRate()))
    {
    }

protected:
    const char* getLabel() const override
    {
        return "Basilico";
    }

    const char* getDescription() const override
    {
        return "Generator-friendly bass instrument covering upright, electric, dub, acid, and industrial tones.";
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
        return d_cconst('B', 's', 'l', 'c');
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
        if (index == static_cast<uint32_t>(ParamId::wobbleStart))
            parameter.unit = "deg";

        if (index == static_cast<uint32_t>(ParamId::model))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kModelEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kModelEnumValues;
            parameter.enumValues.deleteLater = false;
        }
        else if (index == static_cast<uint32_t>(ParamId::waveform))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kWaveEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kWaveEnumValues;
            parameter.enumValues.deleteLater = false;
        }
        else if (index == static_cast<uint32_t>(ParamId::driveType))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kDriveEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kDriveEnumValues;
            parameter.enumValues.deleteLater = false;
        }
        else if (index == static_cast<uint32_t>(ParamId::wobbleSync))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kWobbleSyncEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kWobbleSyncEnumValues;
            parameter.enumValues.deleteLater = false;
        }
        else if (index == static_cast<uint32_t>(ParamId::wobbleDivision))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kWobbleDivisionEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kWobbleDivisionEnumValues;
            parameter.enumValues.deleteLater = false;
        }
        else if (index == static_cast<uint32_t>(ParamId::wobbleShape))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kWobbleShapeEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kWobbleShapeEnumValues;
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

    void run(const float**,
             float** outputs,
             const uint32_t frames,
             const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        engine_.setTransport(toTransport(getTimePosition()));

        std::uint32_t eventIndex = 0;
        for (std::uint32_t frame = 0; frame < frames; ++frame)
        {
            while (eventIndex < midiEventCount && midiEvents[eventIndex].frame <= frame)
                handleMidiEvent(engine_, midiEvents[eventIndex++]);

            const auto out = engine_.processStereo();
            outputs[0][frame] = out.left;
            outputs[1][frame] = out.right;
        }

        while (eventIndex < midiEventCount)
            handleMidiEvent(engine_, midiEvents[eventIndex++]);
    }

private:
    BasilicoEngine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BasilicoPlugin)
};

Plugin* createPlugin()
{
    return new BasilicoPlugin();
}

END_NAMESPACE_DISTRHO
