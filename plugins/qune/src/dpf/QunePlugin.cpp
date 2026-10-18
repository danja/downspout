#include "DistrhoPlugin.hpp"

#include "qune_engine.hpp"
#include "qune_params.hpp"

#include <algorithm>
#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::qune;

// Enumerated parameters get a label list so hosts show names, not numbers.
struct EnumTable {
    const char* const* names;
    std::size_t count;
};

template <std::size_t N>
constexpr EnumTable table(const std::array<const char*, N>& names)
{
    return {names.data(), N};
}

constexpr EnumTable enumFor(const core::ParamId id)
{
    switch (id)
    {
    case core::ParamId::root: return table(core::kNoteNames);
    case core::ParamId::scale: return table(core::kScaleNames);
    case core::ParamId::source: return table(core::kSourceNames);
    case core::ParamId::noNotes: return table(core::kNoNotesNames);
    case core::ParamId::midiCh: return table(core::kMidiChannelNames);
    case core::ParamId::quality: return table(core::kQualityNames);
    case core::ParamId::formantMode: return table(core::kFormantNames);
    default: return {nullptr, 0};
    }
}

}  // namespace

class QunePlugin : public Plugin {
public:
    QunePlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, 0)
    {
        core::activate(engine_, getSampleRate());
        applyConfiguration();
    }

protected:
    const char* getLabel() const override { return "Qune"; }

    const char* getDescription() const override
    {
        return "Pitch quantiser: detects the pitch of a voice or instrument and shifts it to the "
               "nearest note of a chosen scale, or to incoming MIDI notes. Three quality levels, "
               "optional formant preservation and glide.";
    }

    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR, DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('Q', 'u', 'n', 'e'); }

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

        const EnumTable names = enumFor(static_cast<core::ParamId>(index));
        if (names.count > 0)
        {
            // Labels are stored in a table owned by the plugin instance; DPF
            // reads them once during initialisation and keeps the pointer.
            auto* values = new ParameterEnumerationValue[names.count];
            for (std::size_t i = 0; i < names.count; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = names.names[i];
            }
            parameter.enumValues.count = static_cast<uint8_t>(names.count);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = true;
        }

        // The range selector is a stepper in the panel but a plain choice in a host.
        if (static_cast<core::ParamId>(index) == core::ParamId::range)
        {
            auto* values = new ParameterEnumerationValue[core::kRangeCount];
            for (std::size_t i = 0; i < core::kRangeCount; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kRanges[i].name;
            }
            parameter.enumValues.count = static_cast<uint8_t>(core::kRangeCount);
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = true;
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.*core::kParameterFields[index];

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::outPitch: return outPitch_;
        case core::ParamId::outTarget: return outTarget_;
        case core::ParamId::outShift: return outShift_;
        case core::ParamId::outLatency: return outLatency_;
        default: return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // outputs are read-only
        parameters_.*core::kParameterFields[index] = value;
        parameters_ = core::clampParameters(parameters_);
    }

    // The patch lives in parameters_, which activation never touches; only the
    // audio and MIDI state is cleared.
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

    void run(const float** inputs, float** outputs, const uint32_t frames, const MidiEvent* events,
             const uint32_t eventCount) override
    {
        uint32_t done = 0;
        for (uint32_t i = 0; i < eventCount; ++i)
        {
            const MidiEvent& event = events[i];
            const uint32_t at = std::min(std::max(event.frame, done), frames);
            if (at > done)
            {
                core::processBlock(engine_, parameters_, inputs[0] + done, inputs[1] + done, outputs[0] + done,
                                   outputs[1] + done, at - done);
                done = at;
            }
            if (event.size <= MidiEvent::kDataSize)
                core::midiEvent(engine_, parameters_, event.data, static_cast<int>(event.size));
        }
        if (done < frames)
        {
            core::processBlock(engine_, parameters_, inputs[0] + done, inputs[1] + done, outputs[0] + done,
                               outputs[1] + done, frames - done);
        }

        outPitch_ = core::detectedNote(engine_);
        outTarget_ = core::targetNote(engine_);
        outShift_ = core::shiftSemitones(engine_);
        reportLatency();
    }

private:
    void applyConfiguration()
    {
        core::configure(engine_, parameters_);
        reportLatency();
    }

    void reportLatency()
    {
        const int samples = core::latencySamples(engine_);
        outLatency_ = static_cast<float>(1000.0 * samples / getSampleRate());
        if (static_cast<uint32_t>(samples) != reportedLatency_)
        {
            reportedLatency_ = static_cast<uint32_t>(samples);
            setLatency(reportedLatency_);
        }
    }

    core::Parameters parameters_;
    core::EngineState engine_;
    uint32_t reportedLatency_ = 0xFFFFFFFFu;
    float outPitch_ = 0.0f;
    float outTarget_ = 0.0f;
    float outShift_ = 0.0f;
    float outLatency_ = 0.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QunePlugin)
};

Plugin* createPlugin()
{
    return new QunePlugin();
}

END_NAMESPACE_DISTRHO
