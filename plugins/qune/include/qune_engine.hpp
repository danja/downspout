#pragma once

#include "qune_history.hpp"
#include "qune_params.hpp"
#include "qune_pitch.hpp"
#include "qune_shifters.hpp"

#include <array>
#include <cstdint>

namespace downspout::qune {

// ── Target selection ───────────────────────────────────────────────────────

struct NoteChoice {
    bool valid = false;
    int note = 0;  // MIDI note number
};

// Nearest note whose pitch class is set in `absoluteMask` (bit 0 = C). `previous`
// is the note chosen last time, or -1; it is kept while it is a neighbour of the
// best note and no more than `hysteresisSemitones` further from `midi`.
[[nodiscard]] NoteChoice nearestInMask(float midi, std::uint16_t absoluteMask, int previous,
                                       float hysteresisSemitones);

// ── Engine ─────────────────────────────────────────────────────────────────

inline constexpr int kSubBlock = 32;
inline constexpr std::size_t kMaxHeldNotes = 32;

// A note message for the host, `frame` counted from the start of the
// processBlock call that produced it.
struct MidiOutEvent {
    std::uint32_t frame = 0;
    std::uint8_t data[3] = {0, 0, 0};
};

inline constexpr std::size_t kMidiOutCapacity = 64;

struct EngineState {
    double sampleRate = 48000.0;
    bool prepared = false;

    History history;
    PitchTracker tracker;
    FastShifter fast;
    PsolaShifter psola;
    SpectralShifter spectral;

    // MIDI
    std::array<std::uint8_t, kMaxHeldNotes> noteStack {};  // oldest first
    std::size_t noteCount = 0;
    std::array<bool, 128> pendingRelease {};  // released while the pedal was down
    bool pedal = false;
    float bendSemitones = 0.0f;

    // Control
    std::uint32_t lastHop = 0;
    bool voiced = false;
    bool hasTarget = false;
    int previousTarget = -1;
    float detectedNote = 0.0f;
    float targetNote = 0.0f;
    float shiftTarget = 0.0f;
    float shiftSmoothed = 0.0f;
    float lastPeriod = 0.0f;

    // MIDI out: the one note currently sounding downstream (-1 = none).
    int outNote = -1;  // candidate for the current target
    int soundingNote = -1;
    int soundingChannel = 0;
    std::array<MidiOutEvent, kMidiOutCapacity> midiOut {};
    std::size_t midiOutCount = 0;

    // Quality / range changes fade the wet signal out, swap, and fade back in.
    int activeQuality = 1;
    int activeRange = -1;
    bool switching = false;
    float switchGain = 1.0f;

    std::array<float, kSubBlock> mono {};
    std::array<float, kSubBlock> wetLeft {};
    std::array<float, kSubBlock> wetRight {};
};

// Allocates. Call from a non-realtime thread (constructor, activate, sample
// rate change).
void activate(EngineState& state, double sampleRate);

// Clears runtime audio and MIDI state only. Never touches the parameters.
void reset(EngineState& state);

// Applies quality and range at once, outside the audio callback, so the latency
// can be reported before the first block. Inside the callback a change is
// instead faded around (see processBlock).
void configure(EngineState& state, const Parameters& parameters);

void midiEvent(EngineState& state, const Parameters& parameters, const std::uint8_t* data, int size);

void processBlock(EngineState& state, const Parameters& parameters, const float* inLeft, const float* inRight,
                  float* outLeft, float* outRight, std::uint32_t frames);

// Note-on / note-off messages produced by the last processBlock call, in frame
// order. They are cleared at the start of each call, so drain after every call.
[[nodiscard]] inline const MidiOutEvent* midiOutEvents(const EngineState& s) noexcept { return s.midiOut.data(); }
[[nodiscard]] inline std::size_t midiOutCount(const EngineState& s) noexcept { return s.midiOutCount; }

[[nodiscard]] int latencySamples(const EngineState& state);
[[nodiscard]] int latencySamplesFor(const EngineState& state, int quality);

// Live status for the panel.
[[nodiscard]] inline float detectedNote(const EngineState& s) noexcept { return s.voiced ? s.detectedNote : 0.0f; }
[[nodiscard]] inline float targetNote(const EngineState& s) noexcept { return (s.voiced && s.hasTarget) ? s.targetNote : 0.0f; }
[[nodiscard]] inline float shiftSemitones(const EngineState& s) noexcept { return s.shiftSmoothed; }

}  // namespace downspout::qune
