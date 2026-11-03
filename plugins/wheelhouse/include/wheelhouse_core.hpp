#pragma once

#include <array>
#include <cstdint>

namespace downspout::wheelhouse {

// A tonewheel organ. Ninety-one tone wheels turn all the time at fixed equal-tempered pitches (MIDI notes 24-114). A key
// closes nine contacts, one per drawbar footage, each connecting a wheel to the bus through that drawbar's level. Two
// manuals and a pedalboard have their own drawbars; percussion adds the second or third harmonic of the upper manual with a
// decay; a scanner vibrato/chorus, key click, preamp overdrive and tone control finish it.
//
// Clean-room: built from the published behaviour of a tonewheel organ (footage ratios, 3 dB drawbar steps, wheel
// foldback, single-trigger percussion that silences the 1' drawbar, scanner vibrato). It shares no code with any other
// organ emulation. Constants marked "by ear" are not measurements of a Hammond.

inline constexpr int kWheelCount = 91;
inline constexpr int kFirstWheelNote = 24;                       // wheel 0
inline constexpr int kLastWheelNote = kFirstWheelNote + kWheelCount - 1;   // 114
inline constexpr int kLowestKey = 36;                            // C2, the lowest key of a 61-key manual
inline constexpr int kHighestKey = 96;                           // C7
inline constexpr int kHighestPedal = 67;                         // G4: a 32-note pedalboard from C2
inline constexpr int kDrawbarCount = 9;
inline constexpr int kPedalDrawbarCount = 2;

enum Division : int { kUpper = 0, kLower = 1, kPedal = 2 };
inline constexpr int kDivisionCount = 3;

// Semitones from the key to the wheel each footage plays: 16', 5 1/3', 8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1'.
inline constexpr std::array<int, kDrawbarCount> kFootageSemitones = {-12, -5, 0, 12, 19, 24, 28, 31, 36};
inline constexpr std::array<int, kPedalDrawbarCount> kPedalSemitones = {-12, 0};
inline constexpr int kPercussionSecond = 12;                     // 4'
inline constexpr int kPercussionThird = 19;                      // 2 2/3'

inline constexpr float kMinScannerRate = 4.0f;
inline constexpr float kMaxScannerRate = 9.0f;
inline constexpr int kVibratoModeCount = 7;                      // Off, V1, V2, V3, C1, C2, C3
inline constexpr std::array<float, 3> kScannerDepthMs = {0.20f, 0.35f, 0.55f};   // by ear
inline constexpr float kPercussionFastSeconds = 0.35f;           // to -60 dB, by ear
inline constexpr float kPercussionSlowSeconds = 1.5f;
inline constexpr float kPercussionSoftLevel = 0.5f;
inline constexpr float kMasterGain = 0.05f;                      // calibrated by tests

struct Parameters {
    std::array<float, kDrawbarCount> upper {8, 8, 8, 0, 0, 0, 0, 0, 0};
    std::array<float, kDrawbarCount> lower {0, 0, 8, 5, 3, 0, 0, 0, 0};
    std::array<float, kPedalDrawbarCount> pedal {8, 0};
    float percOn = 0.0f;
    float percHarmonic = 1.0f;   // 0 = second (4'), 1 = third (2 2/3')
    float percDecay = 0.0f;      // 0 = fast, 1 = slow
    float percLevel = 1.0f;      // 0 = soft, 1 = normal
    float vibrato = 6.0f;        // 0 Off, 1-3 V1-V3, 4-6 C1-C3
    float vibUpper = 1.0f;
    float vibLower = 0.0f;
    float scannerRate = 6.9f;
    float click = 0.3f;
    float drive = 0.15f;
    float tone = 1.0f;           // 1 = open, 0 = dull
    float width = 0.5f;
    float volume = 0.7f;
    float upperChannel = 1.0f;   // 0 = off, 1-16
    float lowerChannel = 2.0f;
    float pedalChannel = 3.0f;
};

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t status = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

inline constexpr int kDelayRing = 4096;

struct EngineState {
    std::array<double, kWheelCount> phase {};
    std::array<std::array<float, kWheelCount>, 4> gain {};     // upper, lower, pedal, percussion
    std::array<std::array<float, kWheelCount>, 4> target {};
    std::array<std::array<bool, 128>, kDivisionCount> keyDown {};
    int upperHeld = 0;
    float percEnv = 0.0f;
    float swell = 1.0f;
    std::uint32_t rng = 0x9E3779B9u;
    float clickEnv = 0.0f;
    float noisePrev = 0.0f;
    std::array<float, kDelayRing> delay {};
    int delayPos = 0;
    double scanPhase = 0.0;
    std::array<float, 2> toneState {};
    bool started = false;
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// The wheel (0-90) a key's footage plays, or -1 when the key is outside the division's range. Wheels above the top one
// fold back by octaves.
[[nodiscard]] int wheelFor(int key, int semitones) noexcept;
[[nodiscard]] double wheelFrequency(int wheel) noexcept;
[[nodiscard]] bool keyInRange(int division, int key) noexcept;

// Drawbar position 0-8 to amplitude: off at 0, then 3 dB per step up to 1.0 at 8.
[[nodiscard]] float drawbarAmplitude(float position) noexcept;

// Clears the running state: keys, gains, percussion, click, scanner, filters. Keeps nothing of the patch (there is none here).
void resetState(EngineState& state) noexcept;

void processBlock(EngineState& state, const Parameters& params, float sampleRate, std::uint32_t frames, const MidiEvent* events,
                  std::uint32_t eventCount, float* outLeft, float* outRight) noexcept;

}  // namespace downspout::wheelhouse
