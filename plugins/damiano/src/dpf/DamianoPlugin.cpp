#include "DistrhoPlugin.hpp"

#include "damiano_core.hpp"

#include <array>
#include <cstdlib>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

// Parameter indices are stable — never reorder these.
// Mode is kept at index 0 (non-automatable) for project save/restore compatibility.
// Real-time mode switching uses the "mode" state key (bypasses IParameterChanges/automation).
enum ParameterIndex : uint32_t {
    kParamMode = 0,
    kParamDrive,
    kParamTone,
    kParamFoldCount,
    kParamMix,
    kParamOutputGain,
    kParamCCDrive,
    kParamCCChannel,
    kParameterCount
};

constexpr const char* kStateModeKey       = "mode";
constexpr const char* kStateDriveKey      = "drive";
constexpr const char* kStateToneKey       = "tone";
constexpr const char* kStateFoldCountKey  = "fold_count";
constexpr const char* kStateMixKey        = "mix";
constexpr const char* kStateOutputGainKey = "output_gain";
constexpr const char* kStateCCDriveKey    = "cc_drive";
constexpr const char* kStateCCChannelKey  = "cc_channel";

using CoreParameters  = downspout::damiano::Parameters;
using CoreEngineState = downspout::damiano::EngineState;
using CoreAudioBlock  = downspout::damiano::AudioBlock;

constexpr uint32_t kWrapperChannelCount = 2;

}  // namespace

