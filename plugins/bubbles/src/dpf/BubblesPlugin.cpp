#include "DistrhoPlugin.hpp"

#include "bubbles_engine.hpp"
#include "bubbles_serialization.hpp"

#include <algorithm>
#include <array>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamFlow,
    kParamTurbulence,
    kParamSize,
    kParamDensity,
    kParamHeat,
    kParamDepth,
    kParamBrightness,
    kParamResonance,
    kParamRandomness,
    kParamSpace,
    kParamDrive,
    kParamOutput,
    kParamConductorCh,
    kParamLfoTarget,
    kParamLfoShape,
    kParamLfoRate,
    kParamLfoDepth,
    kParamLfoSync,
    kParamLfoDivision,
    kParameterCount
};

enum StateIndex : uint32_t {
    kStateParameters = 0,
    kStateCount
};

constexpr const char* kStateKeyParameters = "parameters";

using CoreEngineState = downspout::bubbles::EngineState;
using CoreParameters  = downspout::bubbles::Parameters;
using CoreTransport   = downspout::bubbles::TransportSnapshot;
using CoreMidiEvent   = downspout::bubbles::InputMidiEvent;

constexpr std::size_t kMaxMidiEventsPerBlock = 512;

CoreMidiEvent toCoreMidiEvent(const MidiEvent& ev)
{
    CoreMidiEvent m;
    m.frame = ev.frame;
    const uint8_t* src = ev.size > MidiEvent::kDataSize ? ev.dataExt : ev.data;
    m.data[0] = ev.size > 0 ? src[0] : 0;
    m.data[1] = ev.size > 1 ? src[1] : 0;
    m.data[2] = ev.size > 2 ? src[2] : 0;
    return m;
}

}  // namespace

