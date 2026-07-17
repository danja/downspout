#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace downspout::tuney_vst {

enum class Limiter : int { wrap = 0, reflect = 1, reflectRepeat = 2 };
enum class Waveform : int { sine = 0, square = 1, triangle = 2 };
enum class TuningType : int { computed = 0, ratios = 1, table = 2 };

enum ParamId : std::uint32_t {
    kParamPlay = 0,
    kParamStop,
    kParamLoop,
    kParamRate,
    kParamAudioEnabled,
    kParamMidiEnabled,
    kParamGain,
    kParamWaveform,
    kParamDutyCycle,
    kParamAudioNoteOffset,
    kParamPolyphony,
    kParamHeadroom,
    kParamMinimumNoteMs,
    kParamMapperLength,
    kParamCaseSensitive,
    kParamInvert,
    kParamMapperOffset,
    kParamRangeLimit,
    kParamLimiter,
    kParamTuningType,
    kParamNotesPerOctave,
    kParamJustLimit,
    kParamOctaveRatio,
    kParamDetune,
    kParamRootFrequency,
    kParamRootNote,
    kParamMidiChannel,
    kParamMidiVelocity,
    kParamMidiNoteOffset,
    kParamTimingScale,
    kParamOverlapMs,
    kParamTimingSeed,
    kParamTransportSync,
    kParamCount
};

struct ParameterSpec {
    const char* name;
    const char* symbol;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer;
    bool boolean;
};

extern const std::array<ParameterSpec, kParamCount> kParameterSpecs;

struct TuneyState {
    int version = 1;
    std::string text;
    std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string noteNames = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string root = "C";
    std::string begin = "A";
    std::string end = "G";
    std::string notes;
    std::vector<int> intervals {2, 2, 1, 2, 2, 2, 1};
    int scaleOffset = 0;
    std::string ratioText;
    std::string tableText;
    int spaceMs = 100;
    int dotMs = 300;
    int commaMs = 200;
    int colonMs = 400;
    int semicolonMs = 400;
    int blankLineMs = 1000;
    bool alphaOnly = true;
    bool stripAccents = true;
};

[[nodiscard]] std::string serializeState(const TuneyState& state);
[[nodiscard]] bool deserializeState(std::string_view text, TuneyState& state, std::string* error = nullptr);
[[nodiscard]] double evaluateExpression(std::string_view text);
[[nodiscard]] std::vector<std::string> splitUtf8(std::string_view text);

struct MidiOutputEvent {
    std::uint32_t frame = 0;
    std::array<std::uint8_t, 3> data {};
};

struct ProcessResult {
    static constexpr std::size_t kMaxMidiEvents = 256;
    std::array<MidiOutputEvent, kMaxMidiEvents> midi {};
    std::size_t midiCount = 0;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

class TuneyEngine {
public:
    explicit TuneyEngine(double sampleRate = 48000.0);

    void setSampleRate(double sampleRate);
    void reset();
    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float getParameter(std::uint32_t index) const;
    [[nodiscard]] const TuneyState& state() const noexcept { return state_; }
    [[nodiscard]] std::string stateText() const;
    [[nodiscard]] bool setStateText(std::string_view text, std::string* error = nullptr);
    void setText(std::string text);
    void clearText();
    void queueTypedCharacter(std::string_view utf8);
    void startPlayback();
    void stopPlayback();
    void process(float* left, float* right, std::uint32_t frames, ProcessResult& result);
    void process(float* left, float* right, std::uint32_t frames, ProcessResult& result,
                 const TransportSnapshot& transport);

    [[nodiscard]] int mapCharacter(std::string_view utf8) const;
    [[nodiscard]] double frequencyForLogicalNote(int logicalNote) const;
    [[nodiscard]] std::size_t scheduledEventCount() const noexcept { return scheduleCounts_[preparedSchedule_.load(std::memory_order_acquire)]; }
    [[nodiscard]] bool playing() const noexcept { return playing_; }

private:
    struct ScheduledEvent { std::uint64_t sample = 0; int note = 0; bool on = false; };
    struct BeatScheduledEvent { double beat = 0.0; int note = 0; bool on = false; };
    struct Voice {
        bool active = false;
        int logicalNote = 0;
        double frequency = 440.0;
        double phase = 0.0;
        std::uint64_t age = 0;
        std::uint64_t releaseAt = UINT64_MAX;
        float releaseGain = 1.0f;
        bool midiOn = false;
        std::uint64_t serial = 0;
    };
    struct PendingTyped { std::array<char, 8> bytes {}; std::uint8_t size = 0; };

    void rebuildDerived();
    void rebuildSchedule();
    void processSynced(float* left, float* right, std::uint32_t frames,
                       ProcessResult& result, const TransportSnapshot& transport);
    void applyEvent(int note, bool on, std::uint32_t frame, ProcessResult& result,
                    bool honorMinimumNote = true);
    void allNotesOff(std::uint32_t frame, ProcessResult& result);
    void drainTyped(ProcessResult& result);
    float renderVoice(Voice& voice);
    void emitMidi(int note, bool on, std::uint32_t frame, ProcessResult& result);
    [[nodiscard]] int limitNote(int note) const;
    [[nodiscard]] int scaleTuningNumber(int note) const;

    double sampleRate_ = 48000.0;
    std::array<float, kParamCount> parameters_ {};
    TuneyState state_ {};
    std::vector<std::string> alphabet_;
    std::vector<int> scaleSteps_;
    std::vector<double> ratios_;
    std::vector<double> table_;
    static constexpr std::size_t kMaxScheduleEvents = 8192;
    std::array<std::array<ScheduledEvent, kMaxScheduleEvents>, 2> schedules_ {};
    std::array<std::array<BeatScheduledEvent, kMaxScheduleEvents>, 2> beatSchedules_ {};
    std::array<std::size_t, 2> scheduleCounts_ {};
    std::array<std::size_t, 2> beatScheduleCounts_ {};
    std::array<std::uint64_t, 2> scheduleLengths_ {};
    std::array<double, 2> beatScheduleLengths_ {};
    std::atomic<int> preparedSchedule_ {0};
    int playbackSchedule_ = 0;
    std::array<Voice, 32> voices_ {};
    std::uint64_t voiceSerial_ = 0;
    std::size_t scheduleIndex_ = 0;
    std::uint64_t playhead_ = 0;
    bool playing_ = false;
    bool stopRequested_ = false;
    bool playRequested_ = false;
    bool syncArmed_ = true;
    bool syncWasTransportPlaying_ = false;
    double syncPlayheadBeats_ = 0.0;
    double expectedHostBeat_ = -1.0;
    std::uint64_t syncCycle_ = 0;

    static constexpr std::size_t kTypedQueueSize = 64;
    std::array<PendingTyped, kTypedQueueSize> typedQueue_ {};
    std::atomic<std::size_t> typedWrite_ {0};
    std::atomic<std::size_t> typedRead_ {0};
};

} // namespace downspout::tuney_vst
