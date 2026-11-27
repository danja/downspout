#include "DistrhoPlugin.hpp"

#include "energise_core.hpp"
#include "energise_params.hpp"
#include "energise_serialization.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

namespace core = downspout::energise;

constexpr const char* kStateKeyProject = core::kStateKey;

enum StateIndex : uint32_t {
    kStateProject = 0,
    kStateCount
};

ParameterEnumerationValue* gridValues()
{
    static ParameterEnumerationValue values[4] = {
        {0.0f, core::kGridNames[0]},
        {1.0f, core::kGridNames[1]},
        {2.0f, core::kGridNames[2]},
        {3.0f, core::kGridNames[3]},
    };
    return values;
}

ParameterEnumerationValue* classValues()
{
    static std::array<ParameterEnumerationValue, core::kClassCount> values = [] {
        std::array<ParameterEnumerationValue, core::kClassCount> result {};
        for (std::size_t i = 0; i < result.size(); ++i) {
            result[i].value = static_cast<float>(i);
            result[i].label = core::kClassNames[i];
        }
        return result;
    }();
    return values.data();
}

ParameterEnumerationValue* stateValues()
{
    static ParameterEnumerationValue values[6] = {
        {0.0f, core::kStateNames[0]}, {1.0f, core::kStateNames[1]},
        {2.0f, core::kStateNames[2]}, {3.0f, core::kStateNames[3]},
        {4.0f, core::kStateNames[4]}, {5.0f, core::kStateNames[5]},
    };
    return values;
}

bool screenshotDemoRequested()
{
    const char* value = std::getenv("DOWNSPOUT_SCREENSHOT_DEMO");
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

}  // namespace

class EnergisePlugin : public Plugin
{
public:
    EnergisePlugin()
        : Plugin(static_cast<uint32_t>(core::kParameterCount), 0, kStateCount)
    {
        processor_.activate(static_cast<int>(std::lround(getSampleRate())));
        if (screenshotDemoRequested())
            prepareScreenshotDemo();
    }

protected:
    const char* getLabel() const override { return "Energise"; }

    const char* getDescription() const override
    {
        return "Offline drum-break transcription, event editing, quantisation and resynthesis.";
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

    int64_t getUniqueId() const override { return d_cconst('E', 'n', 'r', 'g'); }

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
        parameter.ranges.def = spec.defaultValue;
        parameter.ranges.min = spec.minimum;
        parameter.ranges.max = spec.maximum;

        switch (static_cast<core::ParamId>(index)) {
        case core::ParamId::grid:
            parameter.enumValues.count = 4;
            parameter.enumValues.values = gridValues();
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.deleteLater = false;
            break;
        case core::ParamId::status:
            parameter.enumValues.count = 6;
            parameter.enumValues.values = stateValues();
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.deleteLater = false;
            break;
        case core::ParamId::selectedClass:
        case core::ParamId::editClass:
            parameter.enumValues.count = static_cast<uint32_t>(core::kClassCount);
            parameter.enumValues.values = classValues();
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.deleteLater = false;
            break;
        default:
            if (index >= static_cast<uint32_t>(core::ParamId::eventStart) &&
                ((index - static_cast<uint32_t>(core::ParamId::eventStart)) % core::kEventFields) == 1) {
                parameter.enumValues.count = static_cast<uint32_t>(core::kClassCount);
                parameter.enumValues.values = classValues();
                parameter.enumValues.restrictedMode = true;
                parameter.enumValues.deleteLater = false;
            }
            break;
        }
    }

    void initState(const uint32_t index, State& state) override
    {
        if (index != kStateProject)
            return;
        state.key = kStateKeyProject;
        state.label = "Project";
        state.hints = kStateIsOnlyForDSP;
        state.defaultValue = "";
    }

