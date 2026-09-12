#include "DistrhoPlugin.hpp"

#include "magneto_engine.hpp"
#include "magneto_params.hpp"
#include "magneto_serialization.hpp"

#include <array>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

using CoreParameters = downspout::magneto::Parameters;
using CoreState = downspout::magneto::EngineState;
using CoreTransport = downspout::magneto::TransportSnapshot;

namespace core = downspout::magneto;

constexpr const char* kStateKeyParameters = "parameters";

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

// Address of each host-writable field, in ParamId order.
using FieldPointer = float CoreParameters::*;

constexpr std::array<FieldPointer, core::kInputParameterCount> kFields = {{
    &CoreParameters::cylinders,
    &CoreParameters::displacement,
    &CoreParameters::compression,
    &CoreParameters::ignition,
    &CoreParameters::asymmetry,
    &CoreParameters::blockGain,
    &CoreParameters::intakeLen,
    &CoreParameters::intakeGain,
    &CoreParameters::turbulence,
    &CoreParameters::extractorLen,
    &CoreParameters::pipeLen,
    &CoreParameters::mufflerLen,
    &CoreParameters::mufflerAction,
    &CoreParameters::outletLen,
    &CoreParameters::outletGain,
    &CoreParameters::backfire,
    &CoreParameters::rpm,
    &CoreParameters::throttle,
    &CoreParameters::rpmSource,
    &CoreParameters::syncRatio,
    &CoreParameters::idleRpm,
    &CoreParameters::inertia,
    &CoreParameters::seed,
    &CoreParameters::midiCh,
    &CoreParameters::listen,
    &CoreParameters::width,
    &CoreParameters::level,
}};

}  // namespace

class MagnetoPlugin : public Plugin {
public:
    MagnetoPlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, kStateCount)
    {
        parameters_ = core::clampParameters(parameters_);
        core::activate(engineState_, getSampleRate());
    }

protected:
    const char* getLabel() const override { return "Magneto"; }

    const char* getDescription() const override
    {
        return "Physically informed combustion engine: four-stroke cycle, per-cylinder "
               "waveguides, intake runners, extractors, muffler and tailpipe, driven by "
               "RPM and throttle.";
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

    int64_t getUniqueId() const override { return d_cconst('M', 'g', 'n', 't'); }

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

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::seed:
            // Re-seeding under automation would make the engine jump; keep it a
            // patch setting rather than an automatable control.
            parameter.hints = kParameterIsInteger;
            break;

        case core::ParamId::rpmSource:
        {
            static ParameterEnumerationValue values[] = {
                {0.0f, core::kRpmSourceNames[0]},
                {1.0f, core::kRpmSourceNames[1]},
            };
            parameter.enumValues.count = 2;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
            break;
        }

        case core::ParamId::syncRatio:
        {
            static ParameterEnumerationValue values[] = {
                {0.0f, core::kSyncRatioNames[0]}, {1.0f, core::kSyncRatioNames[1]},
                {2.0f, core::kSyncRatioNames[2]}, {3.0f, core::kSyncRatioNames[3]},
                {4.0f, core::kSyncRatioNames[4]}, {5.0f, core::kSyncRatioNames[5]},
                {6.0f, core::kSyncRatioNames[6]}, {7.0f, core::kSyncRatioNames[7]},
            };
            parameter.enumValues.count = 8;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
            break;
        }

        case core::ParamId::listen:
        {
            static ParameterEnumerationValue values[] = {
                {0.0f, core::kListenNames[0]}, {1.0f, core::kListenNames[1]},
                {2.0f, core::kListenNames[2]}, {3.0f, core::kListenNames[3]},
            };
            parameter.enumValues.count = 4;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
            break;
        }

        case core::ParamId::midiCh:
        {
            static ParameterEnumerationValue values[18];
            for (int i = 0; i < 18; ++i)
            {
                values[i].value = static_cast<float>(i);
                values[i].label = core::kMidiChannelNames[static_cast<std::size_t>(i)];
            }
            parameter.enumValues.count = 18;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
            parameter.enumValues.deleteLater = false;
            break;
        }

        default:
            break;
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
            parameters_ = *loaded;
    }

    float getParameterValue(const uint32_t index) const override
    {
        if (index < core::kInputParameterCount)
            return parameters_.*kFields[index];

        switch (static_cast<core::ParamId>(index))
        {
        case core::ParamId::outRpm: return core::currentRpm(engineState_);
        case core::ParamId::outBackfire: return core::backfireLamp(engineState_);
        default: return 0.0f;
        }
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index >= core::kInputParameterCount)
            return;  // output parameters are read-only
        parameters_.*kFields[index] = value;
        parameters_ = core::clampParameters(parameters_);
    }

    void activate() override { core::activate(engineState_, getSampleRate()); }

    void sampleRateChanged(const double newSampleRate) override
    {
        core::activate(engineState_, newSampleRate);
    }

    void run(const float**,
             float** outputs,
             const uint32_t frames,
             const MidiEvent* midiEvents,
             const uint32_t midiEventCount) override
    {
        handleMidi(midiEvents, midiEventCount);

        CoreTransport transport;
        {
            const TimePosition& pos = getTimePosition();
            transport.playing = pos.playing;
            if (pos.bbt.valid)
            {
                transport.valid = true;
                transport.bar = static_cast<double>(pos.bbt.bar) - 1.0;
                transport.barBeat = static_cast<double>(pos.bbt.beat) - 1.0
                                  + static_cast<double>(pos.bbt.tick)
                                        / static_cast<double>(pos.bbt.ticksPerBeat);
                transport.beatsPerBar = pos.bbt.beatsPerBar;
                transport.bpm = pos.bbt.beatsPerMinute;
            }
        }

        core::processBlock(engineState_, parameters_, transport, frames, getSampleRate(),
                           outputs[0], outputs[1]);
    }

