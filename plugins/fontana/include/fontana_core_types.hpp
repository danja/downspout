#pragma once

// Portable data model for the Fontana SoundFont player and slice sampler.
// Nothing in this header depends on DPF, pugl or any host API.

#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace downspout::fontana {

inline constexpr int kMaxVoices = 32;
inline constexpr int kMaxSlices = 128;
inline constexpr int kGeneratorCount = 61;
inline constexpr int kUnset = std::numeric_limits<int>::min();

// SoundFont 2.04 generator IDs. Unused/reserved slots are kept so a raw file
// generator number can be used as an array index directly.
enum class Gen : std::uint16_t {
    Start = 0,
    End = 1,
    StartLoop = 2,
    EndLoop = 3,
    StartCoarse = 4,
    ModLfoToPitch = 5,
    VibLfoToPitch = 6,
    ModEnvToPitch = 7,
    InitialFilterFc = 8,
    InitialFilterQ = 9,
    ModLfoToFilterFc = 10,
    ModEnvToFilterFc = 11,
    EndCoarse = 12,
    ModLfoToVolume = 13,
    ChorusSend = 15,
    ReverbSend = 16,
    Pan = 17,
    DelayModLfo = 21,
    FreqModLfo = 22,
    DelayVibLfo = 23,
    FreqVibLfo = 24,
    DelayModEnv = 25,
    AttackModEnv = 26,
    HoldModEnv = 27,
    DecayModEnv = 28,
    SustainModEnv = 29,
    ReleaseModEnv = 30,
    KeyToModEnvHold = 31,
    KeyToModEnvDecay = 32,
    DelayVolEnv = 33,
    AttackVolEnv = 34,
    HoldVolEnv = 35,
    DecayVolEnv = 36,
    SustainVolEnv = 37,
    ReleaseVolEnv = 38,
    KeyToVolEnvHold = 39,
    KeyToVolEnvDecay = 40,
    Instrument = 41,
    KeyRange = 43,
    VelRange = 44,
    StartLoopCoarse = 45,
    KeyNum = 46,
    Velocity = 47,
    InitialAttenuation = 48,
    EndLoopCoarse = 50,
    CoarseTune = 51,
    FineTune = 52,
    SampleId = 53,
    SampleModes = 54,
    ScaleTuning = 56,
    ExclusiveClass = 57,
    OverridingRootKey = 58,
    EndOper = 60,
};

// A sparse set of generator values. kUnset means "inherit from the enclosing
// zone" (preset -> instrument -> global zone), never "zero".
struct GenSet {
    std::array<int, kGeneratorCount> value {};

    GenSet() { value.fill(kUnset); }

    [[nodiscard]] bool has(Gen generator) const noexcept
    {
        const auto index = static_cast<std::size_t>(generator);
        return index < value.size() && value[index] != kUnset;
    }

    [[nodiscard]] int get(Gen generator, const int fallback = 0) const noexcept
    {
        const auto index = static_cast<std::size_t>(generator);
        if (index >= value.size() || value[index] == kUnset)
            return fallback;
        return value[index];
    }

    void set(Gen generator, const int v) noexcept
    {
        const auto index = static_cast<std::size_t>(generator);
        if (index < value.size())
            value[index] = v;
    }

    // Later layers win, but only where they actually set a value.
    void overlay(const GenSet& later) noexcept
    {
        for (std::size_t i = 0; i < value.size(); ++i)
            if (later.value[i] != kUnset)
                value[i] = later.value[i];
    }
};

[[nodiscard]] GenSet defaultGenerators() noexcept;

// One SoundFont modulator record (pmod/imod are both 10 bytes).
struct Modulator {
    std::uint16_t source = 0;
    std::uint16_t destination = 0;
    std::int16_t amount = 0;
    std::uint16_t amountSource = 0;
    std::uint16_t transform = 0;

    [[nodiscard]] bool usesLinkedDestination() const noexcept { return (destination & 0x8000u) != 0; }
    [[nodiscard]] std::uint16_t linkedIndex() const noexcept { return destination & 0x7fffu; }
};

[[nodiscard]] std::vector<Modulator> defaultModulators();

struct Zone {
    int keyLow = 0;
    int keyHigh = 127;
    int velocityLow = 0;
    int velocityHigh = 127;
    int instrument = -1;
    int sample = -1;
    bool global = false;
    GenSet generators;
    std::vector<Modulator> modulators;

    [[nodiscard]] bool matches(const int key, const int velocity) const noexcept
    {
        return key >= keyLow && key <= keyHigh && velocity >= velocityLow && velocity <= velocityHigh;
    }
};

struct Instrument {
    std::string name;
    std::vector<Zone> zones;
};

struct Preset {
    std::string name;
    int bank = 0;
    int program = 0;
    std::vector<Zone> zones;
};

struct Sample {
    std::string name;
    std::vector<float> data;  // mono, decoded float PCM
    std::uint32_t sampleRate = 44100;
    std::uint32_t start = 0;      // relative to this sample
    std::uint32_t end = 0;        // inclusive, relative to this sample
    std::uint32_t loopStart = 0;  // relative to this sample
    std::uint32_t loopEnd = 0;    // inclusive, relative to this sample
    int originalPitch = 60;
    int pitchCorrection = 0;
    std::uint16_t sampleType = 0;
    bool compressed = false;
    bool valid = false;
};

