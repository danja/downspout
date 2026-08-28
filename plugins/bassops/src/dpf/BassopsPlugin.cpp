#include "DistrhoPlugin.hpp"

#include "bassops_engine.hpp"
#include "bassops_serialization.hpp"

#include <cmath>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    // Automatable controls
    kParamDuckDepth = 0,
    kParamAttackMs,
    kParamReleaseMs,
    kParamCutoffHz,
    kParamSideShape,
    kParamWet,
    // Output meters (read-only, written by DSP)
    kParamInputLevel,
    kParamScLevel,
    kParamDuckGain,
    kParamOutputLevel,
    // Bus control parameters
    kParamMidiEnabled,
    kParamControlChannel,
    kParamRequireGate,
    kParamResetMidi,
    // Bus status (read-only, written by DSP)
    kParamStatusProducer,
    kParamStatusDuckMidi,
    kParamStatusWetMidi,
    kParamStatusDuckDepth,
    kParamStatusWet,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

constexpr const char* kStateKeyParameters = "parameters";

using CoreAudioBlock  = downspout::bassops::AudioBlock;
using CoreEngineState = downspout::bassops::EngineState;
using CoreParameters  = downspout::bassops::Parameters;
using CoreBusParams   = downspout::bassops::BusParameters;

std::string serializeBusConfig(bool midiEnabled, int controlChannel, bool requireGate)
{
    return "midiEnabled=" + std::to_string(midiEnabled ? 1 : 0) + "\n"
           "controlChannel=" + std::to_string(controlChannel) + "\n"
           "requireGate=" + std::to_string(requireGate ? 1 : 0) + "\n";
}

void parseBusConfig(const std::string& text, bool& midiEnabled, int& controlChannel, bool& requireGate)
{
    std::size_t pos = 0;
    while (pos < text.size()) {
        const auto nl  = text.find('\n', pos);
        const auto end = nl == std::string::npos ? text.size() : nl;
        const auto sep = text.find('=', pos);
        if (sep != std::string::npos && sep < end) {
            const auto key = text.substr(pos, sep - pos);
            const auto val = text.substr(sep + 1, end - sep - 1);
            if (!val.empty()) {
                if (key == "midiEnabled")    midiEnabled    = std::stoi(val) != 0;
                if (key == "controlChannel") controlChannel = std::stoi(val);
                if (key == "requireGate")    requireGate    = std::stoi(val) != 0;
            }
        }
        pos = end == text.size() ? end : end + 1;
    }
}

}  // namespace

class BassopsPlugin : public Plugin
{
public:
    BassopsPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::bassops::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override       { return "Bassops"; }
    const char* getDescription() const override { return "Sidechain ducker with mid/side EQ."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('B', 's', 'O', 'p');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);

        if (!input) {
            port.groupId = kPortGroupStereo;
            port.name   = index == 0 ? "Output L" : "Output R";
            port.symbol = index == 0 ? "out_l"    : "out_r";
            return;
        }

