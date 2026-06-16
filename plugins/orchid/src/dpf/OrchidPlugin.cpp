#include "DistrhoPlugin.hpp"

#include "orchid_engine.hpp"
#include "orchid_params.hpp"
#include "orchid_serialization.hpp"

#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

using CoreAudioBlock = downspout::orchid::AudioBlock;
using CoreEngineState = downspout::orchid::EngineState;
using CoreOutputStatus = downspout::orchid::OutputStatus;
using CoreParameters = downspout::orchid::Parameters;
using CoreTransport = downspout::orchid::TransportSnapshot;

using downspout::orchid::kParamCooldownMs;
using downspout::orchid::kParamGrid;
using downspout::orchid::kParamHoldUnits;
using downspout::orchid::kParamLiveUnder;
using downspout::orchid::kParamLoopPeriods;
using downspout::orchid::kParamMix;
using downspout::orchid::kParamPeriodicity;
using downspout::orchid::kParamPitchHigh;
using downspout::orchid::kParamPitchLow;
using downspout::orchid::kParamReleaseMs;
using downspout::orchid::kParamSensitivity;
using downspout::orchid::kParamStabilityMs;
using downspout::orchid::kParamStatusConfidence;
using downspout::orchid::kParamStatusHoldProgress;
using downspout::orchid::kParamStatusPitch;
using downspout::orchid::kParamStatusRms;
using downspout::orchid::kParamStatusState;
using downspout::orchid::kParamStatusTransport;
using downspout::orchid::kParameterCount;
using downspout::orchid::kProcessorStateNames;
using downspout::orchid::kStateCount;
using downspout::orchid::kStateKeyParameters;
using downspout::orchid::kStateParameters;

ParameterEnumerationValue kStateEnumValues[] = {
    {0.0f, "Pass"},
    {1.0f, "Armed"},
    {2.0f, "Held"},
    {3.0f, "Release"},
    {4.0f, "Cooldown"},
};

constexpr std::uint32_t kWrapperChannelCount =
    DISTRHO_PLUGIN_NUM_INPUTS < DISTRHO_PLUGIN_NUM_OUTPUTS ? DISTRHO_PLUGIN_NUM_INPUTS : DISTRHO_PLUGIN_NUM_OUTPUTS;

CoreTransport toCoreTransport(const TimePosition& timePos)
{
    CoreTransport transport;
    transport.playing = timePos.playing;
    transport.valid = timePos.bbt.valid;

    if (timePos.bbt.valid)
    {
        transport.bar = static_cast<double>(timePos.bbt.bar - 1);
        transport.barBeat = static_cast<double>(timePos.bbt.beat - 1)
            + (timePos.bbt.ticksPerBeat > 0.0 ? (timePos.bbt.tick / timePos.bbt.ticksPerBeat) : 0.0);
        transport.beatsPerBar = timePos.bbt.beatsPerBar;
        transport.beatType = timePos.bbt.beatType;
        transport.bpm = timePos.bbt.beatsPerMinute;
        transport.meter = downspout::meterFromTimeSignature(timePos.bbt.beatsPerBar, timePos.bbt.beatType);
    }

    return transport;
}

}  // namespace

class OrchidPlugin : public Plugin
{
public:
    OrchidPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::orchid::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override
    {
        return "Orchid";
    }

    const char* getDescription() const override
    {
        return "Transport-aware voiced audio freeze effect.";
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
        return d_cconst('O', 'r', 'c', 'h');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);

        if (index < 2)
            port.groupId = kPortGroupStereo;
        port.name = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;

