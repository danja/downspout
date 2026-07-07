#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::rift {

inline constexpr std::uint32_t kMaxChannels = 8;
inline constexpr std::size_t kActionCount = 7;
inline constexpr int kSequenceCellCount = 16;
inline constexpr int kSequenceCellKindCount = 9;

enum class ActionType : std::uint8_t {
    Pass = 0,
    Repeat,
    Reverse,
    Skip,
    Smear,
    Slip,
    Dub,
};

inline constexpr std::array<const char*, kActionCount> kActionNames = {{
    "Pass",
    "Repeat",
    "Reverse",
    "Skip",
    "Smear",
    "Slip",
    "Dub",
}};

enum class InputSourceMode : std::uint8_t {
    LiveInput = 0,
    Sample,
    LiveAndSample,
};

enum class SequenceCellKind : std::uint8_t {
    Empty = 0,
    Ratchet,
    HalfBeat,
    OneBeat,
    TwoBeat,
    Reverse,
    Smear,
    Slip,
    Dub,
};

inline constexpr std::array<const char*, kSequenceCellKindCount> kSequenceCellKindNames = {{
    "Empty",
    "Ratchet",
    "1/2 beat",
    "1 beat",
    "2 beat",
    "Reverse",
    "Smear",
    "Slip",
    "Dub",
}};

struct Parameters {
    float grid = 8.0f;
    float density = 0.0f;
    float dub = 0.0f;
    float damage = 45.0f;
    float memoryBars = 2.0f;
    float drift = 20.0f;
    float pitch = 0.0f;
    float mix = 100.0f;
    float blend = 20.0f;
    float hold = 0.0f;
    float sourceMode = 0.0f;
    float sampleBeats = 4.0f;
    float chop = 0.0f;
};

struct Triggers {
    std::uint32_t scatterSerial = 0;
    std::uint32_t recoverSerial = 0;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double beatType = 4.0;
    double bpm = 120.0;
    ::downspout::Meter meter {};
};

struct AudioBlock {
    std::array<const float*, kMaxChannels> inputs {};
    std::array<const float*, kMaxChannels> dryInputs {};
    std::array<float*, kMaxChannels> outputs {};
    std::uint32_t channelCount = 2;
};

struct SampleSource {
    std::vector<float> interleaved;
    std::uint32_t channelCount = 0;
    double sampleRate = 0.0;
    double sourceBpm = 0.0;
    double loopBeats = 0.0;
};

struct SamplePlayback {
    const SampleSource* source = nullptr;
    InputSourceMode mode = InputSourceMode::Sample;
    float gain = 1.0f;
    double beatOffset = 0.0;
    double loopBeats = 0.0;
};

struct SequenceCell {
    SequenceCellKind kind = SequenceCellKind::Empty;
};

struct SequencePattern {
    std::array<SequenceCell, static_cast<std::size_t>(kSequenceCellCount)> cells {};
};

struct BlockSpec {
    ActionType action = ActionType::Pass;
    double readPosition = 0.0;
    double readRate = 1.0;
    std::uint32_t sourceStartFrame = 0;
    std::uint32_t sourceLengthFrames = 1;
    std::uint32_t loopBlendFrames = 0;
    float wet = 0.0f;
    float dry = 1.0f;
    bool valid = false;
};

struct EngineState {
    std::vector<float> buffer;
    std::vector<float> sourceInputScratch;
    std::uint32_t bufferFrames = 0;
    std::uint32_t bufferChannels = 0;
    std::uint32_t writeHead = 0;
    std::uint32_t filledFrames = 0;
    double sampleRate = 0.0;

    bool transportWasPlaying = false;
    std::int64_t activeBlockSerial = -1;
    std::int64_t activeSequenceSerial = -1;
    BlockSpec activeBlock {};
    BlockSpec transitionBlock {};
    std::uint32_t transitionFramesRemaining = 0;
    std::uint32_t transitionFramesTotal = 0;

    std::uint32_t lastScatterSerial = 0;
    std::uint32_t lastRecoverSerial = 0;
    std::uint32_t scatterBlocksRemaining = 0;
    std::uint32_t recoverBlocksRemaining = 0;
    std::uint32_t sequenceBlocksRemaining = 0;

    std::uint32_t rngState = 0x2457ac13u;
};

struct ActionWeights {
    float repeat = 0.0f;
    float reverse = 0.0f;
    float skip = 0.0f;
    float smear = 0.0f;
    float slip = 0.0f;
};

struct OutputStatus {
    ActionType action = ActionType::Pass;
    float activity = 0.0f;
    int sequenceCell = -1;
};

}  // namespace downspout::rift