struct SoundFont {
    std::string name;
    std::string engine;
    std::string creator;
    int versionMajor = 2;
    int versionMinor = 1;
    std::vector<Sample> samples;
    std::vector<Instrument> instruments;
    std::vector<Preset> presets;
    std::vector<std::string> warnings;

    [[nodiscard]] bool empty() const noexcept { return presets.empty() || samples.empty(); }

    [[nodiscard]] const Preset* findPreset(const int bank, const int program) const noexcept
    {
        for (const Preset& preset : presets)
            if (preset.bank == bank && preset.program == program)
                return &preset;
        return nullptr;
    }

    [[nodiscard]] int presetIndex(const int bank, const int program) const noexcept
    {
        for (std::size_t i = 0; i < presets.size(); ++i)
            if (presets[i].bank == bank && presets[i].program == program)
                return static_cast<int>(i);
        return -1;
    }
};

struct Slice {
    std::uint32_t start = 0;  // inclusive frame
    std::uint32_t end = 0;    // inclusive frame
    int rootNote = -1;        // -1 = use sliceBaseNote + index
    bool muted = false;
};

struct SliceSet {
    std::string sourcePath;
    std::vector<Slice> slices;
    std::uint32_t sampleRate = 44100;
    std::uint32_t frameCount = 0;

    [[nodiscard]] bool empty() const noexcept { return slices.empty(); }
};

// A decoded WAV/Ogg sample for the slice sampler and file preview.
struct SampleSource {
    std::string path;
    std::vector<float> data;  // interleaved float PCM
    int channels = 1;
    std::uint32_t sampleRate = 44100;
    std::uint32_t loopStart = 0;  // inclusive frame
    std::uint32_t loopEnd = 0;    // inclusive frame
    bool loopEnabled = false;
    int rootNote = 60;

    [[nodiscard]] std::uint32_t frameCount() const noexcept
    {
        if (channels <= 0)
            return 0;
        return static_cast<std::uint32_t>(data.size() / static_cast<std::size_t>(channels));
    }

    [[nodiscard]] bool empty() const noexcept { return data.empty() || channels <= 0 || sampleRate == 0; }
};

struct Parameters {
    float masterVolume = 0.8f;
    float midiChannel = 0.0f;  // 0 = all channels
    float bank = 0.0f;
    float program = 0.0f;
    float transpose = 0.0f;       // semitones
    float pitchBendRange = 2.0f;  // semitones
    float attackMs = 4.0f;
    float releaseMs = 120.0f;
    float sourceMode = 0.0f;      // 0 = SoundFont, 1 = slices
    float sliceBaseNote = 36.0f;
    float sliceLoop = 0.0f;       // boolean: loop the selected slice
    float filterEnabled = 0.0f;   // boolean: extra post voice low-pass
    float filterCutoff = 20000.0f;
    // Output-only status values mirrored to the UI.
    float statusBank = -1.0f;
    float statusProgram = -1.0f;
    float statusVoices = 0.0f;
    float statusLastNote = -1.0f;
    float statusLoad = 0.0f;  // 0 idle, 1 loading, 2 ok, 3 error
};

[[nodiscard]] Parameters clampParameters(const Parameters& parameters) noexcept;

struct MidiInputEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::uint8_t data[4] {};
};

struct AudioBlock {
    float* outputs[2] {};
    int channelCount = 0;
};

struct Voice {
    bool active = false;
    bool preview = false;
    int note = -1;
    int channel = 0;
    int velocity = 100;
    int sourceIndex = -1;
    int rootNote = 60;
    int exclusiveClass = 0;
    float playbackRate = 1.0f;
    double position = 0.0;
    double endPosition = 0.0;
    double loopStart = 0.0;
    double loopEnd = 0.0;
    bool loop = false;
    float pan = 0.0f;
    float attenuationDb = 0.0f;
    std::uint64_t age = 0;

    float volume = 0.0f;
    float volumeStep = 0.0f;
    float volumeTarget = 1.0f;
    float modEnvelope = 0.0f;
    float modStep = 0.0f;
    float modTarget = 0.0f;
    float lfoPhase = 0.0f;
    float vibPhase = 0.0f;
    float filterState[2] {};
    float filterCutoff = 20000.0f;
    float filterQ = 0.707f;
    bool release = false;
    float releaseStart = 0.0f;
    float releaseRate = 0.0f;
    std::array<Modulator, 64> modulators {};
    int modulatorCount = 0;
    std::array<int, kGeneratorCount> generators {};
};

struct EngineState {
    std::array<Voice, kMaxVoices> voices {};
    std::array<float, 16> channelBend {};
    std::array<int, 128> cc {};
    std::atomic<std::uint32_t>* previewSerial = nullptr;
    std::uint32_t seenPreviewSerial = 0;
    std::uint64_t frameCounter = 0;

    EngineState() = default;
    EngineState(const EngineState&) = delete;
    EngineState& operator=(const EngineState&) = delete;
};

void activate(EngineState& state, double sampleRate);
void resetVoices(EngineState& state) noexcept;

// Convert SoundFont timecents to seconds. Values <= -12000 are treated as zero.
[[nodiscard]] double timecentsToSeconds(int timecents) noexcept;
// Centibels of attenuation to a linear gain.
[[nodiscard]] float centibelsToGain(int centibels) noexcept;
// Absolute filter cutoff cents to Hz ( MIDI note 0 = 8.1758 Hz ).
[[nodiscard]] double centsToHz(double cents) noexcept;

}  // namespace downspout::fontana
