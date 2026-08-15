#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace downspout::campione {

inline constexpr int kMaxVoices = 8;
inline constexpr int kMaxZones  = 128;

struct SampleZone {
    int rootNote = 60;
    int rangeLow = 0;
    int rangeHigh = 127;
    int midiChannel = 0;       // 0 = all channels, 1-16 = specific
    std::vector<float> data;   // interleaved float PCM
    int channelCount = 1;
    double sampleRate = 44100.0;
    bool loopEnabled = false;
    std::uint32_t loopStart = 0;
    std::uint32_t loopEnd = 0;
    std::uint32_t crossfadeFrames = 0;
    std::string sourcePath;
};

struct Voice {
    bool active = false;
    int zoneIndex = -1;
    int midiNote = -1;
    int midiChannel = 0;
    float velocity = 1.0f;
    double position = 0.0;
    double playbackRate = 1.0;
    bool inCrossfade = false;
    double crossfadePosition = 0.0;
    std::uint64_t ageFrames = 0;
};

struct Parameters {
    float masterVolume = 0.8f;
    float midiChannel = 0.0f;       // 0 = all, 1-16 = specific
    float crossfadeDurationMs = 20.0f;
    float pitchBendRange = 2.0f;
};

struct EngineState {
    std::array<Voice, kMaxVoices> voices {};
    std::uint64_t frameCounter = 0;
};

struct MidiInputEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::uint8_t data[4] {};
};

struct AudioBlock {
    float* outputs[2] {};
    int channelCount = 0;
};

}  // namespace downspout::campione