        if (index < 2) {
            port.groupId = kPortGroupStereo;
            port.name   = index == 0 ? "Main L" : "Main R";
            port.symbol = index == 0 ? "main_l" : "main_r";
        } else {
            port.groupId = 100;
            port.name   = index == 2 ? "Sidechain L" : "Sidechain R";
            port.symbol = index == 2 ? "sc_l"        : "sc_r";
        }
    }

    void initPortGroup(const uint32_t groupId, PortGroup& portGroup) override
    {
        if (groupId == 100) {
            portGroup.name   = "Sidechain";
            portGroup.symbol = "sidechain";
        }
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        switch (index)
        {
        case kParamDuckDepth:
            parameter.hints         = kParameterIsAutomatable;
            parameter.name          = "Duck Depth";
            parameter.symbol        = "duck_depth";
            parameter.unit          = "%";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 100.0f;
            parameter.ranges.def    = 80.0f;
            break;
        case kParamAttackMs:
            parameter.hints         = kParameterIsAutomatable;
            parameter.name          = "Attack";
            parameter.symbol        = "attack_ms";
            parameter.unit          = "ms";
            parameter.ranges.min    = 1.0f;
            parameter.ranges.max    = 500.0f;
            parameter.ranges.def    = 10.0f;
            break;
        case kParamReleaseMs:
            parameter.hints         = kParameterIsAutomatable;
            parameter.name          = "Release";
            parameter.symbol        = "release_ms";
            parameter.unit          = "ms";
            parameter.ranges.min    = 10.0f;
            parameter.ranges.max    = 2000.0f;
            parameter.ranges.def    = 100.0f;
            break;
        case kParamCutoffHz:
            parameter.hints         = kParameterIsAutomatable;
            parameter.name          = "M/S Cutoff";
            parameter.symbol        = "cutoff_hz";
            parameter.unit          = "Hz";
            parameter.ranges.min    = 50.0f;
            parameter.ranges.max    = 5000.0f;
            parameter.ranges.def    = 200.0f;
            break;
        case kParamSideShape:
            parameter.hints         = kParameterIsAutomatable;
            parameter.name          = "Side Shape";
            parameter.symbol        = "side_shape";
            parameter.unit          = "%";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 100.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamWet:
            parameter.hints         = kParameterIsAutomatable;
            parameter.name          = "Wet";
            parameter.symbol        = "wet";
            parameter.unit          = "%";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 100.0f;
            parameter.ranges.def    = 100.0f;
            break;
        case kParamInputLevel:
            parameter.hints         = kParameterIsOutput;
            parameter.name          = "Input Level";
            parameter.symbol        = "input_level";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamScLevel:
            parameter.hints         = kParameterIsOutput;
            parameter.name          = "Sidechain Level";
            parameter.symbol        = "sc_level";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamDuckGain:
            parameter.hints         = kParameterIsOutput;
            parameter.name          = "Duck Gain";
            parameter.symbol        = "duck_gain";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 1.0f;
            break;
        case kParamOutputLevel:
            parameter.hints         = kParameterIsOutput;
            parameter.name          = "Output Level";
            parameter.symbol        = "output_level";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamMidiEnabled:
            parameter.hints         = kParameterIsAutomatable | kParameterIsBoolean;
            parameter.name          = "MIDI Enabled";
            parameter.symbol        = "midi_enabled";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamControlChannel:
            parameter.hints         = kParameterIsAutomatable | kParameterIsInteger;
            parameter.name          = "Control Channel";
            parameter.symbol        = "control_channel";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 16.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamRequireGate:
            parameter.hints         = kParameterIsAutomatable | kParameterIsBoolean;
            parameter.name          = "Require Gate";
            parameter.symbol        = "require_gate";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamResetMidi:
            parameter.hints         = kParameterIsAutomatable | kParameterIsTrigger;
            parameter.name          = "Reset MIDI";
            parameter.symbol        = "reset_midi";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamStatusProducer:
            parameter.hints         = kParameterIsOutput | kParameterIsBoolean;
            parameter.name          = "Bus Active";
            parameter.symbol        = "status_producer";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamStatusDuckMidi:
            parameter.hints         = kParameterIsOutput | kParameterIsBoolean;
            parameter.name          = "Duck MIDI Active";
            parameter.symbol        = "status_duck_midi";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamStatusWetMidi:
            parameter.hints         = kParameterIsOutput | kParameterIsBoolean;
            parameter.name          = "Wet MIDI Active";
            parameter.symbol        = "status_wet_midi";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 1.0f;
            parameter.ranges.def    = 0.0f;
            break;
        case kParamStatusDuckDepth:
            parameter.hints         = kParameterIsOutput;
            parameter.name          = "Effective Duck Depth";
            parameter.symbol        = "status_duck_depth";
            parameter.unit          = "%";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 100.0f;
            parameter.ranges.def    = 80.0f;
            break;
        case kParamStatusWet:
            parameter.hints         = kParameterIsOutput;
            parameter.name          = "Effective Wet";
            parameter.symbol        = "status_wet";
            parameter.unit          = "%";
            parameter.ranges.min    = 0.0f;
            parameter.ranges.max    = 100.0f;
            parameter.ranges.def    = 100.0f;
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStateParameters) {
            state.key          = kStateKeyParameters;
            state.label        = "Parameters";
            state.hints        = kStateIsOnlyForDSP;
            state.defaultValue = "";
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index)
        {
        case kParamDuckDepth:       return parameters_.duckDepth;
        case kParamAttackMs:        return parameters_.attackMs;
        case kParamReleaseMs:       return parameters_.releaseMs;
        case kParamCutoffHz:        return parameters_.cutoffHz;
        case kParamSideShape:       return parameters_.sideShape;
        case kParamWet:             return parameters_.wet;
        case kParamInputLevel:      return engineState_.meters.inputPeak;
        case kParamScLevel:         return engineState_.meters.scLevel;
        case kParamDuckGain:        return engineState_.meters.duckGain;
        case kParamOutputLevel:     return engineState_.meters.outputPeak;
        case kParamMidiEnabled:     return midiEnabled_    ? 1.0f : 0.0f;
        case kParamControlChannel:  return static_cast<float>(controlChannel_);
        case kParamRequireGate:     return requireGate_    ? 1.0f : 0.0f;
        case kParamResetMidi:       return 0.0f;
        case kParamStatusProducer:  return engineState_.bus.producerActive ? 1.0f : 0.0f;
        case kParamStatusDuckMidi:  return engineState_.bus.duckMidiActive ? 1.0f : 0.0f;
        case kParamStatusWetMidi:   return engineState_.bus.wetMidiActive  ? 1.0f : 0.0f;
        case kParamStatusDuckDepth: return downspout::bassops::effectiveDuckDepth(engineState_, parameters_);
        case kParamStatusWet:       return downspout::bassops::effectiveWet(engineState_, parameters_);
        default:                    return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index)
        {
        case kParamDuckDepth:      parameters_.duckDepth = value; break;
        case kParamAttackMs:       parameters_.attackMs  = value; break;
        case kParamReleaseMs:      parameters_.releaseMs = value; break;
        case kParamCutoffHz:       parameters_.cutoffHz  = value; break;
        case kParamSideShape:      parameters_.sideShape = value; break;
        case kParamWet:            parameters_.wet       = value; break;
        case kParamMidiEnabled:    midiEnabled_    = value >= 0.5f; break;
        case kParamControlChannel: controlChannel_ = static_cast<int>(std::lround(value)); break;
        case kParamRequireGate:    requireGate_    = value >= 0.5f; break;
        case kParamResetMidi:
            if (value >= 0.5f) {
                engineState_.bus.duckMidiActive = false;
                engineState_.bus.wetMidiActive  = false;
            }
            break;
        default: break;
        }
        if (index <= kParamWet) {
            parameters_ = downspout::bassops::clampParameters(parameters_);
        }
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0) {
            std::string s = downspout::bassops::serializeParameters(parameters_);
            s += serializeBusConfig(midiEnabled_, controlChannel_, requireGate_);
            return String(s.c_str());
        }
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0) { return; }
        const std::string text = value != nullptr ? value : "";
        const auto p = downspout::bassops::deserializeParameters(text);
        if (p.has_value()) { parameters_ = *p; }
        parseBusConfig(text, midiEnabled_, controlChannel_, requireGate_);
    }

    void activate() override
    {
        downspout::bassops::activate(engineState_);
        setLatency(downspout::bassops::kLatencyFrames);
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const CoreBusParams busParams { midiEnabled_, controlChannel_, requireGate_ };

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const MidiEvent& ev = midiEvents[i];
            if (ev.size < 3) continue;
            downspout::bassops::MidiControlEvent cev;
            cev.frame   = ev.frame;
            cev.size    = static_cast<std::uint8_t>(ev.size);
            cev.data[0] = ev.data[0];
            cev.data[1] = ev.data[1];
            cev.data[2] = ev.data[2];
            downspout::bassops::handleMidi(engineState_, busParams, cev);
        }

        CoreAudioBlock audio;
        audio.mainL    = inputs[0];
        audio.mainR    = inputs[1];
        audio.controlL = inputs[2];
        audio.controlR = inputs[3];
        audio.outL     = outputs[0];
        audio.outR     = outputs[1];

        downspout::bassops::processBlock(engineState_, parameters_, busParams, frames, getSampleRate(), audio);
    }

private:
    CoreParameters  parameters_ {};
    CoreEngineState engineState_ {};
    bool            midiEnabled_    = false;
    int             controlChannel_ = 0;
    bool            requireGate_    = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BassopsPlugin)
};

Plugin* createPlugin()
{
    return new BassopsPlugin();
}

END_NAMESPACE_DISTRHO
