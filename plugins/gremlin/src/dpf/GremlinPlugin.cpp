#include "DistrhoPlugin.hpp"

#include "gremlin_dpf_shared.hpp"
#include "gremlin_processor.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

START_NAMESPACE_DISTRHO

namespace {

using downspout::gremlin::ActionId;
using downspout::gremlin::LiveParamId;
using downspout::gremlin::HiddenParamId;
using downspout::gremlin::MacroId;
using downspout::gremlin::MidiMessage;
using downspout::gremlin::MomentaryId;
using downspout::gremlin::Processor;
using downspout::gremlin::SceneId;
using downspout::gremlin::kActionBurst;
using downspout::gremlin::kActionPanic;
using downspout::gremlin::kActionRandomAll;
using downspout::gremlin::kActionRandomDelay;
using downspout::gremlin::kActionRandomSource;
using downspout::gremlin::kActionReseed;
using downspout::gremlin::kGremlinParameterCount;
using downspout::gremlin::kHiddenParamCount;
using downspout::gremlin::kHiddenParamSpecs;
using downspout::gremlin::kLiveParamCount;
using downspout::gremlin::kLiveParamSpecs;
using downspout::gremlin::kMacroCount;
using downspout::gremlin::kMacroNames;
using downspout::gremlin::kMomentaryCount;
using downspout::gremlin::kMomentaryNames;
using downspout::gremlin::kParamActionStart;
using downspout::gremlin::kParamInputHiddenStart;
using downspout::gremlin::kParamInputLiveStart;
using downspout::gremlin::kParamMacroStart;
using downspout::gremlin::kParamMasterTrim;
using downspout::gremlin::kParamMomentaryStart;
using downspout::gremlin::kParamSceneTriggerStart;
using downspout::gremlin::kParamStatusActivity;
using downspout::gremlin::kParamStatusScene;
using downspout::gremlin::kParamStatusStart;
using downspout::gremlin::kSceneCount;
using downspout::gremlin::kSceneNames;
using downspout::gremlin::kStatusNames;

constexpr std::array<const char*, downspout::gremlin::kEffectiveParamCount> kStatusSymbols = {{
    "status_live_mode",
    "status_live_damage",
    "status_live_chaos",
    "status_live_noise",
    "status_live_drift",
    "status_live_crunch",
    "status_live_fold",
    "status_live_delay_time",
    "status_live_feedback",
    "status_live_warp",
    "status_live_stutter",
    "status_live_tone",
    "status_live_damping",
    "status_live_space",
    "status_live_attack",
    "status_live_release",
    "status_live_output",
    "status_live_source_gain",
    "status_live_burst",
    "status_live_pitch_spread",
    "status_live_delay_mix",
    "status_live_cross_feedback",
    "status_live_glitch_length",
    "status_live_chaos_rate",
    "status_live_duck",
}};

constexpr std::array<const char*, 6> kActionSymbols = {{
    "action_reseed",
    "action_burst",
    "action_random_source",
    "action_random_delay",
    "action_random_all",
    "action_panic",
}};

constexpr std::array<const char*, 6> kActionLabels = {{
    "Reseed",
    "Burst",
    "Rand Source",
    "Rand Delay",
    "Rand All",
    "Panic",
}};

constexpr std::array<const char*, kSceneCount> kSceneTriggerSymbols = {{
    "scene_splinter",
    "scene_melt",
    "scene_rust",
    "scene_tunnel",
}};

MidiMessage toCoreMidiMessage(const MidiEvent& event)
{
    MidiMessage message {};
    message.frame = event.frame;
    message.size = static_cast<std::uint8_t>(std::min<std::size_t>(event.size, message.data.size()));
    const std::uint8_t* const bytes = event.size > MidiEvent::kDataSize ? event.dataExt : event.data;
    for (std::size_t i = 0; i < message.size; ++i)
        message.data[i] = bytes[i];
    return message;
}

}  // namespace

