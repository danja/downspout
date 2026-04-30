#pragma once

#include <array>
#include <cstdint>

namespace downspout::ground {

inline constexpr int kMaxPhraseCount = 32;
inline constexpr int kMaxPatternSteps = 1024;
inline constexpr int kMaxEvents = 1024;
inline constexpr int kMaxScheduledMidiEvents = (kMaxEvents * 2) + 32;
inline constexpr int kPatternStateVersion = 1;
inline constexpr int kVariationStateVersion = 1;
inline constexpr int kSafetyGapSamples = 1;

enum class ScaleId : std::int32_t {
    minor = 0,
    major,
    dorian,
    phrygian,
    pentMinor,
    blues,
    mixolydian,
    harmonicMinor,
    pentMajor,
    locrian,
    phrygianDominant,
    count
};

enum class StyleId : std::int32_t {
    grounded = 0,
    ostinato,
    march,
    pulse,
    drone,
    climb,
    count
};

enum class PhraseRoleId : std::int32_t {
    statement = 0,
    answer,
    climb,
    pedal,
    breakdown,
    cadence,
    release,
    count
};

struct Controls {
    int rootNote = 36;
    ScaleId scale = ScaleId::minor;
    StyleId style = StyleId::grounded;
    int channel = 1;
    int formBars = 16;
    int phraseBars = 4;
    float density = 0.45f;
    float motion = 0.55f;
    float tension = 0.45f;
    float cadence = 0.50f;
    int reg = 1;
    float registerArc = 0.40f;
    float sequence = 0.35f;
    float vary = 0.0f;
    std::uint32_t seed = 1u;
    int actionNewForm = 0;
    int actionNewPhrase = 0;
    int actionMutateCell = 0;
};

struct NoteEvent {
    std::int32_t startStep = 0;
    std::int32_t durationSteps = 0;
    std::int32_t note = 0;
    std::int32_t velocity = 0;
};

struct PhrasePlan {
    PhraseRoleId role = PhraseRoleId::statement;
    std::int32_t startBar = 0;
    std::int32_t bars = 0;
    std::int32_t startStep = 0;
    std::int32_t stepCount = 0;
    std::int32_t eventStartIndex = 0;
    std::int32_t eventCount = 0;
    std::int32_t rootDegree = 0;
    std::int32_t registerOffset = 0;
    float intensity = 0.0f;
    float motionBias = 0.0f;
};

struct FormState {
    std::int32_t version = kPatternStateVersion;
    std::int32_t formBars = 0;
    std::int32_t phraseBars = 0;
    std::int32_t phraseCount = 0;
    std::int32_t patternSteps = 0;
    std::int32_t stepsPerBeat = 4;
    std::int32_t eventCount = 0;
    std::int32_t generationSerial = 0;
    std::array<PhrasePlan, kMaxPhraseCount> phrases {};
    std::array<NoteEvent, kMaxEvents> events {};
};

struct VariationState {
    std::int32_t version = kVariationStateVersion;
    std::int64_t completedFormLoops = 0;
    std::int64_t lastMutationLoop = 0;
};

struct TransportSnapshot {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

enum class MidiEventType : std::uint8_t {
    noteOff = 0,
    noteOn
};

struct ScheduledMidiEvent {
    MidiEventType type = MidiEventType::noteOn;
    std::uint32_t frame = 0;
    std::uint8_t channel = 0;
    std::uint8_t data1 = 0;
    std::uint8_t data2 = 0;
};

[[nodiscard]] Controls clampControls(const Controls& raw);
[[nodiscard]] FormState sanitizeFormState(const FormState& raw, bool* valid = nullptr);
[[nodiscard]] VariationState sanitizeVariationState(const VariationState& raw);
[[nodiscard]] bool structureControlsMatch(const Controls& a, const Controls& b);

}  // namespace downspout::ground
