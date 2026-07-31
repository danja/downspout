#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::lightverb {

using downspout::generative::ParamSpec;
inline constexpr std::uint8_t kMixController = 32;
inline constexpr std::uint8_t kSpaceController = 33;
inline constexpr std::size_t kDelayLineCount = 4;

enum Parameter : std::uint32_t {
    kSpace = 0,
    kDecaySeconds,
    kDamping,
    kPreDelayMs,
    kWidth,
    kMix,
    kOutputDb,
    kMidiEnabled,
    kResetMidi,
    kStatusSpace,
    kStatusMix,
    kStatusSpaceMidi,
    kStatusMixMidi,
    kStatusTail,
    kStatusInputPeak,
    kStatusOutputPeak,
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"space", "Space", 0.0f, 1.0f, 0.48f},
    {"decay_seconds", "Decay", 0.15f, 8.0f, 1.8f},
    {"damping", "Damping", 0.0f, 1.0f, 0.46f},
    {"predelay_ms", "Pre-delay", 0.0f, 100.0f, 14.0f},
    {"width", "Stereo width", 0.0f, 1.0f, 0.82f},
    {"mix", "Wet mix", 0.0f, 1.0f, 0.20f},
    {"output_db", "Output trim", -12.0f, 6.0f, 0.0f},
    {"midi_enabled", "MIDI producer control", 0.0f, 1.0f, 1.0f, true},
    {"reset_midi", "Release MIDI takeover", 0.0f, 1.0f, 0.0f, true},
    {"status_space", "Effective space", 0.0f, 1.0f, 0.48f, false, true},
    {"status_mix", "Effective wet mix", 0.0f, 1.0f, 0.20f, false, true},
    {"status_space_midi", "Space MIDI takeover", 0.0f, 1.0f, 0.0f, true, true},
    {"status_mix_midi", "Mix MIDI takeover", 0.0f, 1.0f, 0.0f, true, true},
    {"status_tail", "Tail energy", 0.0f, 1.0f, 0.0f, false, true},
    {"status_input_peak", "Input peak", 0.0f, 1.0f, 0.0f, false, true},
    {"status_output_peak", "Output peak", 0.0f, 1.0f, 0.0f, false, true},
}};

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
    float space = 0.48f;
    float mix = 0.20f;
    bool spaceMidi = false;
    bool mixMidi = false;
    float tail = 0.0f;
    float inputPeak = 0.0f;
    float outputPeak = 0.0f;
};

struct State {
    std::array<std::vector<float>, kDelayLineCount> delays;
    std::array<std::uint32_t, kDelayLineCount> writeHeads {};
    std::array<float, kDelayLineCount> damped {};
    std::vector<float> preDelayLeft;
    std::vector<float> preDelayRight;
    std::uint32_t preDelayWrite = 0;
    double sampleRate = 48000.0;
    float smoothedSpace = 0.48f;
    float smoothedMix = 0.20f;
    float smoothedPreDelayFrames = 1.0f;
    bool smoothingInitialized = false;
    bool spaceMidiActive = false;
    bool mixMidiActive = false;
    float spaceMidiValue = 0.0f;
    float mixMidiValue = 0.0f;
};

void prepare(State& state, double sampleRate);
void reset(State& state) noexcept;
void releaseMidiTakeover(State& state) noexcept;
[[nodiscard]] Parameters clampParameters(const Parameters& parameters) noexcept;
[[nodiscard]] bool handleMidi(State& state, const std::uint8_t* data,
                              std::uint32_t size, bool enabled) noexcept;
[[nodiscard]] float effectiveSpace(const State& state, const Parameters& parameters) noexcept;
[[nodiscard]] float effectiveMix(const State& state, const Parameters& parameters) noexcept;
[[nodiscard]] OutputStatus process(State& state, const Parameters& parameters,
                                   std::uint32_t frames, const AudioBlock& audio,
                                   const MidiControlEvent* midiEvents = nullptr,
                                   std::uint32_t midiEventCount = 0) noexcept;

} // namespace downspout::lightverb