class GremlinPlugin : public Plugin
{
public:
    GremlinPlugin()
        : Plugin(kGremlinParameterCount, 0, 0)
    {
        processor_.init(getSampleRate());
    }

protected:
    const char* getLabel() const override
    {
        return "gremlin";
    }

    const char* getDescription() const override
    {
        return "Chaotic malfunction instrument with direct gesture and modulation control.";
    }

    const char* getMaker() const override
    {
        return "danja";
    }

    const char* getHomePage() const override
    {
        return "https://danja.github.io/flues/";
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
        return d_cconst('G', 'r', 'm', 'n');
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

        if (index >= kParamInputLiveStart && index < kParamInputHiddenStart)
        {
            const auto& spec = kLiveParamSpecs[index - kParamInputLiveStart];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            if (spec.integer)
                parameter.hints |= kParameterIsInteger;
            return;
        }

        if (index >= kParamInputHiddenStart && index < kParamMasterTrim)
        {
            const auto& spec = kHiddenParamSpecs[index - kParamInputHiddenStart];
            parameter.name = spec.name;
            parameter.symbol = spec.symbol;
            parameter.ranges.min = spec.minimum;
            parameter.ranges.max = spec.maximum;
            parameter.ranges.def = spec.defaultValue;
            return;
        }

        if (index == kParamMasterTrim)
        {
            parameter.name = "Master Trim";
            parameter.symbol = "master_trim";
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.45f;
            return;
        }

        if (index >= kParamMacroStart && index < kParamMomentaryStart)
        {
            const std::size_t macroIndex = index - kParamMacroStart;
            parameter.name = kMacroNames[macroIndex];
            parameter.symbol = downspout::gremlin::kMacroParamSpecs[macroIndex].symbol;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.5f;
            return;
        }

        if (index >= kParamMomentaryStart && index < kParamActionStart)
        {
            const std::size_t momentaryIndex = index - kParamMomentaryStart;
            parameter.name = kMomentaryNames[momentaryIndex];
            parameter.symbol = downspout::gremlin::kMomentaryParamSpecs[momentaryIndex].symbol;
            parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamActionStart && index < kParamSceneTriggerStart)
        {
            const std::size_t actionIndex = index - kParamActionStart;
            parameter.name = kActionLabels[actionIndex];
            parameter.symbol = kActionSymbols[actionIndex];
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamSceneTriggerStart && index < kParamStatusStart)
        {
            const std::size_t sceneIndex = index - kParamSceneTriggerStart;
            parameter.name = kSceneNames[sceneIndex + 1];
            parameter.symbol = kSceneTriggerSymbols[sceneIndex];
            parameter.hints = kParameterIsAutomatable | kParameterIsBoolean | kParameterIsInteger | kParameterIsTrigger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index >= kParamStatusStart && index < kParamStatusScene)
        {
            const std::size_t statusIndex = index - kParamStatusStart;
            parameter.name = kStatusNames[statusIndex];
            parameter.symbol = kStatusSymbols[statusIndex];
            parameter.hints = kParameterIsOutput;
            if (statusIndex == 0)
                parameter.hints |= kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = (statusIndex == 0) ? 3.0f : 1.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        if (index == kParamStatusScene)
        {
            parameter.name = "Scene Status";
            parameter.symbol = "status_scene";
            parameter.hints = kParameterIsOutput | kParameterIsInteger;
            parameter.ranges.min = 0.0f;
            parameter.ranges.max = 4.0f;
            parameter.ranges.def = 0.0f;
            return;
        }

        parameter.name = "Controller Activity";
        parameter.symbol = "status_controller_activity";
        parameter.hints = kParameterIsOutput;
        parameter.ranges.min = 0.0f;
        parameter.ranges.max = 1.0f;
        parameter.ranges.def = 0.0f;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index >= kParamInputLiveStart && index < kParamInputHiddenStart)
            return processor_.getLiveParameter(static_cast<LiveParamId>(index - kParamInputLiveStart));

        if (index >= kParamInputHiddenStart && index < kParamMasterTrim)
            return processor_.getHiddenParameter(static_cast<HiddenParamId>(index - kParamInputHiddenStart));

        if (index == kParamMasterTrim)
            return processor_.getMasterTrim();

        if (index >= kParamMacroStart && index < kParamMomentaryStart)
            return processor_.getMacro(static_cast<MacroId>(index - kParamMacroStart));

        if (index >= kParamMomentaryStart && index < kParamActionStart)
            return processor_.getMomentary(static_cast<MomentaryId>(index - kParamMomentaryStart)) ? 1.0f : 0.0f;

        if (index >= kParamActionStart && index < kParamStatusStart)
            return 0.0f;

        const downspout::gremlin::Status& status = processor_.getStatus();
        if (index >= kParamStatusStart && index < kParamStatusScene)
            return status.effective[index - kParamStatusStart];

        if (index == kParamStatusScene)
            return static_cast<float>(status.currentScene);

        return status.controllerActivity;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kParamInputLiveStart && index < kParamInputHiddenStart)
        {
            processor_.setLiveParameter(static_cast<LiveParamId>(index - kParamInputLiveStart), value);
            return;
        }

