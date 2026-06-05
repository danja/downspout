#include "DistrhoPlugin.hpp"

#include "canticle_engine.hpp"
#include "canticle_params.hpp"

#include <array>
#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {

using downspout::canticle::CanticleEngine;
using downspout::canticle::ParamId;
using downspout::canticle::kModelNames;
using downspout::canticle::kParameterCount;
using downspout::canticle::kParameterSpecs;

ParameterEnumerationValue kModelEnumValues[] = {
    {0.0f, kModelNames[0]},
    {1.0f, kModelNames[1]},
    {2.0f, kModelNames[2]},
    {3.0f, kModelNames[3]},
    {4.0f, kModelNames[4]},
};

void handleMidiEvent(CanticleEngine& engine, const MidiEvent& event)
{
    const std::uint8_t* const data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    engine.handleMidi(data, event.size);
}

} // namespace

class CanticlePlugin : public Plugin {
public:
    CanticlePlugin()
        : Plugin(kParameterCount, 0, 0)
        , engine_(static_cast<float>(getSampleRate()))
    {
    }

protected:
    const char* getLabel() const override
    {
        return "Canticle";
    }

    const char* getDescription() const override
    {
        return "Warm polyphonic keys, reed, pad, pluck, and glass voice for Downspout MIDI generators.";
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
        return d_cconst('C', 'n', 't', 'l');
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

        if (index == static_cast<uint32_t>(ParamId::model))
        {
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kModelEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kModelEnumValues;
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
    CanticleEngine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CanticlePlugin)
};

Plugin* createPlugin()
{
    return new CanticlePlugin();
}

END_NAMESPACE_DISTRHO
