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

ParameterEnumerationValue* audioOutValues()
{
    static ParameterEnumerationValue values[3] = {
        {0.0f, core::kAudioOutNames[0]},
        {1.0f, core::kAudioOutNames[1]},
        {2.0f, core::kAudioOutNames[2]},
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

core::HostPosition toHostPosition(const TimePosition& position) noexcept
{
    core::HostPosition host {};
    host.playing = position.playing;
    if (!position.bbt.valid || !(position.bbt.beatsPerMinute > 0.0))
        return host;
    host.valid = true;
    host.bar = position.bbt.bar;
    host.beat = position.bbt.beat;
    host.tick = position.bbt.tick;
    host.ticksPerBeat = position.bbt.ticksPerBeat > 0.0 ? position.bbt.ticksPerBeat : 1920.0;
    host.beatsPerBar = position.bbt.beatsPerBar;
    host.beatType = position.bbt.beatType;
    host.bpm = position.bbt.beatsPerMinute;
    return host;
}

double absoluteBeats(const core::HostPosition& host) noexcept
{
    return static_cast<double>(host.bar - 1) * static_cast<double>(host.beatsPerBar)
           + core::beatInBar(host);
}

double positiveModulo(const double value, const double modulus) noexcept
{
    if (!(modulus > 0.0))
        return 0.0;
    double result = std::fmod(value, modulus);
    if (result < 0.0)
        result += modulus;
    return result;
}

struct PlaybackPhase {
    double start = 0.0;
    double advance = 1.0;  // loop frames consumed per audio sample
};

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
        case core::ParamId::audioOut:
            parameter.enumValues.count = 3;
            parameter.enumValues.values = audioOutValues();
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
        syncLoopFrames();
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
        // Clear only transport/runtime state; the saved break stays loopable.
        stopRuntime();
        syncLoopFrames();
    }

    void sampleRateChanged(const double newSampleRate) override
    {
        processor_.activate(static_cast<int>(std::lround(newSampleRate)));
        stopRuntime();
        syncLoopFrames();
    }

    void run(const float** inputs, float** outputs, const uint32_t frames) override
    {
        const core::HostPosition host = toHostPosition(getTimePosition());
        detectSwitchChanges();
        processor_.setHostBbtValid(host.valid && host.playing);
        if (host.valid && host.playing)
            processor_.setHostGrid(host.bpm, 0.0);

        handleRequests(host);
        maybeStartCapture(host, frames);
        const PlaybackPhase phase = computePlaybackPhase(host);
        flushMidi(frames, host, phase);
        if (recording_)
            appendCapture(inputs, frames);
        renderAudio(inputs, outputs, frames, phase);
        advancePlaybackPhase(phase, frames);
        publishParameters();
    }

private:
    core::Processor processor_ {};
    std::vector<float> recordingLeft_ {};
    std::vector<float> recordingRight_ {};
    std::size_t captureTargetFrames_ = 0;
    std::size_t captureStartOffset_ = 0;
    bool captureOffsetPending_ = false;
    bool captureArmed_ = false;
    bool recording_ = false;

    // BBT loop state. Origin is an absolute beat position; phase is in captured
    // buffer frames so audio and MIDI share one clock.
    std::size_t loopFrames_ = 0;
    int loopBars_ = core::kDefaultCaptureBars;
    double loopOriginBeats_ = 0.0;
    double loopPhaseFrames_ = 0.0;
    std::size_t playhead_ = 0;  // free-run fallback when BBT sync is off

    std::deque<core::MidiMessage> midiQueue_ {};
    std::vector<core::MidiMessage> midiPattern_ {};
    bool midiPatternReady_ = false;
    uint32_t midiClock_ = 0;
    core::AudioOutMode lastAudioOut_ = core::AudioOutMode::Off;
    bool lastMidiOut_ = true;
    bool lastBbtLoop_ = true;
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
        syncLoopFrames();
    }

    void stopRuntime()
    {
        recording_ = false;
        recordingLeft_.clear();
        recordingRight_.clear();
        captureTargetFrames_ = 0;
        captureStartOffset_ = 0;
        captureOffsetPending_ = false;
        captureArmed_ = false;
        playhead_ = 0;
        loopFrames_ = 0;
        loopBars_ = core::kDefaultCaptureBars;
        loopOriginBeats_ = 0.0;
        loopPhaseFrames_ = 0.0;
        clearMidiRuntime();
        publishDirty_ = true;
    }

    void clearMidiRuntime()
    {
        midiQueue_.clear();
        midiPattern_.clear();
        midiPatternReady_ = false;
        midiClock_ = 0;
    }

    bool midiOutEnabled() const noexcept
    {
        return core::midiOutEnabled(processor_.parameter(static_cast<uint32_t>(core::ParamId::midiOut)));
    }

    bool bbtLoopEnabled() const noexcept
    {
        return core::bbtLoopEnabled(processor_.parameter(static_cast<uint32_t>(core::ParamId::bbtLoop)));
    }

    core::AudioOutMode audioOutMode() const noexcept
    {
        return core::audioOutMode(processor_.parameter(static_cast<uint32_t>(core::ParamId::audioOut)));
    }

    void detectSwitchChanges()
    {
        const core::AudioOutMode mode = audioOutMode();
        const bool midiOn = midiOutEnabled();
        const bool loopOn = bbtLoopEnabled();
        if (mode == lastAudioOut_ && midiOn == lastMidiOut_ && loopOn == lastBbtLoop_)
            return;
        lastAudioOut_ = mode;
        lastMidiOut_ = midiOn;
        lastBbtLoop_ = loopOn;
        playhead_ = 0;
        loopPhaseFrames_ = 0.0;
        clearMidiRuntime();
    }

    void handleRequests(const core::HostPosition& host)
    {
        const core::RequestActions actions = processor_.takeRequests();
        if (actions.capture) {
            recording_ = false;
            recordingLeft_.clear();
            recordingRight_.clear();
            captureTargetFrames_ = 0;
            captureArmed_ = true;
            playhead_ = 0;
            loopPhaseFrames_ = 0.0;
            clearMidiRuntime();
        }
        if (actions.demo) {
            const auto seed = static_cast<uint32_t>(
                std::lround(processor_.parameter(static_cast<uint32_t>(core::ParamId::seed))));
            recording_ = false;
            captureArmed_ = false;
            processor_.makeDemo(static_cast<int>(std::lround(getSampleRate())), seed);
            loopOriginBeats_ = host.valid ? absoluteBeats(host) : 0.0;
            loopBars_ = core::kDefaultCaptureBars;
            playhead_ = 0;
            loopPhaseFrames_ = 0.0;
            syncLoopFrames();
            clearMidiRuntime();
        }
        if (actions.analyse)
            processor_.analyse();
        if (actions.render) {
            processor_.render();
            syncLoopFrames();
            clearMidiRuntime();
        }
        if (actions.abSwap) {
            processor_.setAbSide(!processor_.showingRender());
            playhead_ = 0;
            loopPhaseFrames_ = 0.0;
            syncLoopFrames();
            clearMidiRuntime();
        }
        if (actions.undo) {
            processor_.undo();
            playhead_ = 0;
            loopPhaseFrames_ = 0.0;
            syncLoopFrames();
            clearMidiRuntime();
        }
        if (actions.redo) {
            processor_.redo();
            playhead_ = 0;
            loopPhaseFrames_ = 0.0;
            syncLoopFrames();
            clearMidiRuntime();
        }
        if (actions.exportMidi)
            queueMidi();

        if (actions.capture || actions.analyse || actions.render || actions.abSwap ||
            actions.undo || actions.redo || actions.demo || actions.eventEdit)
            publishDirty_ = true;
    }

    void syncLoopFrames()
    {
        const std::size_t captured = std::min(processor_.captureBuffer().left.size(),
                                              processor_.captureBuffer().right.size());
        loopFrames_ = captured;
        if (processor_.showingRender() && processor_.renderReady()) {
            loopFrames_ = std::min(loopFrames_,
                                   std::min(processor_.renderBuffer().left.size(),
                                            processor_.renderBuffer().right.size()));
        }
        loopPhaseFrames_ = positiveModulo(loopPhaseFrames_,
                                          loopFrames_ > 0 ? static_cast<double>(loopFrames_) : 1.0);
    }

    std::size_t barLineOffsetFrames(const core::HostPosition& host, const uint32_t frames) const noexcept
    {
        if (!host.valid || !host.playing)
            return frames;
        const double beat = core::beatInBar(host);
        const double seconds = beat < 1e-9 ? 0.0 : core::secondsToNextBar(host);
        const long offset = std::lround(seconds * std::max(1.0, getSampleRate()));
        if (offset <= 0)
            return 0;
        if (static_cast<std::uint64_t>(offset) >= static_cast<std::uint64_t>(frames))
            return frames;
        return static_cast<std::size_t>(offset);
    }

    void maybeStartCapture(const core::HostPosition& host, const uint32_t frames)
    {
        if (!captureArmed_ || recording_)
            return;
        const std::size_t offset = barLineOffsetFrames(host, frames);
        if (!host.valid || !host.playing || offset >= frames)
            return;
        const int requestedBars = static_cast<int>(std::lround(
            processor_.parameter(static_cast<uint32_t>(core::ParamId::captureBars))));
        captureTargetFrames_ = core::captureFramesForBars(
            static_cast<int>(std::lround(getSampleRate())), host, requestedBars);
        if (captureTargetFrames_ == 0)
            return;

        loopBars_ = core::effectiveCaptureBars(host, requestedBars);
        loopOriginBeats_ = absoluteBeats(host)
                           + (offset > 0 ? static_cast<double>(host.beatsPerBar) : 0.0);
        loopFrames_ = captureTargetFrames_;
        loopPhaseFrames_ = 0.0;
        captureStartOffset_ = offset;
        captureOffsetPending_ = true;
        recordingLeft_.clear();
        recordingRight_.clear();
        recording_ = true;
        captureArmed_ = false;
        playhead_ = 0;
        processor_.setHostGrid(host.bpm, 0.0);
    }

    PlaybackPhase computePlaybackPhase(const core::HostPosition& host) const noexcept
    {
        if (!bbtLoopEnabled() || !host.valid || loopFrames_ == 0 || loopBars_ <= 0)
            return PlaybackPhase {loopPhaseFrames_, 1.0};
        const double loopBeats = static_cast<double>(loopBars_) * static_cast<double>(host.beatsPerBar);
        const double framesPerBeat = std::max(1.0, getSampleRate()) * core::beatSeconds(host);
        if (!(loopBeats > 0.0) || !(framesPerBeat > 0.0))
            return PlaybackPhase {loopPhaseFrames_, 1.0};
        if (!host.playing)
            return PlaybackPhase {loopPhaseFrames_, 0.0};
        const double elapsedBeats = absoluteBeats(host) - loopOriginBeats_;
        const double phaseBeats = positiveModulo(elapsedBeats, loopBeats);
        const double start = phaseBeats / loopBeats * static_cast<double>(loopFrames_);
        const double advance = static_cast<double>(loopFrames_) / (loopBeats * framesPerBeat);
        return PlaybackPhase {start, advance};
    }

    void advancePlaybackPhase(const PlaybackPhase& phase, const uint32_t frames) noexcept
    {
        if (loopFrames_ == 0) {
            playhead_ += frames;
            return;
        }
        if (phase.advance > 0.0) {
            loopPhaseFrames_ = positiveModulo(phase.start + static_cast<double>(frames) * phase.advance,
                                              static_cast<double>(loopFrames_));
            playhead_ = static_cast<std::size_t>(loopPhaseFrames_);
        }
    }

    void emitMidi(const core::MidiMessage& message, const uint32_t offset)
    {
        MidiEvent event {};
        event.frame = offset;
        event.size = 3;
        event.data[0] = message.status;
        event.data[1] = message.note;
        event.data[2] = message.velocity;
        event.dataExt = nullptr;
        writeMidiEvent(event);
    }

    void queueMidi()
    {
        midiQueue_.clear();
        midiClock_ = 0;
        midiPattern_ = processor_.exportMidi();
        midiPatternReady_ = !midiPattern_.empty();
        if (bbtLoopEnabled())
            return;
        const int captureRate = processor_.captureBuffer().sampleRate;
        const double ratio = captureRate > 0
                                 ? static_cast<double>(std::lround(getSampleRate())) / static_cast<double>(captureRate)
                                 : 1.0;
        for (const core::MidiMessage& message : midiPattern_) {
            core::MidiMessage queued = message;
            queued.frame = static_cast<uint32_t>(std::lround(static_cast<double>(message.frame) * ratio));
            midiQueue_.push_back(queued);
        }
    }

    void flushMidi(const uint32_t frames, const core::HostPosition& host, const PlaybackPhase& phase)
    {
        if (!midiOutEnabled()) {
            midiQueue_.clear();
            midiClock_ += frames;
            return;
        }

        if (midiPatternReady_ && bbtLoopEnabled() && loopFrames_ > 0 && phase.advance > 0.0) {
            const double loop = static_cast<double>(loopFrames_);
            const std::size_t captured = std::min(processor_.captureBuffer().left.size(),
                                                  processor_.captureBuffer().right.size());
            const double captureFrames = static_cast<double>(captured > 0 ? captured : loopFrames_);
            double segmentStart = positiveModulo(phase.start, loop);
            double elapsed = 0.0;
            const double total = static_cast<double>(frames);
            while (elapsed < total) {
                const double samplesToWrap = (loop - segmentStart) / phase.advance;
                const double segmentSamples = std::min(total - elapsed, samplesToWrap);
                if (!(segmentSamples > 0.0)) {
                    segmentStart = 0.0;
                    continue;
                }
                const double segmentEnd = segmentStart + segmentSamples * phase.advance;
                for (const core::MidiMessage& message : midiPattern_) {
                    const double eventTime = static_cast<double>(message.frame) / captureFrames * loop;
                    if (eventTime < segmentStart || eventTime >= segmentEnd)
                        continue;
                    const double offset = elapsed + (eventTime - segmentStart) / phase.advance;
                    const auto frameOffset = static_cast<uint32_t>(std::clamp(
                        std::lround(offset), 0L, static_cast<long>(frames) - 1L));
                    emitMidi(message, frameOffset);
                }
                elapsed += segmentSamples;
                segmentStart = segmentEnd >= loop - 1e-9 ? 0.0 : segmentEnd;
            }
            midiQueue_.clear();
            midiClock_ += frames;
            return;
        }

        while (!midiQueue_.empty() && midiQueue_.front().frame < midiClock_ + frames) {
            const core::MidiMessage message = midiQueue_.front();
            midiQueue_.pop_front();
            emitMidi(message, message.frame - midiClock_);
        }
        midiClock_ += frames;
        (void)host;
    }

    void appendCapture(const float** inputs, const uint32_t frames)
    {
        if (captureTargetFrames_ == 0 || inputs == nullptr || inputs[0] == nullptr || inputs[1] == nullptr)
            return;
        std::size_t begin = 0;
        if (captureOffsetPending_) {
            begin = std::min<std::size_t>(captureStartOffset_, frames);
            captureOffsetPending_ = false;
            captureStartOffset_ = 0;
        }
        if (begin >= frames)
            return;
        const std::size_t have = recordingLeft_.size();
        const std::size_t take = std::min<std::size_t>(frames - begin, captureTargetFrames_ - have);
        for (std::size_t i = 0; i < take; ++i) {
            recordingLeft_.push_back(inputs[0][begin + i]);
            recordingRight_.push_back(inputs[1][begin + i]);
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
        loopPhaseFrames_ = 0.0;
        syncLoopFrames();
        publishDirty_ = true;
    }

    void renderAudio(const float** inputs, float** outputs, const uint32_t frames,
                     const PlaybackPhase& phase)
    {
        if (outputs == nullptr || outputs[0] == nullptr || outputs[1] == nullptr)
            return;
        const bool haveInputs = inputs != nullptr && inputs[0] != nullptr && inputs[1] != nullptr;
        const core::AudioOutMode mode = audioOutMode();
        if (mode == core::AudioOutMode::Off) {
            for (uint32_t i = 0; i < frames; ++i) {
                outputs[0][i] = 0.0f;
                outputs[1][i] = 0.0f;
            }
            return;
        }
        if (mode == core::AudioOutMode::Pass || recording_ || loopFrames_ == 0) {
            for (uint32_t i = 0; i < frames; ++i) {
                outputs[0][i] = haveInputs ? inputs[0][i] : 0.0f;
                outputs[1][i] = haveInputs ? inputs[1][i] : 0.0f;
            }
            return;
        }

        const bool useRender = processor_.showingRender() && processor_.renderReady();
        const std::vector<float>& left = useRender ? processor_.renderBuffer().left : processor_.captureBuffer().left;
        const std::vector<float>& right = useRender ? processor_.renderBuffer().right : processor_.captureBuffer().right;
        if (left.size() < loopFrames_ || right.size() < loopFrames_) {
            for (uint32_t i = 0; i < frames; ++i) {
                outputs[0][i] = 0.0f;
                outputs[1][i] = 0.0f;
            }
            return;
        }

        const double loop = static_cast<double>(loopFrames_);
        for (uint32_t i = 0; i < frames; ++i) {
            const double position = positiveModulo(phase.start + static_cast<double>(i) * phase.advance, loop);
            const std::size_t first = static_cast<std::size_t>(position);
            const std::size_t second = first + 1 >= loopFrames_ ? 0 : first + 1;
            const float mix = static_cast<float>(position - static_cast<double>(first));
            outputs[0][i] = left[first] * (1.0f - mix) + left[second] * mix;
            outputs[1][i] = right[first] * (1.0f - mix) + right[second] * mix;
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