class BubblesPlugin : public Plugin
{
public:
    BubblesPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::bubbles::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override       { return "Bubbles"; }
    const char* getDescription() const override { return "Water sound generator: streams, rivers, ocean waves, bubbles and drips."; }
    const char* getMaker() const override       { return "danja"; }
    const char* getHomePage() const override    { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override     { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override { return d_cconst('B', 'b', 'W', 't'); }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        parameter.hints = kParameterIsAutomatable;
        switch (index) {
        case kParamMode:
            parameter.name   = "Mode";
            parameter.symbol = "mode";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 6.0f;
            parameter.ranges.def = 6.0f;
            parameter.enumValues.count = 7;
            parameter.enumValues.restrictedMode = true;
            {
                static ParameterEnumerationValue modeValues[] = {
                    {0.0f, "Stream"},
                    {1.0f, "River"},
                    {2.0f, "Ocean"},
                    {3.0f, "Bubbles"},
                    {4.0f, "Drips"},
                    {5.0f, "Rain"},
                    {6.0f, "Custom"},
                };
                parameter.enumValues.values = modeValues;
                parameter.enumValues.deleteLater = false;
            }
            break;
        case kParamFlow:
            parameter.name   = "Flow";
            parameter.symbol = "flow";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.50f;
            break;
        case kParamTurbulence:
            parameter.name   = "Turbulence";
            parameter.symbol = "turbulence";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.40f;
            break;
        case kParamSize:
            parameter.name   = "Size";
            parameter.symbol = "size";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.45f;
            break;
        case kParamDensity:
            parameter.name   = "Density";
            parameter.symbol = "density";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.50f;
            break;
        case kParamHeat:
            parameter.name   = "Heat";
            parameter.symbol = "heat";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.30f;
            break;
        case kParamDepth:
            parameter.name   = "Depth";
            parameter.symbol = "depth";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.20f;
            break;
        case kParamBrightness:
            parameter.name   = "Brightness";
            parameter.symbol = "brightness";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.55f;
            break;
        case kParamResonance:
            parameter.name   = "Resonance";
            parameter.symbol = "resonance";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.55f;
            break;
        case kParamRandomness:
            parameter.name   = "Randomness";
            parameter.symbol = "randomness";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.40f;
            break;
        case kParamSpace:
            parameter.name   = "Space";
            parameter.symbol = "space";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.35f;
            break;
        case kParamDrive:
            parameter.name   = "Drive";
            parameter.symbol = "drive";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.35f;
            break;
        case kParamOutput:
            parameter.name   = "Output";
            parameter.symbol = "output";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.80f;
            break;
        case kParamConductorCh:
            parameter.name   = "Conductor Ch";
            parameter.symbol = "conductor_ch";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f; parameter.ranges.max = 16.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamLfoTarget:
            parameter.name   = "LFO Target";
            parameter.symbol = "lfo_target";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f; parameter.ranges.max = 6.0f; parameter.ranges.def = 0.0f;
            parameter.enumValues.count = 7;
            parameter.enumValues.restrictedMode = true;
            {
                static ParameterEnumerationValue lfoTargetValues[] = {
                    {0.0f, "Off"},
                    {1.0f, "Flow"},
                    {2.0f, "Density"},
                    {3.0f, "Brightness"},
                    {4.0f, "Size"},
                    {5.0f, "Heat"},
                    {6.0f, "Randomness"},
                };
                parameter.enumValues.values = lfoTargetValues;
                parameter.enumValues.deleteLater = false;
            }
            break;
        case kParamLfoShape:
            parameter.name   = "LFO Shape";
            parameter.symbol = "lfo_shape";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f; parameter.ranges.max = 4.0f; parameter.ranges.def = 0.0f;
            parameter.enumValues.count = 5;
            parameter.enumValues.restrictedMode = true;
            {
                static ParameterEnumerationValue lfoShapeValues[] = {
                    {0.0f, "Sine"},
                    {1.0f, "Triangle"},
                    {2.0f, "Ramp Down"},
                    {3.0f, "Ramp Up"},
                    {4.0f, "Square"},
                };
                parameter.enumValues.values = lfoShapeValues;
                parameter.enumValues.deleteLater = false;
            }
            break;
        case kParamLfoRate:
            parameter.name   = "LFO Rate";
            parameter.symbol = "lfo_rate";
            parameter.unit   = "Hz";
            parameter.ranges.min = 0.05f; parameter.ranges.max = 20.0f; parameter.ranges.def = 0.5f;
            break;
        case kParamLfoDepth:
            parameter.name   = "LFO Depth";
            parameter.symbol = "lfo_depth";
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamLfoSync:
            parameter.name   = "LFO Sync";
            parameter.symbol = "lfo_sync";
            parameter.hints |= kParameterIsBoolean;
            parameter.ranges.min = 0.0f; parameter.ranges.max = 1.0f; parameter.ranges.def = 0.0f;
            break;
        case kParamLfoDivision:
            parameter.name   = "LFO Division";
            parameter.symbol = "lfo_division";
            parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f; parameter.ranges.max = 7.0f; parameter.ranges.def = 2.0f;
            parameter.enumValues.count = 8;
            parameter.enumValues.restrictedMode = true;
            {
                static ParameterEnumerationValue lfoDivValues[] = {
                    {0.0f, "1/1"},
                    {1.0f, "1/2"},
                    {2.0f, "1/4"},
                    {3.0f, "1/8"},
                    {4.0f, "1/8T"},
                    {5.0f, "1/16"},
                    {6.0f, "1/16T"},
                    {7.0f, "1/32"},
                };
                parameter.enumValues.values = lfoDivValues;
                parameter.enumValues.deleteLater = false;
            }
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
        switch (index) {
        case kParamMode:        return parameters_.mode;
        case kParamFlow:        return parameters_.flow;
        case kParamTurbulence:  return parameters_.turbulence;
        case kParamSize:        return parameters_.size;
        case kParamDensity:     return parameters_.density;
        case kParamHeat:        return parameters_.heat;
        case kParamDepth:       return parameters_.depth;
        case kParamBrightness:  return parameters_.brightness;
        case kParamResonance:   return parameters_.resonance;
        case kParamRandomness:  return parameters_.randomness;
        case kParamSpace:       return parameters_.space;
        case kParamDrive:       return parameters_.drive;
        case kParamOutput:      return parameters_.output;
        case kParamConductorCh: return parameters_.conductorCh;
        case kParamLfoTarget:   return parameters_.lfoTarget;
        case kParamLfoShape:    return parameters_.lfoShape;
        case kParamLfoRate:     return parameters_.lfoRate;
        case kParamLfoDepth:    return parameters_.lfoDepth;
        case kParamLfoSync:     return parameters_.lfoSync;
        case kParamLfoDivision: return parameters_.lfoDivision;
        default:                return 0.0f;
        }
    }

    void setParameterValue(uint32_t index, float value) override
    {
        switch (index) {
        case kParamMode:        parameters_.mode        = value; break;
        case kParamFlow:        parameters_.flow        = value; break;
        case kParamTurbulence:  parameters_.turbulence  = value; break;
        case kParamSize:        parameters_.size        = value; break;
        case kParamDensity:     parameters_.density     = value; break;
        case kParamHeat:        parameters_.heat        = value; break;
        case kParamDepth:       parameters_.depth       = value; break;
        case kParamBrightness:  parameters_.brightness  = value; break;
        case kParamResonance:   parameters_.resonance   = value; break;
        case kParamRandomness:  parameters_.randomness  = value; break;
        case kParamSpace:       parameters_.space       = value; break;
        case kParamDrive:       parameters_.drive       = value; break;
        case kParamOutput:      parameters_.output      = value; break;
        case kParamConductorCh: parameters_.conductorCh = value; break;
        case kParamLfoTarget:   parameters_.lfoTarget   = value; break;
        case kParamLfoShape:    parameters_.lfoShape    = value; break;
        case kParamLfoRate:     parameters_.lfoRate     = value; break;
        case kParamLfoDepth:    parameters_.lfoDepth    = value; break;
        case kParamLfoSync:     parameters_.lfoSync     = value; break;
        case kParamLfoDivision: parameters_.lfoDivision = value; break;
        }
        parameters_ = downspout::bubbles::clampParameters(parameters_);
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyParameters) == 0)
            return String(downspout::bubbles::serializeParameters(parameters_).c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyParameters) != 0) return;
        const std::string text = value ? value : "";
        auto loaded = downspout::bubbles::deserializeParameters(text);
        if (loaded.has_value()) parameters_ = *loaded;
    }

    void activate() override
    {
        downspout::bubbles::activate(engineState_, getSampleRate());
    }

    void handleCC(uint8_t cc, uint8_t value) noexcept
    {
        const float norm = static_cast<float>(value) / 127.0f;
        switch (cc) {
        case   1: setParameterValue(kParamFlow,       norm); break;
        case   2: setParameterValue(kParamTurbulence, norm); break;
        case   3: setParameterValue(kParamDensity,    norm); break;
        case   4: setParameterValue(kParamSize,       norm); break;
        case   5: setParameterValue(kParamHeat,       norm); break;
        case   6: setParameterValue(kParamDepth,      norm); break;
        case   7: setParameterValue(kParamOutput,     norm); break;
        case  71: setParameterValue(kParamResonance,  norm); break;
        case  74: setParameterValue(kParamBrightness, norm); break;
        case  75: setParameterValue(kParamRandomness, norm); break;
        case  76: setParameterValue(kParamSpace,      norm); break;
        case  77: setParameterValue(kParamDrive,      norm); break;
        default: break;
        }
    }

    void run(const float**,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        std::fill_n(outputs[0], frames, 0.0f);
        std::fill_n(outputs[1], frames, 0.0f);

        // Fill transport snapshot from host
        CoreTransport transport {};
        {
            const TimePosition& pos = getTimePosition();
            transport.playing = pos.playing;
            if (pos.bbt.valid) {
                transport.valid       = true;
                transport.bar         = static_cast<double>(pos.bbt.bar) - 1.0;
                transport.barBeat     = static_cast<double>(pos.bbt.beat) - 1.0
                                      + static_cast<double>(pos.bbt.tick)
                                        / static_cast<double>(pos.bbt.ticksPerBeat);
                transport.beatsPerBar = pos.bbt.beatsPerBar;
                transport.bpm         = pos.bbt.beatsPerMinute;
            }
        }

        // Dispatch CC events; Conductor CCs override on the configured channel
        const int condCh = static_cast<int>(parameters_.conductorCh + 0.5f);
        for (uint32_t k = 0; k < midiEventCount; ++k) {
            const uint8_t* d = midiEvents[k].size > MidiEvent::kDataSize
                               ? midiEvents[k].dataExt : midiEvents[k].data;
            if ((d[0] & 0xF0u) == 0xB0u && midiEvents[k].size >= 3) {
                const uint8_t ch1 = static_cast<uint8_t>((d[0] & 0x0Fu) + 1u);
                const uint8_t cc  = d[1];
                const uint8_t val = d[2];
                if (condCh > 0 && ch1 == static_cast<uint8_t>(condCh)) {
                    // Conductor protocol (CC 21–24 on configured channel)
                    const float norm = static_cast<float>(val) / 127.0f;
                    switch (cc) {
                    case 21: setParameterValue(kParamDensity,    norm); break;
                    case 22: setParameterValue(kParamFlow,        norm); break;
                    case 23: setParameterValue(kParamRandomness,  norm); break;
                    case 24:
                        if (val == 127) {
                            // Reset: burst-trigger spawn accumulators
                            engineState_.bubbleSpawnAccum = 5.0f;
                            engineState_.dripSpawnAccum   = 5.0f;
                        }
                        break;
                    default: handleCC(cc, val); break;
                    }
                } else {
                    handleCC(cc, val);
                }
            }
        }

        std::array<CoreMidiEvent, kMaxMidiEventsPerBlock> coreEvents;
        const uint32_t count = std::min<uint32_t>(midiEventCount,
                                                   static_cast<uint32_t>(coreEvents.size()));
        for (uint32_t k = 0; k < count; ++k)
            coreEvents[k] = toCoreMidiEvent(midiEvents[k]);

        downspout::bubbles::processBlock(engineState_,
                                         parameters_,
                                         transport,
                                         frames,
                                         getSampleRate(),
                                         outputs[0],
                                         outputs[1],
                                         coreEvents.data(),
                                         count);
    }

private:
    CoreParameters  parameters_ {};
    CoreEngineState engineState_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BubblesPlugin)
};

Plugin* createPlugin()
{
    return new BubblesPlugin();
}

END_NAMESPACE_DISTRHO
