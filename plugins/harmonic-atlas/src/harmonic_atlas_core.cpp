#include "harmonic_atlas_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace downspout::harmonic_atlas {
namespace {

struct ScaleDef { const int* intervals; int count; };
constexpr int kScaleMajor[]               = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleIonian[]              = {0, 2, 4, 5, 7, 9, 11};
constexpr int kScaleMinor[]               = {0, 2, 3, 5, 7, 8, 10};
constexpr int kScaleHarmonicMinor[]       = {0, 2, 3, 5, 7, 8, 11};
constexpr int kScaleMelodicMinor[]        = {0, 2, 3, 5, 7, 9, 11};
constexpr int kScaleDorian[]              = {0, 2, 3, 5, 7, 9, 10};
constexpr int kScalePhrygian[]            = {0, 1, 3, 5, 7, 8, 10};
constexpr int kScaleLydian[]              = {0, 2, 4, 6, 7, 9, 11};
constexpr int kScaleMixolydian[]          = {0, 2, 4, 5, 7, 9, 10};
constexpr int kScaleLocrian[]             = {0, 1, 3, 5, 6, 8, 10};
constexpr int kScalePhrygianDominant[]    = {0, 1, 4, 5, 7, 8, 10};
constexpr int kScaleNeapolitanMajor[]     = {0, 1, 3, 5, 7, 9, 11};
constexpr int kScaleNeapolitanMinor[]     = {0, 1, 3, 5, 7, 8, 11};
constexpr int kScalePentMajor[]           = {0, 2, 4, 7, 9};
constexpr int kScalePentMinor[]           = {0, 3, 5, 7, 10};
constexpr int kScaleBlues[]               = {0, 3, 5, 6, 7, 10};
constexpr int kScaleWholeTone[]           = {0, 2, 4, 6, 8, 10};
constexpr int kScaleAltered[]             = {0, 1, 3, 4, 6, 8, 10};
constexpr int kScaleHalfWholeDim[]        = {0, 1, 3, 4, 6, 7, 9, 10};
constexpr int kScaleWholeHalfDim[]        = {0, 2, 3, 5, 6, 8, 9, 11};
constexpr int kScaleBebopDominant[]       = {0, 2, 4, 5, 7, 9, 10, 11};
constexpr int kScaleBebopMajor[]          = {0, 2, 4, 5, 7, 8, 9, 11};
constexpr int kScaleBebopMinor[]          = {0, 2, 3, 4, 5, 7, 9, 10};
constexpr ScaleDef kScaleDefs[] = {
    {kScaleMajor, 7}, {kScaleIonian, 7}, {kScaleMinor, 7},
    {kScaleHarmonicMinor, 7}, {kScaleMelodicMinor, 7}, {kScaleDorian, 7},
    {kScalePhrygian, 7}, {kScaleLydian, 7}, {kScaleMixolydian, 7},
    {kScaleLocrian, 7}, {kScalePhrygianDominant, 7},
    {kScaleNeapolitanMajor, 7}, {kScaleNeapolitanMinor, 7},
    {kScalePentMajor, 5}, {kScalePentMinor, 5}, {kScaleBlues, 6},
    {kScaleWholeTone, 6}, {kScaleAltered, 7},
    {kScaleHalfWholeDim, 8}, {kScaleWholeHalfDim, 8},
    {kScaleBebopDominant, 8}, {kScaleBebopMajor, 8}, {kScaleBebopMinor, 8},
};

int value(const std::array<float, kParameterCount>& p, const Param id) noexcept
{
    return static_cast<int>(std::lround(
        downspout::generative::clampParam(p[static_cast<std::size_t>(id)],
                                          kParameterSpecs[static_cast<std::size_t>(id)])));
}

float unit(const std::array<float, kParameterCount>& p, const Param id) noexcept
{
    return downspout::generative::clampParam(
        p[static_cast<std::size_t>(id)], kParameterSpecs[static_cast<std::size_t>(id)]);
}

void release(State& state, MidiBlock& block, const std::uint32_t frame, const int channel) noexcept
{
    for (int i = 0; i < state.activeCount; ++i)
        block.push(frame, downspout::generative::status(false, channel),
                   static_cast<std::uint8_t>(state.activeNotes[static_cast<std::size_t>(i)]), 0);
    state.activeCount = 0;
}

int movementRoot(const int style,
                 const std::int64_t chord,
                 const int tonic,
                 const int cadence,
                 const std::uint64_t seed) noexcept
{
    if (cadence > 0 && chord > 0 && chord % cadence == cadence - 1)
        return tonic;
    constexpr std::array<std::array<int, 8>, 4> movement {{
        {{0, 5, 9, 7, 0, 2, 5, 7}},
        {{0, 2, 5, 10, 3, 7, 0, 10}},
        {{0, 4, 8, 3, 7, 11, 6, 0}},
        {{0, 3, 7, 4, 8, 5, 9, 1}},
    }};
    const int position = static_cast<int>(
        (chord + downspout::generative::randomInt(seed, chord, 0, 3)) % 8);
    return (tonic + movement[static_cast<std::size_t>(style)]
        [static_cast<std::size_t>(position)]) % 12;
}

struct Pull {
    int step;      // semitones above the tonic
    float weight;
};

// Weighted targets, by the previous chord's distance above the tonic.
int gravityTarget(const int previousDistance, const float draw) noexcept
{
    static constexpr std::array<Pull, 3> afterDominant {{{0, 0.70f}, {5, 0.10f}, {9, 0.20f}}};
    static constexpr std::array<Pull, 3> afterSubdominant {{{7, 0.55f}, {0, 0.30f}, {2, 0.15f}}};
    static constexpr std::array<Pull, 3> afterTonic {{{5, 0.40f}, {7, 0.35f}, {9, 0.25f}}};
    static constexpr std::array<Pull, 3> elsewhere {{{7, 0.40f}, {0, 0.35f}, {5, 0.25f}}};
    const auto& row = previousDistance == 7 ? afterDominant
        : previousDistance == 5 ? afterSubdominant
        : previousDistance == 0 ? afterTonic : elsewhere;
    float acc = 0.0f;
    for (const Pull& pull : row) {
        acc += pull.weight;
        if (draw < acc)
            return pull.step;
    }
    return row.back().step;
}

int rootAt(const int style, const std::int64_t chord, const int tonic, const int cadence,
           const std::uint64_t seed, const float gravity, const int depth) noexcept
{
    const int base = movementRoot(style, chord, tonic, cadence, seed);
    if (gravity <= 0.0f || chord <= 0 || depth <= 0)
        return base;
    if (cadence > 0 && chord % cadence == cadence - 1)
        return tonic;
    const float draw = downspout::generative::randomUnit(seed, static_cast<std::uint64_t>(chord) + 977);
    if (draw >= gravity * 0.85f)
        return base;
    const int previous = rootAt(style, chord - 1, tonic, cadence, seed, gravity, depth - 1);
    const int distance = ((previous - tonic) % 12 + 12) % 12;
    const float pick = downspout::generative::randomUnit(seed, static_cast<std::uint64_t>(chord) + 1409);
    return (tonic + gravityTarget(distance, pick)) % 12;
}

bool chordIsMinor(const int style, const std::uint64_t seed, const std::int64_t chord, const float tension) noexcept
{
    return style == 1 || downspout::generative::randomUnit(seed, static_cast<std::uint64_t>(chord + 71)) < tension * 0.45f;
}

struct Voicing {
    std::array<int, 6> notes {};  // ascending
    int count = 0;
};

struct VoicingSettings {
    int style;
    int tonic;
    int cadence;
    std::uint64_t seed;
    float gravity;
    float tension;
    int voices;
    int inversionRange;
    float strictness;  // the Voice-leading control, above 0.5
};

// The original stacking: fixed intervals above the root, the lowest `inversion` voices lifted an octave,
// anything above 72 dropped an octave when the control is above 0.5.
Voicing plainVoicing(const int root, const bool minor, const float tension, const int voices, const int inversion,
                     const bool fold) noexcept
{
    const std::array<int, 6> intervals {{0, minor ? 3 : 4, 7, tension > 0.45f ? 10 : 11, 14, 17}};
    Voicing v;
    v.count = voices;
    for (int voice = 0; voice < voices; ++voice) {
        int note = 48 + root + intervals[static_cast<std::size_t>(voice)];
        if (voice < inversion)
            note += 12;
        if (fold && note > 72)
            note -= 12;
        v.notes[static_cast<std::size_t>(voice)] = std::clamp(note, 24, 96);
    }
    return v;
}

// Real voice-leading: among rotations (inversions) and octave placements of the chord, pick the one whose
// sorted voices move least from the previous chord. The previous chord is itself computed the same way, a
// bounded number of chords back, so the result depends only on the chord number: loops and jumps agree.
Voicing leadVoicing(const VoicingSettings& s, const std::int64_t chord, const int depth) noexcept
{
    const int root = rootAt(s.style, chord, s.tonic, s.cadence, s.seed, s.gravity, 8);
    const bool minor = chordIsMinor(s.style, s.seed, chord, s.tension);
    if (chord <= 0 || depth <= 0)
        return plainVoicing(root, minor, s.tension, s.voices, 0, true);

    const Voicing previous = leadVoicing(s, chord - 1, depth - 1);
    const std::array<int, 6> intervals {{0, minor ? 3 : 4, 7, s.tension > 0.45f ? 10 : 11, 14, 17}};

    struct Option {
        Voicing voicing;
        double cost;
    };
    std::array<Option, 3 * 6> options {};
    int optionCount = 0;
    double best = 1.0e30;
    const int maxInversion = std::min(s.inversionRange + 1, s.voices - 1);
    for (int inversion = 0; inversion <= maxInversion; ++inversion) {
        for (const int shift : {-12, 0, 12}) {
            Voicing v;
            v.count = s.voices;
            for (int voice = 0; voice < s.voices; ++voice)
                v.notes[static_cast<std::size_t>(voice)] = 48 + root + intervals[static_cast<std::size_t>(voice)]
                    + (voice < inversion ? 12 : 0) + shift;
            std::sort(v.notes.begin(), v.notes.begin() + v.count);
            double cost = 0.5 * inversion;
            for (int i = 0; i < v.count; ++i) {
                const int target = i < previous.count ? previous.notes[static_cast<std::size_t>(i)]
                                                      : previous.notes[static_cast<std::size_t>(previous.count - 1)];
                cost += std::abs(v.notes[static_cast<std::size_t>(i)] - target);
                if (v.notes[static_cast<std::size_t>(i)] < 36 || v.notes[static_cast<std::size_t>(i)] > 84)
                    cost += 6.0;
            }
            options[static_cast<std::size_t>(optionCount++)] = {v, cost};
            best = std::min(best, cost);
        }
    }
    // Below full strictness, any option within a window of the best may be chosen (seeded).
    const double window = (1.0 - static_cast<double>(s.strictness)) * 16.0;
    int eligible[3 * 6];
    int eligibleCount = 0;
    for (int i = 0; i < optionCount; ++i)
        if (options[static_cast<std::size_t>(i)].cost <= best + window)
            eligible[eligibleCount++] = i;
    const int pick = eligibleCount > 1
        ? downspout::generative::randomInt(s.seed, static_cast<std::uint64_t>(chord) + 2113, 0, eligibleCount - 1)
        : 0;
    Voicing chosen = options[static_cast<std::size_t>(eligible[pick])].voicing;
    for (int i = 0; i < chosen.count; ++i)
        chosen.notes[static_cast<std::size_t>(i)] = std::clamp(chosen.notes[static_cast<std::size_t>(i)], 24, 96);
    return chosen;
}

} // namespace

