#include "DistrhoPlugin.hpp"

#include "skream_core.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Stable parameter indices — never reorder.
enum ParameterIndex : uint32_t {
    kParamInputGain = 0,
    kParamCutoff,
    kParamScream,
    kParamResonance,
    kParamMix,
    kParamOutputGain,
    kParamTrack,
    kParamCCCutoff,
    kParamCCScream,
    kParamCCChannel,
    kParameterCount
};

constexpr const char* kStateInputGain  = "input_gain";
constexpr const char* kStateCutoff     = "cutoff";
constexpr const char* kStateScream     = "scream";
constexpr const char* kStateResonance  = "resonance";
constexpr const char* kStateMix        = "mix";
constexpr const char* kStateOutputGain = "output_gain";
constexpr const char* kStateTrack      = "track";
constexpr const char* kStateCCCutoff   = "cc_cutoff";
constexpr const char* kStateCCScream   = "cc_scream";
constexpr const char* kStateCCChannel  = "cc_channel";

constexpr uint32_t kStateCount = 10;

using CoreParameters  = downspout::skream::Parameters;
using CoreEngineState = downspout::skream::EngineState;
using CoreAudioBlock  = downspout::skream::AudioBlock;

}  // namespace

class SkreemPlugin : public Plugin
{
public:
    SkreemPlugin()
        : Plugin(kParameterCount, 0, kStateCount)
    {
        parameters_ = downspout::skream::clampParameters(parameters_);
    }

protected:
    const char* getLabel()       const override { return "Skream"; }
    const char* getDescription() const override { return "Scream filter: SVF + feedback saturation for dubstep growl."; }
    const char* getMaker()       const override { return "danja"; }
    const char* getHomePage()    const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense()     const override { return "MIT"; }

    uint32_t getVersion() const override
    {
        return d_version(DOWNSPOUT_PLUGIN_VERSION_MAJOR,
                         DOWNSPOUT_PLUGIN_VERSION_MINOR,
                         DOWNSPOUT_PLUGIN_VERSION_PATCH);
    }

