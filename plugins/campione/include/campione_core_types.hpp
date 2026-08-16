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
    // ADSR
    float attackMs     = 5.0f;
    float decayMs      = 100.0f;
    float sustainLevel = 1.0f;
    float releaseMs    = 200.0f;
    // Filter
    int   filterType     = 0;       // 0=LP 1=BP 2=HP 3=Notch
    float filterCutoffHz = 20000.0f;
    float filterQ        = 0.707f;
    bool  filterEnabled  = false;
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
    // ADSR
    enum class AdsrPhase : int { Idle=0, Attack, Decay, Sustain, Release };
    AdsrPhase adsrPhase = AdsrPhase::Idle;
    float     adsrValue = 0.0f;
    // Biquad filter state (one per output channel)
    float bqZ1[2] = {};
    float bqZ2[2] = {};
    // Biquad coefficients (set at note-on from zone params)
    float bqB0 = 1.0f, bqB1 = 0.0f, bqB2 = 0.0f;
    float bqA1 = 0.0f, bqA2 = 0.0f;
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