int chordRoot(const int style, const std::int64_t chord, const int tonic, const int cadence,
              const std::uint64_t seed, const float gravity) noexcept
{
    return rootAt(style, chord, tonic, cadence, seed, gravity, 8);
}

void reset(State& state) noexcept
{
    state = {};
    state.lastChord = -1;
    state.followedRoot = -1;
}

MidiBlock process(State& state,
                  const std::array<float, kParameterCount>& parameters,
                  const Transport& transport,
                  const std::uint32_t frames,
                  const double sampleRate,
                  const MidiEvent* input,
                  const std::uint32_t inputCount) noexcept
{
    MidiBlock result;
    const int channel = value(parameters, kChannel);
    const int configuredRoot = value(parameters, kRoot);
    const bool follow = value(parameters, kFollowInput) != 0;
    for (std::uint32_t i = 0; i < inputCount; ++i) {
        const auto& event = input[i];
        if (event.size >= 3 && (event.data[0] & 0xf0) == 0x90 && event.data[2] > 0)
            state.followedRoot = event.data[1] % 12;
    }

    if (!transport.valid || !transport.playing || frames == 0) {
        release(state, result, 0, channel);
        state.wasPlaying = false;
        state.havePosition = false;
        state.lastChord = -1;
        return result;
    }

    const double bpm = std::clamp(transport.bpm, 1.0, 999.0);
    const double qpf = bpm / (60.0 * std::max(1.0, sampleRate));
    const double start = downspout::generative::absoluteQuarter(transport);
    const double end = start + qpf * frames;
    if (!state.wasPlaying
        || downspout::generative::isDiscontinuity(state.havePosition, state.previousEnd, start)) {
        release(state, result, 0, channel);
        state.lastChord = -1;
    }

    const double chordLength = downspout::generative::barLengthQuarters(transport)
        * value(parameters, kRhythmBars);
    std::int64_t chord = static_cast<std::int64_t>(std::floor((start + 1.0e-8) / chordLength));
    double boundary = static_cast<double>(chord) * chordLength;
    if (boundary < start - 1.0e-8) {
        ++chord;
        boundary += chordLength;
    }
    if (state.lastChord < 0)
        boundary = start;

    while (boundary < end - 1.0e-8) {
        const std::uint32_t frame = downspout::generative::frameAt(boundary, start, qpf, frames);
        release(state, result, frame, channel);
        const int tonic = follow && state.followedRoot >= 0 ? state.followedRoot : configuredRoot;
        const int root = chordRoot(value(parameters, kStyle), chord, tonic,
                                   value(parameters, kCadenceBars),
                                   static_cast<std::uint64_t>(value(parameters, kSeed)),
                                   unit(parameters, kGravity));
        const int voices = value(parameters, kVoiceCount);
        const float tension = unit(parameters, kTension);
        const bool minor = chordIsMinor(value(parameters, kStyle), static_cast<std::uint64_t>(value(parameters, kSeed)),
                                        chord, tension);
        const int inversion = std::min(value(parameters, kInversionRange),
            downspout::generative::randomInt(value(parameters, kSeed), chord + 13, 0, 3));
        const float strictness = unit(parameters, kVoiceLeading);
        Voicing voicing;
        if (strictness > 0.5f) {
            // Voice-leading engaged: the voicing is chosen against the previous chords (six back).
            const VoicingSettings settings {value(parameters, kStyle), tonic, value(parameters, kCadenceBars),
                                            static_cast<std::uint64_t>(value(parameters, kSeed)),
                                            unit(parameters, kGravity), tension, voices,
                                            value(parameters, kInversionRange), strictness};
            voicing = leadVoicing(settings, chord, 6);
        } else {
            voicing = plainVoicing(root, minor, tension, voices, inversion, false);
        }
        for (int voice = 0; voice < voices; ++voice) {
            const int note = voicing.notes[static_cast<std::size_t>(voice)];
            state.activeNotes[static_cast<std::size_t>(state.activeCount++)] = note;
            const int velocity = std::clamp(76 + static_cast<int>(tension * 38.0f) - voice * 3, 1, 127);
            result.push(frame, downspout::generative::status(true, channel),
                        static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity));
        }
        if (unit(parameters, kScaleNotes) > 0.0f
            && downspout::generative::randomUnit(value(parameters, kSeed), chord + 311)
                < unit(parameters, kScaleNotes)
            && state.activeCount < static_cast<int>(state.activeNotes.size())) {
            const int scaleId = std::clamp(value(parameters, kScale), 0, 22);
            const auto& sc = kScaleDefs[scaleId];
            const int degree = downspout::generative::randomInt(
                static_cast<std::uint64_t>(value(parameters, kSeed)),
                static_cast<std::uint64_t>(chord + 399), 0, sc.count - 1);
            const int note = std::clamp(60 + root + sc.intervals[degree], 0, 127);
            state.activeNotes[static_cast<std::size_t>(state.activeCount++)] = note;
            result.push(frame, downspout::generative::status(true, channel),
                        static_cast<std::uint8_t>(note), 62);
        }
        state.lastChord = chord;
        state.statusRoot = root;
        ++chord;
        boundary += chordLength;
    }

    state.wasPlaying = true;
    state.havePosition = true;
    state.previousEnd = end;
    return result;
}

} // namespace downspout::harmonic_atlas
