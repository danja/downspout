#pragma once

#include "generative_common.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace downspout::markov {

using downspout::generative::MidiBlock;
using downspout::generative::MidiEvent;
using downspout::generative::ParamSpec;
using downspout::generative::Transport;

// The chain runs over the twelve pitch classes, counted from the Root note (state 0 is the tonic).
// A transition matrix gives, for each state, how likely every state is to come next. The melody is a
// random walk through it, restarted at every phrase, so any position renders the same notes.
inline constexpr int kStates = 12;
inline constexpr int kCells = kStates * kStates;      // [from * 12 + to]
inline constexpr int kMaxWeight = 8;                  // editable weights are 0 (never) to 8
inline constexpr int kScaleCount = 24;                // docs/scales.md canonical order, 0 = chromatic
inline constexpr int kGridCount = 12;
inline constexpr int kGridSixteenth = 2;
inline constexpr int kStyleCount = 8;

enum Param : std::uint32_t {
    kRoot,
    kScale,
    kLow,
    kOctaves,
    kOrder,
    kChaos,
    kGrid,
    kGate,
    kDensity,
    kVelocity,
    kChannel,
    kSeed,
    kPhraseBars,
    kLearn,
    kLearnChannel,
    kLearnedMix,
    kClearLearned,
    kConductorCh,
    kStatusState,
    kStatusLearned,
    kParameterCount
};

using Params = std::array<float, kParameterCount>;

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"root", "Root note", 0, 127, 60, true},
    {"scale", "Scale", 0, kScaleCount - 1, 1, true},
    {"low", "Lowest note", 12, 96, 48, true},
    {"octaves", "Octaves", 1, 4, 2, true},
    {"order", "Order", 1, 2, 1, true},
    {"chaos", "Chaos", 0.0f, 1.0f, 0.5f},
    {"grid", "Step grid", 0, kGridCount - 1, kGridSixteenth, true},
    {"gate", "Gate", 0.1f, 1.0f, 0.7f},
    {"density", "Density", 0.0f, 1.0f, 0.85f},
    {"velocity", "Velocity", 1, 127, 90, true},
    {"channel", "Channel", 1, 16, 1, true},
    {"seed", "Seed", 1, 65535, 1, true},
    {"phrase_bars", "Phrase bars", 1, 8, 2, true},
    {"learn", "Learn", 0, 1, 0, true},
    {"learn_channel", "Learn channel", 0, 16, 0, true},
    {"learned_mix", "Learned mix", 0.0f, 1.0f, 0.0f},
    {"clear_learned", "Clear learned", 0, 65535, 0, true},  // a counter: every change clears
    {"conductor_ch", "Conductor ch", 0, 16, 0, true},
    {"status_state", "Current state", -1, 11, -1, true, true},
    {"status_learned", "Learned transitions", 0, 1000000, 0, true, true},
}};

// What the chain knows. `base` is the matrix you draw or pick a style for; the learned tables count
// transitions heard on the MIDI input and are blended in by Learned mix (order 1) or used directly where
// they have data (order 2).
struct Model {
    std::array<std::uint8_t, kCells> base {};
    std::array<std::uint16_t, kCells> learned1 {};                       // [from * 12 + to]
    std::array<std::uint16_t, kCells * kStates> learned2 {};             // [(two back * 12 + one back) * 12 + to]
    std::uint32_t learnedTotal = 0;                                      // transitions counted
};

[[nodiscard]] Model defaultModel();

// ---- Styles: ready-made base matrices ------------------------------------------------------------------------
const char* styleName(int style) noexcept;
void loadStyle(Model& model, int style) noexcept;       // replaces `base`; learned data is kept
void clearBase(Model& model) noexcept;                  // all zero: every row falls back to "any note in the scale"
void randomiseBase(Model& model, std::uint64_t seed) noexcept;
void bumpCell(Model& model, int from, int to) noexcept; // +1, wrapping from kMaxWeight back to 0
// Sets one cell's weight (clamped to 0..kMaxWeight); returns the weight now stored, or -1 for a cell out of range.
int setCell(Model& model, int from, int to, int weight) noexcept;
void clearLearned(Model& model) noexcept;

// ---- Scales and grid (the same tables as Sprout: docs/scales.md) ---------------------------------------------
const char* scaleName(int scale) noexcept;
// Bit i set when semitone i above the root belongs to the scale.
[[nodiscard]] std::uint16_t scaleMask(int scale) noexcept;
const char* gridName(int grid) noexcept;
[[nodiscard]] double gridQuarters(int grid, double barQuarters) noexcept;

// ---- The chain -------------------------------------------------------------------------------------------------
// Row-wise weights actually used, after blending the learned data, masking out notes that are not in the scale
// (an empty row falls back to every allowed note) and applying Chaos as an exponent: below 0.5 it sharpens the
// favourites, above it flattens them toward uniform. Chaos 0.5 leaves the weights as drawn.
using Matrix = std::array<float, kCells>;
[[nodiscard]] Matrix effectiveMatrix(const Model& model, const Params& params);

// The weights for the next state given the last two (order 2 uses learned second-order data where it has any,
// and the order-1 row otherwise). `previous2` is -1 when unknown.
void nextRow(const Model& model, const Params& params, const Matrix& matrix, int previous2, int previous1,
             std::array<float, kStates>& row);

// Picks a state from a row by a unit random number in [0, 1).
[[nodiscard]] int sampleRow(const std::array<float, kStates>& row, float unit) noexcept;

struct WalkNote {
    int state = 0;  // pitch class above the root
    int note = 60;  // MIDI note
};

// Note `k` of the phrase. A walk from the start of the phrase, seeded by (seed, salt, phrase), so it depends only on
// those and the model.
[[nodiscard]] WalkNote walkNote(const Model& model, const Params& params, const Matrix& matrix, std::uint64_t seed,
                                std::int64_t phrase, int k);

// ---- Playback ---------------------------------------------------------------------------------------------------
struct State {
    int activeNote = -1;
    int activeChannel = 1;
    double offQuarter = 0.0;
    std::int64_t lastStep = -1;
    bool havePosition = false;
    double previousEnd = 0.0;
    int statusState = -1;
    // Conductor reset: at the next bar line the phrase count starts again and the melody is re-rolled.
    std::int64_t lastBar = -1;
    bool restartPending = false;
    std::int64_t restartStep = 0;
    int salt = 0;
    // Learning: the last two states heard (-1 = none).
    int learnPrevious1 = -1;
    int learnPrevious2 = -1;
    // The last Clear learned counter seen (-1 = none yet, so loading a project does not clear anything).
    int clearSerial = -1;
};

void reset(State& state) noexcept;

// Applies a block's incoming MIDI before process(). Returns true when the learned tables changed.
//  - With Learn on, note-ons on the Learn channel (0 = all) are counted as transitions. Feed it one melodic line.
//  - With Conductor ch set, on that channel: CC 21 Density, 22 Velocity, 23 Chaos, 24 = 127 restarts at the next
//    bar line and re-rolls the melody. CC 20 (Scene) is not used.
// Writes into `params`. Clear learned is a counter: any change to it wipes the learned tables.
bool handleMidi(State& state, Model& model, Params& params, const MidiEvent* events, std::uint32_t count) noexcept;

MidiBlock process(State& state, const Model& model, const Params& params, const Transport& transport,
                  std::uint32_t frames, double sampleRate) noexcept;

// ---- State text ---------------------------------------------------------------------------------------------------
[[nodiscard]] std::string serializeModel(const Model& model);
[[nodiscard]] std::optional<Model> deserializeModel(std::string_view text);

}  // namespace downspout::markov