    String getState(const char* key) const override
    {
        if (std::strcmp(key, kStateKeyProject) != 0)
            return String();
        const std::string text = processor_.serialize();
        return String(text.c_str());
    }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, kStateKeyProject) != 0)
            return;
        if (value == nullptr || value[0] == '\0')
            return;
        processor_.deserialize(value);
        stopRuntime();
    }

    float getParameterValue(const uint32_t index) const override
    {
        return processor_.parameter(index);
    }

    void setParameterValue(const uint32_t index, const float value) override
    {
        if (index < core::kInputParameterCount)
            processor_.setParameter(index, value);
    }

    void activate() override
    {
        processor_.activate(static_cast<int>(std::lround(getSampleRate())));
        stopRuntime();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        processor_.activate(static_cast<int>(std::lround(newSampleRate)));
        stopRuntime();
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        handleRequests();
        flushMidi(frames);
        if (recording_)
            appendCapture(inputs, frames);
        renderAudio(inputs, outputs, frames);
        publishParameters();
    }

private:
    core::Processor processor_ {};
    std::vector<float> recordingLeft_ {};
    std::vector<float> recordingRight_ {};
    std::size_t captureTargetFrames_ = 0;
    std::size_t playhead_ = 0;
    bool recording_ = false;

    std::deque<core::MidiMessage> midiQueue_ {};
    uint32_t midiClock_ = 0;
    uint32_t publishCounter_ = 0;
    bool publishDirty_ = true;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnergisePlugin)

    void prepareScreenshotDemo()
    {
        const auto seed = static_cast<uint32_t>(
            std::lround(processor_.parameter(static_cast<uint32_t>(core::ParamId::seed))));
        if (!processor_.makeDemo(static_cast<int>(std::lround(getSampleRate())), seed))
            return;
        processor_.analyse();
        processor_.quantise();
        processor_.render();
    }

    void stopRuntime()
    {
        recording_ = false;
        recordingLeft_.clear();
        recordingRight_.clear();
        captureTargetFrames_ = 0;
        playhead_ = 0;
        midiQueue_.clear();
        midiClock_ = 0;
        publishDirty_ = true;
    }

    void handleRequests()
    {
        const core::RequestActions actions = processor_.takeRequests();
        if (actions.capture) {
            recording_ = true;
            recordingLeft_.clear();
            recordingRight_.clear();
            const int seconds = static_cast<int>(std::lround(
                processor_.parameter(static_cast<uint32_t>(core::ParamId::captureSeconds))));
            captureTargetFrames_ = static_cast<std::size_t>(
                std::clamp(seconds, core::kMinCaptureSeconds, core::kMaxCaptureSeconds))
                * static_cast<std::size_t>(std::max(1, static_cast<int>(std::lround(getSampleRate()))));
            playhead_ = 0;
        }
        if (actions.demo) {
            const auto seed = static_cast<uint32_t>(
                std::lround(processor_.parameter(static_cast<uint32_t>(core::ParamId::seed))));
            recording_ = false;
            processor_.makeDemo(static_cast<int>(std::lround(getSampleRate())), seed);
            playhead_ = 0;
        }
        if (actions.analyse)
            processor_.analyse();
        if (actions.render)
            processor_.render();
        if (actions.abSwap) {
            processor_.setAbSide(!processor_.showingRender());
            playhead_ = 0;
        }
        if (actions.undo) {
            processor_.undo();
            playhead_ = 0;
        }
        if (actions.redo) {
            processor_.redo();
            playhead_ = 0;
        }
        if (actions.exportMidi)
            queueMidi();

        if (actions.capture || actions.analyse || actions.render || actions.abSwap ||
            actions.undo || actions.redo || actions.demo || actions.eventEdit)
            publishDirty_ = true;
    }

    void queueMidi()
    {
        midiQueue_.clear();
        midiClock_ = 0;
        const int captureRate = processor_.captureBuffer().sampleRate;
        const double ratio = captureRate > 0
                                 ? static_cast<double>(std::lround(getSampleRate())) / static_cast<double>(captureRate)
                                 : 1.0;
        for (const core::MidiMessage& message : processor_.exportMidi()) {
            core::MidiMessage queued = message;
            queued.frame = static_cast<uint32_t>(std::lround(static_cast<double>(message.frame) * ratio));
            midiQueue_.push_back(queued);
        }
    }

    void flushMidi(const uint32_t frames)
    {
        while (!midiQueue_.empty() && midiQueue_.front().frame < midiClock_ + frames) {
            const core::MidiMessage message = midiQueue_.front();
            midiQueue_.pop_front();
            MidiEvent event {};
            event.frame = message.frame - midiClock_;
            event.size = 3;
            event.data[0] = message.status;
            event.data[1] = message.note;
            event.data[2] = message.velocity;
            event.dataExt = nullptr;
            if (!writeMidiEvent(event))
                break;
        }
        midiClock_ += frames;
    }

    void appendCapture(const float** inputs, const uint32_t frames)
    {
        if (captureTargetFrames_ == 0 || inputs == nullptr || inputs[0] == nullptr || inputs[1] == nullptr)
            return;
        const std::size_t have = recordingLeft_.size();
        const std::size_t take = std::min<std::size_t>(frames, captureTargetFrames_ - have);
        for (std::size_t i = 0; i < take; ++i) {
            recordingLeft_.push_back(inputs[0][i]);
            recordingRight_.push_back(inputs[1][i]);
        }
        if (recordingLeft_.size() < captureTargetFrames_)
            return;

        core::CaptureBuffer buffer {};
        buffer.sampleRate = static_cast<int>(std::lround(getSampleRate()));
        buffer.left = recordingLeft_;
        buffer.right = recordingRight_;
        processor_.capture(buffer);
        recording_ = false;
        recordingLeft_.clear();
        recordingRight_.clear();
        captureTargetFrames_ = 0;
        playhead_ = 0;
        publishDirty_ = true;
    }

    void renderAudio(const float** inputs, float** outputs, const uint32_t frames)
    {
        if (outputs == nullptr || outputs[0] == nullptr || outputs[1] == nullptr)
            return;

        const bool haveInputs = inputs != nullptr && inputs[0] != nullptr && inputs[1] != nullptr;
        const bool useRender = processor_.showingRender() && processor_.renderReady();
        const std::vector<float>& left = useRender ? processor_.renderBuffer().left : processor_.captureBuffer().left;
        const std::vector<float>& right = useRender ? processor_.renderBuffer().right : processor_.captureBuffer().right;

        if (recording_ || left.empty() || right.empty()) {
            for (uint32_t i = 0; i < frames; ++i) {
                outputs[0][i] = haveInputs ? inputs[0][i] : 0.0f;
                outputs[1][i] = haveInputs ? inputs[1][i] : 0.0f;
            }
            return;
        }

        std::size_t written = 0;
        while (written < frames) {
            if (playhead_ >= left.size() || playhead_ >= right.size())
                playhead_ = 0;
            const std::size_t available = std::min(left.size(), right.size()) - playhead_;
            const std::size_t count = std::min<std::size_t>(frames - written, available);
            for (std::size_t i = 0; i < count; ++i) {
                outputs[0][written + i] = left[playhead_ + i];
                outputs[1][written + i] = right[playhead_ + i];
            }
            written += count;
            playhead_ += count;
        }
    }

    void publishParameters()
    {
#if DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST
        ++publishCounter_;
        if (!publishDirty_ && (publishCounter_ % 8u) != 0u)
            return;
        publishDirty_ = false;
        for (uint32_t i = static_cast<uint32_t>(core::kInputParameterCount);
             i < static_cast<uint32_t>(core::kParameterCount); ++i)
            requestParameterValueChange(i, processor_.parameter(i));
#endif
    }
};

Plugin* createPlugin()
{
    return new EnergisePlugin();
}

END_NAMESPACE_DISTRHO
