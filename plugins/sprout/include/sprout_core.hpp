#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace downspout::sprout {

using downspout::generative::MidiBlock;
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
}};

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

MidiBlock process(State&, const std::array<float, kParameterCount>&, const Transport&, std::uint32_t frames,
                  double sampleRate) noexcept;

}  // namespace downspout::sprout
