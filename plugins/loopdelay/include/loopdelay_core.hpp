#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::loopdelay {

using downspout::generative::ParamSpec;
using downspout::generative::Transport;

inline constexpr float kMaximumDelaySeconds = 16.0f;
inline constexpr std::uint8_t kTimeController = 30;
inline constexpr std::uint8_t kFeedbackController = 31;
inline constexpr int kSyncChoiceCount = 7;

enum Parameter : std::uint32_t {
    kMode = 0,
    kTimeMode,
    kFreeTimeMs,
    kSyncLength,
    kFeedback,
    kMix,
    kTone,
    kPingPong,
    kOverdub,
    kOutputDb,
    kMidiEnabled,
    kClear,
    kResetMidi,
    kStatusState,
    kStatusDelayMs,
    kStatusFeedback,
    kStatusTimeMidi,
    kStatusFeedbackMidi,
    kStatusLoopProgress,
    kStatusInputPeak,
    kStatusOutputPeak,
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"mode", "Engine mode", 0.0f, 1.0f, 0.0f, true},
    {"time_mode", "Time mode", 0.0f, 1.0f, 1.0f, true},
    {"free_time_ms", "Free delay time", 20.0f, 4000.0f, 375.0f},
    {"sync_length", "Synchronized length", 0.0f, 6.0f, 2.0f, true},
    {"feedback", "Feedback", 0.0f, 1.0f, 0.55f},
    {"mix", "Wet mix", 0.0f, 1.0f, 0.35f},
    {"tone", "Repeat tone", 0.0f, 1.0f, 0.72f},
    {"ping_pong", "Ping-pong", 0.0f, 1.0f, 0.28f},
    {"overdub", "Loop overdub", 0.0f, 1.0f, 0.22f},
    {"output_db", "Output trim", -12.0f, 6.0f, 0.0f},
    {"midi_enabled", "MIDI producer control", 0.0f, 1.0f, 1.0f, true},
    {"clear", "Clear / recapture", 0.0f, 1.0f, 0.0f, true},
    {"reset_midi", "Release MIDI takeover", 0.0f, 1.0f, 0.0f, true},
    {"status_state", "Engine state", 0.0f, 2.0f, 0.0f, true, true},
    {"status_delay_ms", "Effective delay", 0.0f, 16000.0f, 500.0f, false, true},
    {"status_feedback", "Effective feedback", 0.0f, 1.0f, 0.55f, false, true},
    {"status_time_midi", "Time MIDI takeover", 0.0f, 1.0f, 0.0f, true, true},
    {"status_feedback_midi", "Feedback MIDI takeover", 0.0f, 1.0f, 0.0f, true, true},
    {"status_loop_progress", "Loop progress", 0.0f, 1.0f, 0.0f, false, true},
    {"status_input_peak", "Input peak", 0.0f, 1.0f, 0.0f, false, true},
    {"status_output_peak", "Output peak", 0.0f, 1.0f, 0.0f, false, true},
}};

inline constexpr std::array<const char*, 2> kModeNames {{"Delay", "Loop"}};
inline constexpr std::array<const char*, 2> kTimeModeNames {{"Free", "Sync"}};
inline constexpr std::array<const char*, kSyncChoiceCount> kSyncNames {{
    "1/4 beat", "1/2 beat", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars"
}};
inline constexpr std::array<const char*, 3> kStateNames {{"DELAY", "CAPTURE", "LOOP"}};

struct Parameters {
    std::array<float, kParameterCount> values {};
    Parameters();
};

struct MidiControlEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 3;
    std::array<std::uint8_t, 3> data {};
};

struct AudioBlock {
    const float* leftIn = nullptr;
    const float* rightIn = nullptr;
    float* leftOut = nullptr;
    float* rightOut = nullptr;
};

struct OutputStatus {
    int state = 0;
    float delayMs = 0.0f;
    float feedback = 0.0f;
    bool timeMidi = false;
    bool feedbackMidi = false;
    float loopProgress = 0.0f;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
};

struct State {
    std::vector<float> leftBuffer;
    std::vector<float> rightBuffer;
    std::uint32_t bufferFrames = 0;
    std::uint32_t writeHead = 0;
    double sampleRate = 48000.0;
    float smoothedDelayFrames = 1.0f;
    float smoothedFeedback = 0.0f;
    bool smoothingInitialized = false;
    float lowpassLeft = 0.0f;
    float lowpassRight = 0.0f;
    float dcInputLeft = 0.0f;
    float dcInputRight = 0.0f;
    float dcOutputLeft = 0.0f;
    float dcOutputRight = 0.0f;
    bool timeMidiActive = false;
    bool feedbackMidiActive = false;
    float timeMidiValue = 0.0f;
    float feedbackMidiValue = 0.0f;
    int previousMode = -1;
    bool loopCaptureRequested = false;
    bool loopCapturing = false;
    std::uint32_t loopLength = 1;
    std::uint32_t loopPosition = 0;
};

void prepare(State& state, double sampleRate);
void reset(State& state) noexcept;
void requestClear(State& state) noexcept;
void releaseMidiTakeover(State& state) noexcept;
[[nodiscard]] Parameters clampParameters(const Parameters& parameters) noexcept;
[[nodiscard]] bool handleMidi(State& state, const std::uint8_t* data, std::uint32_t size,
                              bool midiEnabled) noexcept;
[[nodiscard]] float effectiveDelayMilliseconds(const State& state,
                                               const Parameters& parameters,
                                               const Transport& transport) noexcept;
[[nodiscard]] float effectiveFeedback(const State& state,
                                      const Parameters& parameters) noexcept;
[[nodiscard]] OutputStatus process(State& state,
                                   const Parameters& parameters,
                                   const Transport& transport,
                                   std::uint32_t frames,
                                   const AudioBlock& audio,
                                   const MidiControlEvent* midiEvents = nullptr,
                                   std::uint32_t midiEventCount = 0) noexcept;

} // namespace downspout::loopdelay