    int64_t getUniqueId() const override
    {
        return d_cconst('S', 'k', 'r', 'm');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_"    : "out_")    + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        // Non-automatable: REAPER Write/Latch mode would permanently override values.
        // Drift controls these via MIDI CC; all other changes come through setState.
        parameter.hints = 0;

        switch (index) {
        case kParamInputGain:
            parameter.name   = "Input Gain";
            parameter.symbol = "input_gain";
            parameter.ranges = { -24.0f, 24.0f, 0.0f };
            break;
        case kParamCutoff:
            parameter.name   = "Cutoff";
            parameter.symbol = "cutoff";
            parameter.ranges = { 0.0f, 100.0f, 85.0f };
            break;
        case kParamScream:
            parameter.name   = "Scream";
            parameter.symbol = "scream";
            parameter.ranges = { 0.0f, 100.0f, 46.5f };
            break;
        case kParamResonance:
            parameter.name   = "Resonance";
            parameter.symbol = "resonance";
            parameter.ranges = { 0.0f, 100.0f, 100.0f };
            break;
        case kParamMix:
            parameter.name   = "Mix";
            parameter.symbol = "mix";
            parameter.ranges = { 0.0f, 100.0f, 100.0f };
            break;
        case kParamOutputGain:
            parameter.name   = "Output Gain";
            parameter.symbol = "output_gain";
            parameter.ranges = { -24.0f, 0.0f, -6.0f };
            break;
        case kParamTrack:
            parameter.name   = "Track";
            parameter.symbol = "track";
            parameter.ranges = { 0.0f, 100.0f, 0.0f };
            break;
        case kParamCCCutoff:
            parameter.name   = "CC Cutoff";
            parameter.symbol = "cc_cutoff";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 0.0f, 127.0f, 0.0f };
            break;
        case kParamCCScream:
            parameter.name   = "CC Scream";
            parameter.symbol = "cc_scream";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 0.0f, 127.0f, 0.0f };
            break;
        case kParamCCChannel:
            parameter.name   = "CC Channel";
            parameter.symbol = "cc_channel";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = { 1.0f, 16.0f, 1.0f };
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct Info { const char* key; const char* label; const char* def; };
        static constexpr Info kInfo[kStateCount] = {
            { kStateInputGain,  "Input Gain",  "0"    },
            { kStateCutoff,     "Cutoff",      "85"   },
            { kStateScream,     "Scream",      "46.5" },
            { kStateResonance,  "Resonance",   "100"  },
            { kStateMix,        "Mix",         "100"  },
            { kStateOutputGain, "Output Gain", "-6"   },
            { kStateTrack,      "Track",       "0"    },
            { kStateCCCutoff,   "CC Cutoff",   "1"    },
            { kStateCCScream,   "CC Scream",   "2"    },
            { kStateCCChannel,  "CC Channel",  "1"    },
        };
        if (index < kStateCount) {
            state.key          = kInfo[index].key;
            state.label        = kInfo[index].label;
            state.hints        = kStateIsOnlyForDSP;
            state.defaultValue = kInfo[index].def;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamInputGain:  return parameters_.inputGain;
        case kParamCutoff:     return parameters_.cutoff;
        case kParamScream:     return parameters_.scream;
        case kParamResonance:  return parameters_.resonance;
        case kParamMix:        return parameters_.mix;
        case kParamOutputGain: return parameters_.outputGain;
        case kParamTrack:      return parameters_.track;
        case kParamCCCutoff:   return parameters_.ccCutoff;
        case kParamCCScream:   return parameters_.ccScream;
        case kParamCCChannel:  return parameters_.ccChannel;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t /*index*/, float /*value*/) override
    {
        // Intentional no-op — all state driven through setState to prevent
        // host automation replay from overriding live values.
    }

    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        auto f = [&] { return static_cast<float>(std::atof(value)); };
        if      (std::strcmp(key, kStateInputGain)  == 0) { parameters_.inputGain  = f(); }
        else if (std::strcmp(key, kStateCutoff)     == 0) { parameters_.cutoff     = f(); }
        else if (std::strcmp(key, kStateScream)     == 0) { parameters_.scream     = f(); }
        else if (std::strcmp(key, kStateResonance)  == 0) { parameters_.resonance  = f(); }
        else if (std::strcmp(key, kStateMix)        == 0) { parameters_.mix        = f(); }
        else if (std::strcmp(key, kStateOutputGain) == 0) { parameters_.outputGain = f(); }
        else if (std::strcmp(key, kStateTrack)      == 0) { parameters_.track      = f(); }
        else if (std::strcmp(key, kStateCCCutoff)   == 0) { parameters_.ccCutoff   = f(); }
        else if (std::strcmp(key, kStateCCScream)   == 0) { parameters_.ccScream   = f(); }
        else if (std::strcmp(key, kStateCCChannel)  == 0) { parameters_.ccChannel  = f(); }
        parameters_ = downspout::skream::clampParameters(parameters_);
    }

    void activate() override
    {
        engineState_      = CoreEngineState{};
        ccCutoffOverride_ = -1.0f;
        ccScreamOverride_ = -1.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccCutoffNum = static_cast<int>(parameters_.ccCutoff);
        const int ccScreamNum = static_cast<int>(parameters_.ccScream);
        const int ccCh        = static_cast<int>(parameters_.ccChannel);

        for (uint32_t i = 0; i < midiEventCount; ++i) {
            const auto& ev = midiEvents[i];
            if (ev.size < 3) continue;
            const uint8_t status = ev.data[0];
            if ((status & 0xF0) != 0xB0) continue;  // not CC
            if ((status & 0x0F) + 1 != ccCh) continue;

            const uint8_t evCC  = ev.data[1];
            const uint8_t evVal = ev.data[2];
            const float   norm  = static_cast<float>(evVal) / 127.0f * 100.0f;

            if (ccCutoffNum > 0 && evCC == static_cast<uint8_t>(ccCutoffNum))
                ccCutoffOverride_ = norm;
            if (ccScreamNum > 0 && evCC == static_cast<uint8_t>(ccScreamNum))
                ccScreamOverride_ = norm;
        }

        if (ccCutoffNum == 0) ccCutoffOverride_ = -1.0f;
        if (ccScreamNum == 0) ccScreamOverride_ = -1.0f;

        const float effectiveCutoff = (ccCutoffOverride_ >= 0.0f) ? ccCutoffOverride_ : parameters_.cutoff;
        const float effectiveScream = (ccScreamOverride_ >= 0.0f) ? ccScreamOverride_ : parameters_.scream;

        CoreAudioBlock audio;
        audio.inputs[0]  = inputs[0];
        audio.inputs[1]  = inputs[1];
        audio.outputs[0] = outputs[0];
        audio.outputs[1] = outputs[1];

        downspout::skream::processBlock(engineState_, parameters_, frames,
                                        getSampleRate(), audio,
                                        effectiveCutoff, effectiveScream);
    }

private:
    CoreParameters  parameters_{};
    CoreEngineState engineState_{};
    float           ccCutoffOverride_ = -1.0f;
    float           ccScreamOverride_ = -1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SkreemPlugin)
};

Plugin* createPlugin()
{
    return new SkreemPlugin();
}

END_NAMESPACE_DISTRHO