        switch (index)
        {
        case kParamSensitivity:
            parameter.name = "Sensitivity";
            parameter.symbol = "sensitivity";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 62.0f;
            break;
        case kParamPeriodicity:
            parameter.name = "Periodicity";
            parameter.symbol = "periodicity";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 72.0f;
            break;
        case kParamStabilityMs:
            parameter.name = "Stability";
            parameter.symbol = "stability_ms";
            parameter.ranges.min = 5.0f;
            parameter.ranges.max = 250.0f;
            parameter.ranges.def = 55.0f;
            break;
        case kParamPitchLow:
            parameter.name = "Pitch Low";
            parameter.symbol = "pitch_low";
            parameter.ranges.min = 40.0f;
            parameter.ranges.max = 800.0f;
            parameter.ranges.def = 80.0f;
            break;
        case kParamPitchHigh:
            parameter.name = "Pitch High";
            parameter.symbol = "pitch_high";
            parameter.ranges.min = 120.0f;
            parameter.ranges.max = 2000.0f;
            parameter.ranges.def = 900.0f;
            break;
        case kParamGrid:
            parameter.name = "Grid";
            parameter.symbol = "grid";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 8.0f;
            break;
        case kParamHoldUnits:
            parameter.name = "Hold";
            parameter.symbol = "hold_units";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 1.0f;
            parameter.ranges.max = 8.0f;
            parameter.ranges.def = 2.0f;
            break;
        case kParamReleaseMs:
            parameter.name = "Release";
            parameter.symbol = "release_ms";
            parameter.ranges.min = 2.0f;
            parameter.ranges.max = 500.0f;
            parameter.ranges.def = 35.0f;
            break;
        case kParamCooldownMs:
            parameter.name = "Cooldown";
            parameter.symbol = "cooldown_ms";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1000.0f;
            parameter.ranges.def = 80.0f;
            break;
        case kParamLoopPeriods:
            parameter.name = "Loop Periods";
            parameter.symbol = "loop_periods";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 2.0f;
            parameter.ranges.max = 16.0f;
            parameter.ranges.def = 5.0f;
            break;
        case kParamMix:
            parameter.name = "Mix";
            parameter.symbol = "mix";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 82.0f;
            break;
        case kParamLiveUnder:
            parameter.name = "Live Under";
            parameter.symbol = "live_under";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 100.0f;
            parameter.ranges.def = 18.0f;
            break;
        case kParamStatusState:
            parameter.name = "State";
            parameter.symbol = "status_state";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = static_cast<float>(kProcessorStateNames.size() - 1u);
            parameter.ranges.def = 0.0f;
            parameter.enumValues.count = static_cast<uint8_t>(std::size(kStateEnumValues));
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = kStateEnumValues;
            parameter.enumValues.deleteLater = false;
            break;
        case kParamStatusConfidence:
            parameter.name = "Confidence";
            parameter.symbol = "status_confidence";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusPitch:
            parameter.name = "Detected Pitch";
            parameter.symbol = "status_pitch";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 2000.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusRms:
            parameter.name = "Input RMS";
            parameter.symbol = "status_rms";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusHoldProgress:
            parameter.name = "Hold Progress";
            parameter.symbol = "status_hold_progress";
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        case kParamStatusTransport:
            parameter.name = "Transport";
            parameter.symbol = "status_transport";
            parameter.hints = kParameterIsOutput | kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStateParameters)
        {
            state.key = kStateKeyParameters;
            state.label = "Parameters";
            state.hints = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamSensitivity: return parameters_.sensitivity;
        case kParamPeriodicity: return parameters_.periodicity;
        case kParamStabilityMs: return parameters_.stabilityMs;
        case kParamPitchLow: return parameters_.pitchLow;
        case kParamPitchHigh: return parameters_.pitchHigh;
        case kParamGrid: return parameters_.grid;
        case kParamHoldUnits: return parameters_.holdUnits;
        case kParamReleaseMs: return parameters_.releaseMs;
        case kParamCooldownMs: return parameters_.cooldownMs;
        case kParamLoopPeriods: return parameters_.loopPeriods;
        case kParamMix: return parameters_.mix;
        case kParamLiveUnder: return parameters_.liveUnder;
        case kParamStatusState: return static_cast<float>(status_.state);
        case kParamStatusConfidence: return status_.detectorConfidence;
        case kParamStatusPitch: return status_.detectedPitchHz;
        case kParamStatusRms: return status_.inputRms;
        case kParamStatusHoldProgress: return status_.holdProgress;
        case kParamStatusTransport: return status_.transportUsable;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamSensitivity: parameters_.sensitivity = value; break;
        case kParamPeriodicity: parameters_.periodicity = value; break;
        case kParamStabilityMs: parameters_.stabilityMs = value; break;
        case kParamPitchLow: parameters_.pitchLow = value; break;
        case kParamPitchHigh: parameters_.pitchHigh = value; break;
        case kParamGrid: parameters_.grid = value; break;
        case kParamHoldUnits: parameters_.holdUnits = value; break;
        case kParamReleaseMs: parameters_.releaseMs = value; break;
        case kParamCooldownMs: parameters_.cooldownMs = value; break;
        case kParamLoopPeriods: parameters_.loopPeriods = value; break;
        case kParamMix: parameters_.mix = value; break;
        case kParamLiveUnder: parameters_.liveUnder = value; break;
        default: break;
        }

        parameters_ = downspout::orchid::clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::orchid::serializeParameters(parameters_).c_str());

        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0)
            return;

        const std::string text = value != nullptr ? value : "";
        const auto parameters = downspout::orchid::deserializeParameters(text);
        if (parameters.has_value())
            parameters_ = *parameters;
    }

    void activate() override
    {
        downspout::orchid::activate(engineState_, getSampleRate(), kWrapperChannelCount);
        status_ = {};
    }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        std::array<const float*, downspout::orchid::kMaxChannels> safeInputs {};
        std::array<float*, downspout::orchid::kMaxChannels> safeOutputs {};

        for (uint32_t channel = 0; channel < kWrapperChannelCount; ++channel)
        {
            safeInputs[channel] = inputs[channel];
            safeOutputs[channel] = outputs[channel];
        }

        CoreAudioBlock audio;
        audio.inputs = safeInputs;
        audio.outputs = safeOutputs;
        audio.channelCount = kWrapperChannelCount;

        status_ = downspout::orchid::processBlock(engineState_,
                                                  parameters_,
                                                  toCoreTransport(getTimePosition()),
                                                  frames,
                                                  getSampleRate(),
                                                  audio);
    }

private:
    CoreParameters parameters_ {};
    CoreEngineState engineState_ {};
    CoreOutputStatus status_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OrchidPlugin)
};

Plugin* createPlugin()
{
    return new OrchidPlugin();
}

END_NAMESPACE_DISTRHO
