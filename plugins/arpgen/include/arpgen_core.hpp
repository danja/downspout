#pragma once

#include <array>
#include <cstdint>

namespace downspout::arpgen {

inline constexpr int kMaxMidiData = 4;
inline constexpr int kMaxInputEvents = 512;
inline constexpr int kMaxOutputEvents = 2048;
inline constexpr int kMaxMaterialNotes = 128;

enum Mode : int {
    MODE_CHORD = 0,
    MODE_SCALE,
    MODE_COUNT
};

enum Order : int {
    ORDER_UP = 0,
    ORDER_DOWN,
    ORDER_UP_DOWN,
    ORDER_DOWN_UP,
    ORDER_COUNT
};

enum Rate : int {
    RATE_QUARTER = 0,
    RATE_EIGHTH,
    RATE_EIGHTH_TRIPLET,
    RATE_SIXTEENTH,
    RATE_SIXTEENTH_TRIPLET,
    RATE_THIRTY_SECOND,
    RATE_COUNT
};

enum CaptureSlice : int {
    CAPTURE_QUARTER_BAR = 0,
    CAPTURE_HALF_BAR,
    CAPTURE_BAR,
    CAPTURE_COUNT
};

enum Scale : int {
    SCALE_MAJOR = 0,
    SCALE_NATURAL_MINOR,
    SCALE_HARMONIC_MINOR,
    SCALE_DORIAN,
    SCALE_MIXOLYDIAN,
    SCALE_PENTATONIC_MAJOR,
    SCALE_PENTATONIC_MINOR,
    SCALE_BLUES,
    SCALE_LYDIAN,
    SCALE_PHRYGIAN_DOMINANT,
    SCALE_COUNT
};

enum ScaleShape : int {
    SHAPE_RUN = 0,
    SHAPE_TRIAD,
    SHAPE_SEVENTH,
    SHAPE_COUNT
};

struct Controls {
    int mode = MODE_CHORD;
    int order = ORDER_UP_DOWN;
    int rate = RATE_SIXTEENTH;
    int captureSlice = CAPTURE_QUARTER_BAR;
    int key = 0;
    int scale = SCALE_MAJOR;
    int scaleShape = SHAPE_RUN;
    int octaves = 2;
    float gate = 0.72f;
    float velocityFollow = 0.8f;
    bool passInput = false;
    int outputChannel = 0;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double beatType = 4.0;
    double bpm = 120.0;
};

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, kMaxMidiData> data {};
};

using InputMidiEvent = MidiMessage;
using ScheduledMidiEvent = MidiMessage;

struct EngineState {
    Controls controls {};
    std::array<bool, 128> held {};
    std::array<std::uint8_t, 128> heldVelocity {};
    std::array<bool, 128> captured {};
    std::array<std::uint8_t, 128> capturedVelocity {};
    std::array<bool, 128> latched {};
    std::array<std::uint8_t, 128> latchedVelocity {};
    bool capturePriming = true;
    std::int64_t captureSegment = -1;
    std::uint64_t materialFingerprint = 0;
    std::uint64_t traversalStep = 0;
    int lastInputChannel = 1;
    int activeNote = -1;
    int activeChannel = 1;
    bool noteOffPending = false;
    double noteOffQuarter = 0.0;
    bool wasPlaying = false;
    bool havePosition = false;
    double previousBlockEndQuarter = 0.0;
};

struct BlockResult {
    std::array<ScheduledMidiEvent, kMaxOutputEvents> events {};
    int eventCount = 0;
    int materialCount = 0;
    int activeNote = -1;
};

[[nodiscard]] Controls clampControls(const Controls& raw) noexcept;
[[nodiscard]] double rateInQuarterNotes(int rate) noexcept;
void activate(EngineState& state) noexcept;
void deactivate(EngineState& state) noexcept;
[[nodiscard]] BlockResult processBlock(EngineState& state,
                                       const Controls& controls,
                                       const TransportSnapshot& transport,
                                       std::uint32_t nframes,
                                       double sampleRate,
                                       const InputMidiEvent* midiEvents,
                                       std::uint32_t midiEventCount) noexcept;

}  // namespace downspout::arpgen