        if (index >= kParamInputHiddenStart && index < kParamMasterTrim)
        {
            processor_.setHiddenParameter(static_cast<HiddenParamId>(index - kParamInputHiddenStart), value);
            return;
        }

        if (index == kParamMasterTrim)
        {
            processor_.setMasterTrim(value);
            return;
        }

        if (index >= kParamMacroStart && index < kParamMomentaryStart)
        {
            processor_.setMacro(static_cast<MacroId>(index - kParamMacroStart), value);
            return;
        }

        if (index >= kParamMomentaryStart && index < kParamActionStart)
        {
            processor_.setMomentary(static_cast<MomentaryId>(index - kParamMomentaryStart), value >= 0.5f);
            return;
        }

        if (index >= kParamActionStart && index < kParamStatusStart)
        {
            if (value > 0.5f)
            {
                if (index < kParamSceneTriggerStart)
                {
                    switch (index)
                    {
                    case kActionReseed: processor_.triggerAction(ActionId::reseed); break;
                    case kActionBurst: processor_.triggerAction(ActionId::burst); break;
                    case kActionRandomSource: processor_.triggerAction(ActionId::randomSource); break;
                    case kActionRandomDelay: processor_.triggerAction(ActionId::randomDelay); break;
                    case kActionRandomAll: processor_.triggerAction(ActionId::randomAll); break;
                    case kActionPanic: processor_.triggerAction(ActionId::panic); break;
                    default: break;
                    }
                }
                else
                {
                    const std::uint32_t sceneIndex = index - kParamSceneTriggerStart;
                    processor_.loadScene(static_cast<SceneId>(sceneIndex + 1));
                }
            }
        }
    }

    void activate() override
    {
        processor_.activate();
    }

    void sampleRateChanged(double newSampleRate) override
    {
        processor_.init(newSampleRate);
    }

    void run(const float**,
             float** outputs,
             uint32_t frames,
             const MidiEvent* midiEvents,
             uint32_t midiEventCount) override
    {
        std::array<MidiMessage, 256> coreEvents {};
        const std::uint32_t coreCount = std::min<std::uint32_t>(midiEventCount, coreEvents.size());
        for (std::uint32_t i = 0; i < coreCount; ++i)
            coreEvents[i] = toCoreMidiMessage(midiEvents[i]);

        processor_.processBlock(outputs[0], outputs[1], frames, coreEvents.data(), coreCount);
    }

private:
    Processor processor_ {};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GremlinPlugin)
};

Plugin* createPlugin()
{
    return new GremlinPlugin();
}

END_NAMESPACE_DISTRHO