private:
    // Incoming CC is written through to host parameters so the panel, the
    // automation lane and the controller never disagree about the value.
    void handleMidi(const MidiEvent* const events, const uint32_t count)
    {
        const int channelSetting = static_cast<int>(parameters_.midiCh + 0.5f);
        if (channelSetting == 0 || events == nullptr)
            return;

        for (uint32_t i = 0; i < count; ++i)
        {
            const MidiEvent& event = events[i];
            if (event.size < 3)
                continue;

            const uint8_t status = event.data[0];
            if ((status & 0xF0) != 0xB0)
                continue;

            const int channel = static_cast<int>(status & 0x0F) + 1;
            if (channelSetting <= 16 && channel != channelSetting)
                continue;

            applyController(event.data[1], event.data[2]);
        }
    }

    void applyController(const uint8_t controller, const uint8_t value)
    {
        const float normalized = static_cast<float>(value) / 127.0f;

        // DPF has no DSP-side path for pushing a parameter change back to the
        // host or panel, so a CC moves the engine but not the on-screen
        // control. The live RPM readout comes from the output parameters
        // instead, which is how the panel stays honest under MIDI control.
        const auto write = [this](const core::ParamId id, const float v) {
            setParameterValue(static_cast<uint32_t>(id), v);
        };

        switch (controller)
        {
        case core::kCcThrottle:
        case core::kCcThrottleAlt:
            write(core::ParamId::throttle, normalized);
            break;
        case core::kCcRpm:
        {
            const core::ParamSpec& spec =
                core::kParameterSpecs[static_cast<std::size_t>(core::ParamId::rpm)];
            write(core::ParamId::rpm, spec.minimum + normalized * (spec.maximum - spec.minimum));
            break;
        }
        case core::kCcLevel:
            write(core::ParamId::level, normalized);
            break;
        default:
            break;
        }
    }

    CoreParameters parameters_;
    CoreState engineState_;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagnetoPlugin)
};

Plugin* createPlugin()
{
    return new MagnetoPlugin();
}

END_NAMESPACE_DISTRHO
