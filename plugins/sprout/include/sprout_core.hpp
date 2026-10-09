#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace downspout::sprout {

using downspout::generative::MidiBlock;
using downspout::generative::MidiEvent;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;

inline constexpr int kPresetCount = 7;
// Scales use the canonical ordering in docs/scales.md (the same 24 as plank, 0 =
// chromatic, append-only).
inline constexpr int kScaleCount = 24;
inline constexpr int kScaleMajor = 1;
inline constexpr int kScaleMinor = 3;
inline constexpr int kScaleBebopMinor = 23;
inline constexpr int kMaxGeneration = 16;
// Step grid: musical divisions of the quarter note, plus one bar of the host's time
// signature. The Param value is an index into this list.
inline constexpr int kGridCount = 12;
inline constexpr int kGridSixteenth = 2;
inline constexpr int kGridBar = kGridCount - 1;
inline constexpr std::size_t kMaxSymbols = 131072;  // an expansion longer than this is not used (~8k bars at 1/16)

enum Param : std::uint32_t {
    kPreset,
    kGenerations,
    kGrowBars,
    kScale,
    kRoot,
    kStepSize,
    kRange,
    kGrid,
    kGate,
    kProbability,
    kVelocity,
    kChannel,
    kSeed,
    kStatusLength,
    kStatusGeneration,
    kStatusStep,
    // Appended (after the status outputs) so saved projects keep their meaning.
    kPitchSource,    // 0 = Scale (the original behaviour), 1 = held notes, 2 = latched notes (MIDI input)
    kInputChannel,   // channel whose notes count as held; 0 = all
    kCcChannel,      // channel listened to for the Drift CC set (CC 1-4); 0 = off
    kConductorCh,    // channel listened to for the Conductor CC set (CC 21-24); 0 = off
    kParameterCount
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"preset", "Grammar", 0, kPresetCount - 1, 0, true},
    {"generations", "Generations", 1, kMaxGeneration, 4, true},
    {"grow_bars", "Grow every", 0, 16, 0, true},
    {"scale", "Scale", 0, kScaleCount - 1, kScaleMajor, true},
    {"root", "Root note", 0, 127, 60, true},
    {"step_size", "Step size", 1, 3, 1, true},
    {"range", "Range", 3, 28, 14, true},
    {"grid", "Step grid", 0, kGridCount - 1, kGridSixteenth, true},
    {"gate", "Gate", 0.1f, 1.0f, 0.7f},
    {"probability", "Probability", 0.0f, 1.0f, 1.0f},
    {"velocity", "Velocity", 1, 127, 90, true},
    {"channel", "Channel", 1, 16, 1, true},
    {"seed", "Seed", 1, 65535, 1, true},
    {"status_length", "Pattern steps", 0, static_cast<float>(kMaxSymbols), 0, true, true},
    {"status_generation", "Generation", 0, kMaxGeneration, 0, true, true},
    {"status_step", "Current step", 0, static_cast<float>(kMaxSymbols), 0, true, true},
    {"pitch_source", "Pitch source", 0, 2, 0, true},
    {"input_channel", "Input channel", 0, 16, 0, true},
    {"cc_channel", "CC channel", 0, 16, 0, true},
    {"conductor_ch", "Conductor ch", 0, 16, 0, true},
}};

inline constexpr int kPitchScale = 0;
inline constexpr int kPitchHeld = 1;     // the notes held down right now
inline constexpr int kPitchLatched = 2;  // like held, but the chord stays after the keys are released

// One time step of a pattern. `unit` is the turtle's pitch in +/- units (the
// Step size control scales it to scale degrees), `depth` is the bracket nesting.
struct Step {
    std::int16_t unit = 0;
    std::uint8_t depth = 0;
    bool note = false;
};

struct Sequence {
    int generation = 0;  // the generation actually used (may be lower than asked)
    std::vector<Step> steps;
};

