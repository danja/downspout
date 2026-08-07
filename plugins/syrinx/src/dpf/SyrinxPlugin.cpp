#include "DistrhoPlugin.hpp"

#include "syrinx_engine.hpp"
#include "syrinx_params.hpp"

#include <cstdint>

START_NAMESPACE_DISTRHO

namespace {
using downspout::syrinx::Engine;
using downspout::syrinx::kParameterCount;
using downspout::syrinx::getParameterSpec;

void handleMidiEvent(Engine& engine, const MidiEvent& event)
{
    const std::uint8_t* data = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    engine.handleMidi(data, event.size);
}
} // namespace

class SyrinxPlugin : public Plugin {
public:
    SyrinxPlugin()
        : Plugin(kParameterCount, 0, 0)
        , engine_(static_cast<float>(getSampleRate()))
    {}

protected:
    const char* getLabel()       const override { return "Syrinx"; }
    const char* getDescription() const override
    {
        return "Biophysical bird vocal synthesiser based on the Mindlin-Laje syrinx model. "
               "10 presets, chromatic MIDI playback, CC parameter control.";
    }
    const char* getMaker()    const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()  const override { return "MIT"; }

    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t  getUniqueId() const override { return d_cconst('S', 'y', 'R', 'x'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initParameter(const uint32_t index, Parameter& parameter) override
    {
        if (index >= kParameterCount) return;
        const auto spec = getParameterSpec(index);
        parameter.name   = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints  = kParameterIsAutomatable;
        if (spec.boolean) parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
    }

    float    getParameterValue(const uint32_t index) const override { return engine_.getParameter(index); }
    void     setParameterValue(const uint32_t index, const float value) override { engine_.setParameter(index, value); }
    void     activate() override { engine_.reset(); }
    void     sampleRateChanged(const double newSampleRate) override
    {
        engine_.setSampleRate(static_cast<float>(newSampleRate));
    }

    void run(const float**, float** outputs, const uint32_t frames,
             const MidiEvent* midiEvents, const uint32_t midiEventCount) override
    {
        std::uint32_t eventIndex = 0;
        for (std::uint32_t frame = 0; frame < frames; ++frame) {
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
    Engine engine_;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SyrinxPlugin)
};

Plugin* createPlugin() { return new SyrinxPlugin(); }

END_NAMESPACE_DISTRHO
