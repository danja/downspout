#include "DistrhoPlugin.hpp"

#include "mouth_engine.hpp"
#include "mouth_params.hpp"

START_NAMESPACE_DISTRHO

namespace core = downspout::mouth;

class MouthPlugin : public Plugin {
public:
    MouthPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        core::activate(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Mouth"; }

    const char* getDescription() const override
    {
        return "Vowel filter: five parallel formant band-passes morph between A, E, I, O and U on an x-y "
               "pad, for bass, tenor, alto and soprano voices. Shift, resonance, stereo spread, drive and "
               "air. Drift CC 1-4 move the vowel, shift and resonance.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('M', 'o', 'u', 't'); }

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

        const core::ParameterSpec& spec = core::kParameterSpecs[index];
        parameter.hints = kParameterIsAutomatable;
        if (spec.integer)
            parameter.hints |= kParameterIsInteger;
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0')
            parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;

        if (index == core::kVoice)
        {
            // DPF keeps this pointer and frees it itself (deleteLater).
            auto* values = new ParameterEnumerationValue[core::kVoices];
            for (std::size_t i = 0; i < core::kVoices; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kVoiceNames[i];
            }
            parameter.enumValues.count = static_cast<uint8_t>(core::kVoices);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = true;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        return index < core::kParameterCount ? core::field(parameters_, index) : 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kParameterCount)
            return;
        core::field(parameters_, index) = value;
        parameters_ = core::clampParameters(parameters_);
        // The panel or the host has taken the control back from a controller.
        if (const int slot = core::liveSlot(index); slot >= 0)
            live_.value[static_cast<std::size_t>(slot)] = -1.0f;
    }

    // Clears the filter memory only. The patch lives in parameters_, which activation never touches.
    void activate() override
    {
        core::reset(engine_);
        live_ = core::LiveControl {};
    }

    void sampleRateChanged(const double newSampleRate) override { core::activate(engine_, newSampleRate); }

    void run(const float** inputs, float** outputs, const uint32_t frames, const MidiEvent* events,
             const uint32_t eventCount) override
    {
        // CC 1-4 on the set channel override Vowel X, Vowel Y, Shift and Resonance (the Drift convention).
        for (uint32_t i = 0; i < eventCount; ++i)
        {
            const MidiEvent& e = events[i];
            if (e.size >= 3 && core::ccChannelMatches(e.data[0], parameters_.ccChannel))
                core::handleController(live_, e.data[1], e.data[2]);
        }
        const core::Parameters effective = core::withLive(parameters_, live_);
        core::processBlock(engine_, effective, inputs[0], inputs[1], outputs[0], outputs[1], frames);
    }

private:
    core::Parameters parameters_;
    core::LiveControl live_;
    core::Engine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MouthPlugin)
};

Plugin* createPlugin()
{
    return new MouthPlugin();
}

END_NAMESPACE_DISTRHO
