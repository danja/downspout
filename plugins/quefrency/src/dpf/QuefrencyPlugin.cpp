#include "DistrhoPlugin.hpp"

#include "quefrency_core.hpp"
#include "quefrency_params.hpp"

#include <array>
#include <cstring>
#include <optional>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using CoreParameters = downspout::quefrency::Parameters;
using CoreState = downspout::quefrency::EngineState;
namespace core = downspout::quefrency;

constexpr const char* kStateKeyParameters = "parameters";

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

using FieldPointer = float CoreParameters::*;

constexpr std::array<FieldPointer, core::kInputParameterCount> kFields = {{
    &CoreParameters::formantShift,
    &CoreParameters::formantDepth,
    &CoreParameters::formantTilt,
    &CoreParameters::pitchShift,
    &CoreParameters::pitchFine,
    &CoreParameters::freqShift,
    &CoreParameters::harmonicDepth,
    &CoreParameters::lifter,
    &CoreParameters::estimator,
    &CoreParameters::mix,
    &CoreParameters::output,
}};

}  // namespace

class QuefrencyPlugin : public Plugin {
public:
    QuefrencyPlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, kStateCount)
    {
        parameters_ = core::clampParameters(parameters_);
        core::activate(engineState_, getSampleRate());
        pushAllParameters();
        setLatency(static_cast<uint32_t>(core::currentLatencySamples(engineState_)));
    }

protected:
    const char* getLabel() const override { return "Quefrency"; }

    const char* getDescription() const override
    {
        return "Cepstral formant and harmonic shifter: envelope and excitation "
               "transformed independently, reported latency compensated by the host.";
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

    int64_t getUniqueId() const override { return d_cconst('Q', 'u', 'F', 'r'); }

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

        if (static_cast<core::ParamId>(index) == core::ParamId::estimator)
        {
            static ParameterEnumerationValue values[] = {
                {0.0f, core::kEstimatorNames[0]},
                {1.0f, core::kEstimatorNames[1]},
            };
            parameter.enumValues.count = 2;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
        }
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index != kStateParameters)
            return;
        state.key = kStateKeyParameters;
        state.label = "Parameters";
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(core::serializeParameters(parameters_).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;
        const std::string text = (value != nullptr) ? value : "";
        if (const auto loaded = core::deserializeParameters(text))
        {
            parameters_ = *loaded;
            ccOverride_.fill(std::nullopt);
            pushAllParameters();
        }
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.*kFields[index];
        if (static_cast<core::ParamId>(index) == core::ParamId::outLatency)
            return core::currentLatencySamples(engineState_);
        return 0.0f;
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;
        parameters_.*kFields[index] = value;
        parameters_ = core::clampParameters(parameters_);
        // Last-wins: a host write clears any controller override, as in the
        // source, where the host's value wins once it changes.
        ccOverride_[index] = std::nullopt;
        core::setParameter(engineState_, index, parameters_.*kFields[index]);
    }

    void activate() override
    {
        core::activate(engineState_, getSampleRate());
        pushAllParameters();
        setLatency(static_cast<uint32_t>(core::currentLatencySamples(engineState_)));
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::activate(engineState_, newSampleRate);
        pushAllParameters();
        // Frame size (and therefore latency) follows the rate: 2048 at
        // 50 kHz and below, 4096 above. Re-report so the host recompensates.
        setLatency(static_cast<uint32_t>(core::currentLatencySamples(engineState_)));
    }

    void run(const float** inputs, float** outputs, const uint32_t frames,
             const MidiEvent* midiEvents, const uint32_t midiEventCount) override
    {
        handleMidi(midiEvents, midiEventCount);
        core::processBlock(engineState_, frames, inputs, outputs);
    }

private:
    void pushAllParameters()
    {
        for (std::uint32_t i = 0; i < core::kInputParameterCount; ++i)
            core::setParameter(engineState_, i, parameters_.*kFields[i]);
        for (std::uint32_t i = 0; i < core::kInputParameterCount; ++i)
        {
            if (ccOverride_[i].has_value())
                core::setParameter(engineState_, i, *ccOverride_[i]);
        }
    }

    // DPF MIDI events carry no sample offset, so a block's controllers apply
    // at the block start (the source applies them at their own frame; its
    // spectral parameters only take effect at the next hop anyway).
    // Overrides hold until the host writes the same parameter (last-wins).
    void handleMidi(const MidiEvent* const events, const uint32_t count)
    {
        if (events == nullptr)
            return;
        for (uint32_t i = 0; i < count; ++i)
        {
            const MidiEvent& event = events[i];
            if (event.size < 3)
                continue;
            if ((event.data[0] & 0xF0) != 0xB0)
                continue;
            core::ParamId target = core::ParamId::mix;
            if (!core::controllerTarget(event.data[1], target))
                continue;
            const auto index = static_cast<std::uint32_t>(target);
            float value = core::controllerToParameter(target, event.data[2]);
            if (target == core::ParamId::estimator)
                value = value >= 0.5f ? 1.0f : 0.0f;
            ccOverride_[index] = value;
            core::setParameter(engineState_, index, value);
        }
    }

    CoreParameters parameters_;
    CoreState engineState_;
    std::array<std::optional<float>, core::kInputParameterCount> ccOverride_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QuefrencyPlugin)
};

Plugin* createPlugin()
{
    return new QuefrencyPlugin();
}

END_NAMESPACE_DISTRHO
