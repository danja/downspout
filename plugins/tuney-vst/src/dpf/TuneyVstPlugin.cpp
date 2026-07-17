#include "DistrhoPlugin.hpp"
#include "tuney_vst_engine.hpp"

#include <cstring>

START_NAMESPACE_DISTRHO

namespace {
constexpr std::uint32_t kStateTuney = 0;
constexpr std::uint32_t kStateUiEvent = 1;
constexpr std::uint32_t kStateCount = 2;
constexpr const char* kStateKeyTuney = "tuney_state";
constexpr const char* kStateKeyUiEvent = "ui_event";

ParameterEnumerationValue kWaveforms[] = {{0, "Sine"}, {1, "Square"}, {2, "Triangle"}};
ParameterEnumerationValue kLimiters[] = {{0, "Wrap"}, {1, "Reflect"}, {2, "Reflect Repeat"}};
ParameterEnumerationValue kTunings[] = {{0, "Computed"}, {1, "Ratios"}, {2, "Table"}};
}

class TuneyVstPlugin final : public Plugin {
public:
    TuneyVstPlugin() : Plugin(downspout::tuney_vst::kParamCount, 0, kStateCount), engine_(getSampleRate()) {}

protected:
    const char* getLabel() const override { return "TuneyVST"; }
    const char* getDescription() const override { return "Text-to-music instrument with flexible mapping, microtonal tuning, audio, and MIDI output."; }
    const char* getMaker() const override { return "danja"; }
    const char* getHomePage() const override { return "https://danja.github.io/downspout/"; }
    const char* getLicense() const override { return "MIT"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('T', 'u', 'n', 'y'); }

    void initAudioPort(bool input, uint32_t index, AudioPort& port) override
    {
        Plugin::initAudioPort(input, index, port);
        if (index < 2) port.groupId = kPortGroupStereo;
        port.name = String("Output ") + String(static_cast<int>(index + 1));
        port.symbol = String("out_") + String(static_cast<int>(index + 1));
    }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= downspout::tuney_vst::kParamCount) return;
        const auto& spec = downspout::tuney_vst::kParameterSpecs[index];
        parameter.name = spec.name;
        parameter.symbol = spec.symbol;
        parameter.hints = kParameterIsAutomatable;
        if (spec.integer) parameter.hints |= kParameterIsInteger;
        if (spec.boolean) parameter.hints |= kParameterIsBoolean;
        if (index == downspout::tuney_vst::kParamPlay || index == downspout::tuney_vst::kParamStop)
            parameter.hints |= kParameterIsTrigger;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;
        parameter.ranges.def = spec.defaultValue;
        if (index == downspout::tuney_vst::kParamWaveform) {
            parameter.enumValues.count = 3; parameter.enumValues.restrictedMode = true; parameter.enumValues.values = kWaveforms; parameter.enumValues.deleteLater = false;
        } else if (index == downspout::tuney_vst::kParamLimiter) {
            parameter.enumValues.count = 3; parameter.enumValues.restrictedMode = true; parameter.enumValues.values = kLimiters; parameter.enumValues.deleteLater = false;
        } else if (index == downspout::tuney_vst::kParamTuningType) {
            parameter.enumValues.count = 3; parameter.enumValues.restrictedMode = true; parameter.enumValues.values = kTunings; parameter.enumValues.deleteLater = false;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == kStateTuney) {
            static const std::string defaults = downspout::tuney_vst::serializeState({});
            state.key = kStateKeyTuney; state.defaultValue = defaults.c_str();
        } else if (index == kStateUiEvent) {
            state.key = kStateKeyUiEvent; state.defaultValue = ""; state.hints = kStateIsOnlyForDSP;
        }
    }

    float getParameterValue(uint32_t index) const override { return engine_.getParameter(index); }
    void setParameterValue(uint32_t index, float value) override { engine_.setParameter(index, value); }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyTuney) == 0) return String(engine_.stateText().c_str());
        return String();
    }

    void setState(const char* key, const char* value) override
    {
        if (!key || !value) return;
        if (std::strcmp(key, kStateKeyTuney) == 0) (void)engine_.setStateText(value);
        else if (std::strcmp(key, kStateKeyUiEvent) == 0) {
            const std::string_view event(value);
            if (event == "play") engine_.startPlayback();
            else if (event == "stop") engine_.stopPlayback();
            else if (event == "clear") engine_.clearText();
            else if (event.starts_with("type:")) engine_.queueTypedCharacter(event.substr(5));
        }
    }

    void activate() override { engine_.reset(); }
    void deactivate() override { engine_.reset(); }
    void sampleRateChanged(double sampleRate) override { engine_.setSampleRate(sampleRate); }

    void run(const float**, float** outputs, uint32_t frames, const MidiEvent*, uint32_t) override
    {
        downspout::tuney_vst::ProcessResult result;
        engine_.process(outputs[0], outputs[1], frames, result);
        for (std::size_t i = 0; i < result.midiCount; ++i) {
            MidiEvent event {}; event.frame = result.midi[i].frame; event.size = 3;
            event.data[0] = result.midi[i].data[0]; event.data[1] = result.midi[i].data[1]; event.data[2] = result.midi[i].data[2];
            writeMidiEvent(event);
        }
    }

private:
    downspout::tuney_vst::TuneyEngine engine_;
    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TuneyVstPlugin)
};

Plugin* createPlugin() { return new TuneyVstPlugin(); }

END_NAMESPACE_DISTRHO