const char* presetName(int preset) noexcept;
const char* presetRules(int preset) noexcept;  // axiom and rules, for display
const char* scaleName(int scale) noexcept;
const char* gridName(int grid) noexcept;  // "1/16", "1/8T", "1/4.", "1 bar"

// Length of a grid step in quarter notes; `barQuarters` is the bar length in the
// host's time signature (used by "1 bar").
double gridQuarters(int grid, double barQuarters) noexcept;

// The turtle interpretation of generation `generation` of `preset`. If the
// expansion would exceed kMaxSymbols the deepest generation that fits is returned.
// The result lives for the program's lifetime and is safe to read from any thread.
const Sequence& sequenceFor(int preset, int generation) noexcept;

// The symbol string itself, mainly for tests.
std::string expansion(int preset, int generation);

// Builds the tables now so no allocation happens in process().
void prepare();

// Folds a degree into [-range, +range] by reflection at the edges.
int foldDegree(int degree, int range) noexcept;

// MIDI note for a scale degree relative to the root (negative degrees go below it).
int degreeToNote(int scale, int root, int degree) noexcept;

// Semitones above the root of each degree of `scale` (one octave) and their count.
const std::uint8_t* scaleIntervals(int scale, int& count) noexcept;

// The deepest generation of `preset` that fits kMaxSymbols, at most `generations`.
int usableGeneration(int preset, int generations) noexcept;

// Generation in force during `bar` when growing: 1 at bar 0, one more every
// `growBars` bars, up to `generations`. growBars == 0 means always `generations`.
int generationAtBar(int generations, int growBars, std::int64_t bar) noexcept;

struct State {
    // The chord the pitch source plays (on the Input channel). In Held mode it is the keys down
    // now. In Latched mode it survives note-off: it is replaced by the first note pressed after
    // every key was released, like an arpeggiator's latch.
    std::array<bool, 128> held {};
    int heldCount = 0;
    // Keys physically down, so a fresh press can be told from a note added to a chord.
    std::array<bool, 128> down {};
    int downCount = 0;
    // Bar the previous step fell in; the generation count is latched once per bar so a
    // change (panel, automation or CC) takes effect on a bar line.
    std::int64_t lastBar = -1;
    int latchedGenerations = 0;
    int lastGeneration = -1;
    // Conductor CC 24 asks for a restart at the next bar line; restartStep then offsets the
    // pattern so that bar begins at its first step.
    bool restartPending = false;
    std::int64_t restartStep = 0;
    int activeNote = -1;
    int activeChannel = 1;
    double offQuarter = 0.0;
    std::int64_t lastStep = -1;
    bool havePosition = false;
    double previousEnd = 0.0;
    int statusLength = 0;
    int statusGeneration = 0;
    int statusStep = 0;
};

void reset(State&) noexcept;

// Applies a block's incoming MIDI before process(): tracks held notes, and turns the two CC
// sets into parameter writes on `params` (which the caller owns and which the host then
// sees). Both CC sets are off until their channel parameter is non-zero.
//   CC channel   (Drift convention): CC 1 Probability, 2 Gate, 3 Range, 4 Generations.
//   Conductor ch: CC 21 Probability, 22 Velocity, 23 Seed, 24 = 127 restarts the pattern at the
//                 next bar line. CC 20 (Scene) is not used.
// Events are applied at the start of the block, so a note or CC takes effect on the first
// step that starts in that block or later.
void handleMidi(State& s, std::array<float, kParameterCount>& params, const MidiEvent* events,
                std::uint32_t count) noexcept;

// The note for a scale degree when the pitch source is the held notes: degree 0 is the lowest
// held note and degrees are spread across the held tones (about seven degrees per octave, so
// Range spans a similar number of octaves whatever the chord size). -1 when nothing is held.
int heldDegreeToNote(const State& s, int degree) noexcept;

MidiBlock process(State&, const std::array<float, kParameterCount>&, const Transport&, std::uint32_t frames,
                  double sampleRate) noexcept;

}  // namespace downspout::sprout
