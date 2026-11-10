#include "DistrhoPlugin.hpp"

#include "sieve_engine.hpp"
#include "sieve_params.hpp"

START_NAMESPACE_DISTRHO

namespace core = downspout::sieve;

class SievePlugin : public Plugin {
public:
    SievePlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        for (int i = 0; i < core::kParameterCount; ++i)
            params_[i] = core::kParameterSpecs[i].defaultValue;
        core::activate(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Sieve"; }
    const char* getDescription() const override
    {
        return "Spectral sieve: keeps only partials near the notes of a chosen scale (or held MIDI notes), "
               "in every octave.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('S', 'i', 'e', 'v'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= core::kParameterCount) return;
        const core::ParameterSpec& spec = core::kParameterSpecs[index];
        parameter.hints = kParameterIsAutomatable;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        if (spec.unit[0] != '\0') parameter.unit = spec.unit;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index >= core::kParameterCount) return 0.0f;
        return params_[index];
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kParameterCount) return;
        params_[index] = value;
    }

    void activate() override
    {
        core::reset(engine_);
        applyConfiguration();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::activate(engine_, newSampleRate);
        applyConfiguration();
    }

    void run(const float** inputs, float** outputs, const uint32_t frames,
             const MidiEvent* events, const uint32_t eventCount) override
    {
        core::Params p;
        p.root = params_[0];
        p.scale = params_[1];
        p.source = params_[2];
        p.midiCh = params_[3];
        p.width = params_[4];
        p.floor = params_[5];
        p.tilt = params_[6];
        p.mix = params_[7];
        p.level = params_[8];
        for (uint32_t i = 0; i < eventCount; ++i)
        {
            const MidiEvent& event = events[i];
            if (event.size < 3) continue;
            core::handleMidiNote(engine_, event.data[0], event.data[1], event.data[2], p.midiCh);
        }
        core::processBlock(engine_, p, inputs[0], inputs[1], outputs[0], outputs[1], frames);
        reportLatency();
    }

private:
    void applyConfiguration() {}

    void reportLatency()
    {
        const int samples = core::latencySamples(engine_);
        if (static_cast<uint32_t>(samples) != reportedLatency_)
        {
            reportedLatency_ = static_cast<uint32_t>(samples);
            Plugin::setLatency(reportedLatency_);
        }
    }

    uint32_t reportedLatency_ = 0;

    core::Engine engine_ {};
    float params_[core::kParameterCount] {};
};

Plugin* createPlugin() { return new SievePlugin(); }

END_NAMESPACE_DISTRHO