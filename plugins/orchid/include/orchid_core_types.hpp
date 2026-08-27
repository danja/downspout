#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace downspout::orchid {

inline constexpr std::uint32_t kMaxChannels = 8;

// ── MIDI input ────────────────────────────────────────────────────────────────

struct MidiInputEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::uint8_t data[4] {};
};

// ── Scale names (index 0 = None, 1-23 in canonical order) ─────────────────────

inline constexpr int kOrchidScaleCount = 24;

inline constexpr const char* kOrchidScaleNames[kOrchidScaleCount] = {
    "None",
    "Major", "Ionian", "Minor", "Harm Minor", "Mel Minor",
    "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian",
    "Phryg Dom", "Neo Major", "Neo Minor",
    "Pent Major", "Pent Minor", "Blues",
    "Whole Tone", "Altered", "Half-Whole", "Whole-Half",
    "Bebop Dom", "Bebop Major", "Bebop Minor",
};

enum class ProcessorState : std::uint8_t {
    Pass = 0,
    Armed,
    Held,
    Release,
    Cooldown,
};

inline constexpr std::array<const char*, 5> kProcessorStateNames = {{
    "Pass",
    "Armed",
    "Held",
    "Release",
    "Cooldown",
}};

struct Parameters {
    float sensitivity = 62.0f;
    float periodicity = 72.0f;
    float stabilityMs = 55.0f;
    float pitchLow = 80.0f;
    float pitchHigh = 900.0f;
    float grid = 8.0f;
    float holdUnits = 2.0f;
    float releaseMs = 35.0f;
    float cooldownMs = 80.0f;
    float loopPeriods = 5.0f;
    float mix = 82.0f;
    float liveUnder = 18.0f;
    float captureTiming = 0.0f;
    float retrigger = 20.0f;
    float scale = 0.0f;  // 0=None, 1-20 = scale (matching bassgen ScaleId 0-19)
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
    std::array<float*, kMaxChannels> outputs {};
    std::uint32_t channelCount = 2;
};

struct DetectorStatus {
    bool voiced = false;
    float confidence = 0.0f;
    float frequencyHz = 0.0f;
    float rms = 0.0f;
    std::uint32_t stableFrames = 0;
};

struct LoopRegion {
    bool valid = false;
    std::uint32_t startFrame = 0;
    std::uint32_t lengthFrames = 1;
    float frequencyHz = 0.0f;
    float confidence = 0.0f;
};

struct EngineState {
    std::vector<float> buffer;
    std::vector<float> analysisWindow;
    std::uint32_t bufferFrames = 0;
    std::uint32_t bufferChannels = 0;
    std::uint32_t writeHead = 0;
    std::uint32_t filledFrames = 0;
    double sampleRate = 0.0;

    ProcessorState processorState = ProcessorState::Pass;
    DetectorStatus detector {};
    LoopRegion loop {};
    LoopRegion pendingLoop {};

    float previousStableFrequencyHz = 0.0f;
    std::uint32_t framesUntilAnalysis = 0;
    std::uint32_t analysisHopFrames = 0;
    std::uint32_t analysisWindowFrames = 0;
    std::uint32_t analysisDecimation = 1;

    std::uint32_t holdFramesRemaining = 0;
    std::uint32_t holdFramesTotal = 0;
    std::uint32_t releaseFramesRemaining = 0;
    std::uint32_t releaseFramesTotal = 0;
    std::uint32_t cooldownFramesRemaining = 0;
    double loopReadPosition = 0.0;
    double armedGridSerial = -1.0;
    int activeMidiNote = -1;
    double playbackRate = 1.0;

    bool transportWasUsable = false;
    double lastAbsoluteBeat = 0.0;
};

struct OutputStatus {
    ProcessorState state = ProcessorState::Pass;
    float detectorConfidence = 0.0f;
    float detectedPitchHz = 0.0f;
    float inputRms = 0.0f;
    float holdProgress = 0.0f;
    float transportUsable = 0.0f;
};

}  // namespace downspout::orchid
