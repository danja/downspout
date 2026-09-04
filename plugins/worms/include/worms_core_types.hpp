#pragma once

#include "downspout/meter.hpp"

#include <array>
#include <cstdint>

namespace downspout::worms {

// Six hex-lattice directions: E=0, NE=1, NW=2, W=3, SW=4, SE=5 (clockwise)
enum class Direction : int { E=0, NE=1, NW=2, W=3, SW=4, SE=5 };

// 2D Tonnetz coordinate (q=fifths axis, r=major-thirds axis)
// Pitch class = ((q*7 + r*4) % 12 + 12) % 12
struct TonnetzPos {
    int q = 0;
    int r = 0;
};

// Worm rule table: turn[D] ∈ {0..4} determines next direction given incoming D
// 0=L120, 1=L60, 2=Straight, 3=R60, 4=R120 (relative to incoming direction)
struct WormRule {
    std::array<int, 6> turn { 1, 2, 3, 0, 1, 2 };
};

// Scale enum matching BassGen/Melgen canonical ordering (docs/scales.md)
enum class ScaleId : std::int32_t {
    major = 0,
    ionian,
    minor,
    harmonicMinor,
    melodicMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    phrygianDominant,
    neapolitanMajor,
    neapolitanMinor,
    pentMajor,
    pentMinor,
    blues,
    wholeTone,
    altered,
    halfWholeDiminished,
    wholeHalfDiminished,
    bebopDominant,
    bebopMajor,
    bebopMinor,
    count
};

// Step size: quarter=1, eighth=2, sixteenth=4, thirtySecond=8 steps per beat
enum class StepSizeId : int { quarter=0, eighth=1, sixteenth=2, thirtySecond=3, count=4 };

// Pattern length in steps
enum class PatternLenId : int { s16=0, s32=1, s64=2, s128=3, count=4 };

inline constexpr int kMaxPatternEvents = 128;
inline constexpr int kMaxScheduledEvents = 300;
inline constexpr int kSafetyGapSamples = 1;

struct Controls {
    int   root      = 0;       // 0-11 root pitch class (C=0)
    int   reg       = 2;       // 0-4 register
    int   stepSize  = 1;       // StepSizeId
    int   patLen    = 1;       // PatternLenId
    float density   = 0.8f;
    float velocity  = 0.75f;
    float vary      = 0.2f;
    float seed      = 0.0f;    // 0-1 normalized → internal uint seed
    int   condCh    = 0;       // 0=off, 1-16=Conductor channel
    WormRule rule;
    bool  quantize  = false;
    int   scale     = 0;       // ScaleId
    int   midiCh    = 1;       // 1-16 output MIDI channel
    int   actionRandomize = 0; // incremented to trigger
    int   actionMutate    = 0;
};

struct NoteEvent {
    std::int32_t startStep    = 0;
    std::int32_t durationSteps = 1;
    std::int32_t note         = 60;
    std::int32_t velocity     = 96;
};

struct PatternState {
    int patternSteps    = 32;
    int stepsPerBeat    = 2;
    int stepsPerBar     = 8;
    int eventCount      = 0;
    int generationSerial = 0;
    ::downspout::Meter meter {};
    TonnetzPos endPos {};     // worm position after last step (for seamless continuation)
    Direction  endDir = Direction::E;
    std::array<NoteEvent, kMaxPatternEvents> events {};
};

struct TransportSnapshot {
    bool valid       = false;
    bool playing     = false;
    double bar       = 0.0;
    double barBeat   = 0.0;
    double beatsPerBar = 4.0;
    double beatType  = 4.0;
    double bpm       = 120.0;
    ::downspout::Meter meter {};
};

enum class MidiEventType : std::uint8_t { noteOff=0, noteOn };

struct ScheduledMidiEvent {
    MidiEventType type = MidiEventType::noteOn;
    std::uint32_t frame = 0;
    std::uint8_t  channel = 0;
    std::uint8_t  note    = 0;
    std::uint8_t  velocity = 0;
};

}  // namespace downspout::worms
