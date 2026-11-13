#include "DistrhoPlugin.hpp"

#include "bore_engine.hpp"
#include "bore_params.hpp"

#include <algorithm>
#include <array>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::bore;

}  // namespace

class BorePlugin : public Plugin {
public:
    BorePlugin() : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, 0)
    {
        engine_.activate(getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Bore"; }
    const char* getDescription() const override
    {
        return "Eight-voice lip-reed instrument with a pitch-tuned feedback bore and adjustable embouchure, bell and breath.";
    }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('B', 'o', 'r', 'E'); }

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

        if (static_cast<core::ParamId>(index) == core::ParamId::midiCh) {
            static ParameterEnumerationValue values[18];
            for (int i = 0; i < 18; ++i) {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kMidiChannelNames[static_cast<std::size_t>(i)];
            }
            parameter.enumValues.count = 18;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
        }
    }

    float getParameterValue(const uint32_t index) const override { return engine_.getParameter(index); }
    void setParameterValue(const uint32_t index, const float value) override { engine_.setParameter(index, value); }

    void activate() override { engine_.activate(getSampleRate()); }
    void sampleRateChanged(const double sampleRate) override { engine_.activate(sampleRate); }

    void run(const float**,
             float** outputs,
             const uint32_t frames,
             const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        std::array<core::MidiMessage, 256> events {};
        const uint32_t count = std::min<uint32_t>(midiEventCount, static_cast<uint32_t>(events.size()));
        for (uint32_t i = 0; i < count; ++i) {
            const MidiEvent& source = midiEvents[i];
            const uint8_t* const bytes = source.size > MidiEvent::kDataSize ? source.dataExt : source.data;
            core::MidiMessage& target = events[i];
            target.frame = source.frame;
            target.size = std::min<uint32_t>(source.size, 3u);
            for (uint32_t b = 0; b < target.size; ++b)
                target.data[b] = bytes[b];
        }
        std::stable_sort(events.begin(), events.begin() + count,
                         [](const core::MidiMessage& a, const core::MidiMessage& b) { return a.frame < b.frame; });
        engine_.process(outputs[0], outputs[1], frames, events.data(), count);
    }

private:
    core::Engine engine_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BorePlugin)
};

Plugin* createPlugin()
{
    return new BorePlugin();
}

END_NAMESPACE_DISTRHO