class DamianoPlugin : public Plugin
{
public:
    DamianoPlugin()
        : Plugin(kParameterCount, 0, 8)  // 8 state keys: mode + 7 sliders
    {
        parameters_ = downspout::damiano::clampParameters(parameters_);
    }

protected:
    const char* getLabel() const override       { return "Damiano"; }
    const char* getDescription() const override { return "Stereo distortion with MIDI CC control from Drift."; }
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
        return d_cconst('D', 'a', 'm', 'i');
    }

    void initAudioPort(const bool input, const uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name   = String(input ? "Input " : "Output ") + String(static_cast<int>(index + 1));
        port.symbol = String(input ? "in_" : "out_")       + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        // No parameter is automatable: Reaper's Write/Latch mode records any
        // slider touch and replays it every process() block via IParameterChanges,
        // permanently overriding subsequent UI changes. Drive is controlled live
        // via MIDI CC (Drift); all other parameters are set-and-forget.
        parameter.hints = 0;

        switch (index)
        {
        case kParamMode:
            parameter.name   = "Mode";
            parameter.symbol = "mode";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = {0.0f, 5.0f, static_cast<float>(downspout::damiano::kModeTanh)};
            break;
        case kParamDrive:
            parameter.name   = "Drive";
            parameter.symbol = "drive";
            parameter.ranges = {1.0f, 10.0f, 2.0f};
            break;
        case kParamTone:
            parameter.name   = "Tone";
            parameter.symbol = "tone";
            parameter.ranges = {0.0f, 100.0f, 50.0f};
            break;
        case kParamFoldCount:
            parameter.name   = "Fold Count";
            parameter.symbol = "fold_count";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = {1.0f, 8.0f, 2.0f};
            break;
        case kParamMix:
            parameter.name   = "Mix";
            parameter.symbol = "mix";
            parameter.ranges = {0.0f, 100.0f, 100.0f};
            break;
        case kParamOutputGain:
            parameter.name   = "Output Gain";
            parameter.symbol = "output_gain";
            parameter.ranges = {-24.0f, 24.0f, 0.0f};
            break;
        case kParamCCDrive:
            parameter.name   = "CC Drive";
            parameter.symbol = "cc_drive";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = {0.0f, 127.0f, 0.0f};
            break;
        case kParamCCChannel:
            parameter.name   = "CC Channel";
            parameter.symbol = "cc_channel";
            parameter.hints  = kParameterIsInteger;
            parameter.ranges = {1.0f, 16.0f, 1.0f};
            break;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        struct StateInfo { const char* key; const char* label; const char* def; };
        static constexpr StateInfo kInfo[] = {
            { kStateModeKey,       "Mode",        "1"   },
            { kStateDriveKey,      "Drive",       "2"   },
            { kStateToneKey,       "Tone",        "50"  },
            { kStateFoldCountKey,  "Fold Count",  "2"   },
            { kStateMixKey,        "Mix",         "100" },
            { kStateOutputGainKey, "Output Gain", "0"   },
            { kStateCCDriveKey,    "CC Drive",    "0"   },
            { kStateCCChannelKey,  "CC Channel",  "1"   },
        };
        if (index < 8) {
            state.key          = kInfo[index].key;
            state.label        = kInfo[index].label;
            state.hints        = kStateIsOnlyForDSP;
            state.defaultValue = kInfo[index].def;
        }
    }

    float getParameterValue(uint32_t index) const override
    {
        switch (index) {
        case kParamMode:       return parameters_.mode;
        case kParamDrive:      return parameters_.drive;
        case kParamTone:       return parameters_.tone;
        case kParamFoldCount:  return parameters_.foldCount;
        case kParamMix:        return parameters_.mix;
        case kParamOutputGain: return parameters_.outputGain;
        case kParamCCDrive:    return parameters_.ccDrive;
        case kParamCCChannel:  return parameters_.ccChannel;
        default: return 0.0f;
        }
    }

    void setParameterValue(uint32_t /*index*/, float /*value*/) override
    {
        // Intentional no-op. REAPER Write/Latch mode records touches and
        // replays them via IParameterChanges every process block, permanently
        // overriding live values. All slider state is driven through setState
        // so host automation cannot interfere.
    }

    // All parameters driven through state: bypasses IParameterChanges so
    // REAPER automation cannot override live UI changes.
    void setState(const char* key, const char* value) override
    {
        if (!value) return;
        if (std::strcmp(key, kStateModeKey) == 0) {
            const int m = std::atoi(value);
            parameters_.mode = static_cast<float>(std::max(0, std::min(5, m)));
        } else if (std::strcmp(key, kStateDriveKey) == 0) {
            parameters_.drive      = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateToneKey) == 0) {
            parameters_.tone       = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateFoldCountKey) == 0) {
            parameters_.foldCount  = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateMixKey) == 0) {
            parameters_.mix        = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateOutputGainKey) == 0) {
            parameters_.outputGain = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateCCDriveKey) == 0) {
            parameters_.ccDrive    = static_cast<float>(std::atof(value));
        } else if (std::strcmp(key, kStateCCChannelKey) == 0) {
            parameters_.ccChannel  = static_cast<float>(std::atof(value));
        }
        parameters_ = downspout::damiano::clampParameters(parameters_);
    }

    void activate() override
    {
        engineState_      = CoreEngineState{};
        ccDriveOverride_  = -1.0f;
    }

    void run(const float** inputs, float** outputs, uint32_t frames,
             const MidiEvent* midiEvents, uint32_t midiEventCount) override
    {
        const int ccNum = static_cast<int>(parameters_.ccDrive);
        const int ccCh  = static_cast<int>(parameters_.ccChannel);

        if (ccNum > 0) {
            for (uint32_t i = 0; i < midiEventCount; ++i) {
                const auto& ev = midiEvents[i];
                if (ev.size < 3) continue;
                const uint8_t status  = ev.data[0];
                const uint8_t evCCNum = ev.data[1];
                const uint8_t evCCVal = ev.data[2];
                const bool isCC       = (status & 0xF0) == 0xB0;
                const int evChannel   = (status & 0x0F) + 1;

                if (isCC && evChannel == ccCh && evCCNum == static_cast<uint8_t>(ccNum))
                    ccDriveOverride_ = 1.0f + (static_cast<float>(evCCVal) / 127.0f) * 9.0f;
            }
        } else {
            ccDriveOverride_ = -1.0f;
        }

        const float effectiveDrive = (ccDriveOverride_ >= 0.0f)
                                        ? ccDriveOverride_
                                        : parameters_.drive;

        std::array<const float*, downspout::damiano::kMaxChannels> safeInputs {};
        std::array<float*,       downspout::damiano::kMaxChannels> safeOutputs {};

        for (uint32_t c = 0; c < kWrapperChannelCount; ++c) {
            safeInputs[c]  = inputs[c];
            safeOutputs[c] = outputs[c];
        }

        CoreAudioBlock audio;
        audio.inputs       = safeInputs;
        audio.outputs      = safeOutputs;
        audio.channelCount = kWrapperChannelCount;

        downspout::damiano::processBlock(engineState_, parameters_, frames,
                                         getSampleRate(), audio, effectiveDrive);
    }

private:
    CoreParameters  parameters_ {};
    CoreEngineState engineState_ {};
    float           ccDriveOverride_ = -1.0f;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DamianoPlugin)
};

Plugin* createPlugin()
{
    return new DamianoPlugin();
}

END_NAMESPACE_DISTRHO
