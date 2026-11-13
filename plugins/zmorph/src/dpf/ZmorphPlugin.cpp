#include "DistrhoPlugin.hpp"

#include "zmorph_engine.hpp"
#include "zmorph_params.hpp"

START_NAMESPACE_DISTRHO

namespace core = downspout::zmorph;

class ZmorphPlugin : public Plugin {
public:
    ZmorphPlugin()
        : Plugin(core::kParameterCount, 0, 0)
    {
        for (int i = 0; i < core::kParameterCount; ++i)
            params_[i] = core::kParameterSpecs[i].defaultValue;
        core::activate(engine_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Zmorph"; }
    const char* getDescription() const override { return "Z-plane morph filter: morph between two pole/zero sets."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('Z', 'm', 'r', 'p'); }

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
             const MidiEvent* midiEvents, const uint32_t midiEventCount) override
    {
        core::Params p;
        p.cutoff = params_[0];
        p.resonance = params_[1];
        p.morph = params_[2];
        p.mix = params_[3];
        p.level = params_[4];
        p.noteTracking = false;
        p.noteCutoff = 0.0f;

        if (midiEvents != nullptr) {
            for (uint32_t i = 0; i < midiEventCount; ++i) {
                const MidiEvent& event = midiEvents[i];
                const uint8_t* const data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
                if (event.size < 3)
                    continue;
                const uint8_t status = data[0] & 0xF0u;
                const uint8_t channel = data[0] & 0x0Fu;
                const uint8_t data1 = data[1];
                const uint8_t data2 = data[2];
                (void)channel;

                if (status == 0x90u && data2 > 0) {
                    const float cutoff = core::noteToCutoff(static_cast<int>(data1));
                    p.noteCutoff = cutoff;
                    p.noteTracking = true;
                    params_[0] = cutoff;
                    requestParameterValueChange(0, cutoff);
                }
                else if (status == 0x80u || (status == 0x90u && data2 == 0)) {
                    p.noteTracking = false;
                    p.noteCutoff = 0.0f;
                }
                else if (status == 0xB0u) {
                    core::ParamId target = core::kCutoff;
                    if (core::controllerTarget(data1, target)) {
                        const float value = core::controllerToParameter(target, data2);
                        params_[static_cast<uint32_t>(target)] = value;
                        requestParameterValueChange(static_cast<uint32_t>(target), value);
                    }
                }
            }
        }

        core::processBlock(engine_, p, inputs[0], inputs[1], outputs[0], outputs[1], frames);
    }

private:
    void applyConfiguration() {}

    core::Engine engine_ {};
    float params_[core::kParameterCount] {};
};

Plugin* createPlugin() { return new ZmorphPlugin(); }

END_NAMESPACE_DISTRHO