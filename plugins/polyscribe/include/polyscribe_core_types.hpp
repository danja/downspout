#pragma once

#include "polyscribe_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace downspout::polyscribe {

inline constexpr std::size_t kWindow = 4096;
inline constexpr std::size_t kDrumWindow = 1024;  // short enough to resolve percussive attacks
inline constexpr std::size_t kHop = 512;
inline constexpr std::size_t kNoteCount = 128;
inline constexpr std::size_t kMaxActive = 8;
inline constexpr std::size_t kMaxEvents = 64;
inline constexpr std::size_t kDrumBands = 3;

struct MidiOutEvent {
    std::uint32_t frame = 0;
    std::uint8_t data[3] = {0, 0, 0};
};

struct MidiOutBlock {
    std::array<MidiOutEvent, kMaxEvents> events {};
    std::size_t count = 0;
};

struct DesiredNote {
    int note = 0;
    int velocity = 0;
    float magnitude = 0.0f;
};

struct ActiveNote {
    bool active = false;
    std::uint8_t note = 0;
    std::uint8_t velocity = 0;
    std::uint8_t channel = 0;
    std::uint64_t started = 0;
    std::uint64_t lastSeen = 0;
};

struct EngineState {
    double sampleRate = 48000.0;
    bool prepared = false;

    // Analysis tables, built once at prepare() / sample-rate change.
    std::array<float, kWindow> hann {};
    float hannSum = 0.0f;
    std::array<float, kNoteCount> noteHz {};
    std::array<float, kNoteCount> goertzelCoeff {};
    std::array<float, kNoteCount> goertzelCos {};
    std::array<float, kNoteCount> goertzelSin {};
    std::array<float, kNoteCount> magnitude {};

    // Short-window drum analysis, measured only in Drums mode.
    std::array<float, kDrumWindow> drumHann {};
    float drumHannSum = 0.0f;
    std::array<float, kDrumBands> drumMagnitude {};
    std::array<float, kDrumBands> drumCoeff {};
    std::array<float, kDrumBands> drumCos {};
    std::array<float, kDrumBands> drumSin {};

    // Mono input ring. Analysis runs on absolute sample counts, so block size
    // cannot change what is measured or when a note is emitted.
    std::array<float, kWindow> ring {};
    std::size_t writeIndex = 0;
    std::uint64_t sampleCounter = 0;
    std::uint64_t nextAnalysis = kWindow;

    std::array<DesiredNote, kMaxActive> desired {};
    std::size_t desiredCount = 0;
    std::array<ActiveNote, kMaxActive> active {};

    std::array<float, kDrumBands> bandEnvelope {};
    std::array<float, kDrumBands> bandPeak {};
    std::array<bool, kDrumBands> bandArmed {};
    std::array<std::uint64_t, kDrumBands> bandLastOnset {};

    int activeType = -1;
    int activeChannel = -1;
    int activeTranspose = 0;
    int lastPitchedNote = 0;
    bool activeMidiOut = true;

    std::uint64_t gateSamples = 0;
    std::uint64_t releaseSamples = 0;
    std::uint64_t drumCooldownSamples = 0;

    float level = 0.0f;
    float flash = 0.0f;
    int lastNote = 0;
    int lastVelocity = 0;
    std::uint32_t faults = 0;

    MidiOutBlock midiOut {};
};

// Allocates nothing. Call from the constructor, activate() or a sample-rate change.
void prepare(EngineState& state, double sampleRate);

// Clears runtime state only. Never touches the patch.
void reset(EngineState& state);

// Writes pass-through audio when enabled and appends note-on/off events to
// state.midiOut, which is cleared at the start of each call.
void processBlock(EngineState& state, const Parameters& params, std::uint32_t frames,
                  const float* const* inputs, float* const* outputs);

[[nodiscard]] inline const MidiOutEvent* midiOutEvents(const EngineState& s) noexcept { return s.midiOut.events.data(); }
[[nodiscard]] inline std::size_t midiOutCount(const EngineState& s) noexcept { return s.midiOut.count; }

[[nodiscard]] inline int activeNoteCount(const EngineState& s) noexcept
{
    int n = 0;
    for (const ActiveNote& a : s.active)
        n += a.active;
    return n;
}
[[nodiscard]] inline int lastNote(const EngineState& s) noexcept { return s.lastNote; }
[[nodiscard]] inline int lastVelocity(const EngineState& s) noexcept { return s.lastVelocity; }
[[nodiscard]] inline float inputLevel(const EngineState& s) noexcept { return s.level; }
[[nodiscard]] inline float flashLevel(const EngineState& s) noexcept { return s.flash; }
[[nodiscard]] inline std::uint32_t faultCount(const EngineState& s) noexcept { return s.faults; }
[[nodiscard]] inline int latencySamples() noexcept { return static_cast<int>(kWindow / 2); }

// Band-limited analysis: a 4096-sample Hann window every 512 samples.
[[nodiscard]] inline double detectionSeconds(const EngineState& s) noexcept
{
    return s.sampleRate > 0.0 ? static_cast<double>(kWindow) / s.sampleRate : 0.0;
}

}  // namespace downspout::polyscribe
