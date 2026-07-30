#include "harmonic_atlas_core.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace downspout::harmonic_atlas {
namespace {

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

} // namespace

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
        const int root = movementRoot(value(parameters, kStyle), chord, tonic,
                                      value(parameters, kCadenceBars),
                                      static_cast<std::uint64_t>(value(parameters, kSeed)));
        const int voices = value(parameters, kVoiceCount);
        const float tension = unit(parameters, kTension);
        const bool minor = value(parameters, kStyle) == 1
            || downspout::generative::randomUnit(value(parameters, kSeed), chord + 71) < tension * 0.45f;
        std::array<int, 6> intervals {{0, minor ? 3 : 4, 7, tension > 0.45f ? 10 : 11, 14, 17}};
        const int inversion = std::min(value(parameters, kInversionRange),
            downspout::generative::randomInt(value(parameters, kSeed), chord + 13, 0, 3));
        for (int voice = 0; voice < voices; ++voice) {
            int note = 48 + root + intervals[static_cast<std::size_t>(voice)];
            if (voice < inversion)
                note += 12;
            if (unit(parameters, kVoiceLeading) > 0.5f && note > 72)
                note -= 12;
            note = std::clamp(note, 24, 96);
            state.activeNotes[static_cast<std::size_t>(state.activeCount++)] = note;
            const int velocity = std::clamp(76 + static_cast<int>(tension * 38.0f) - voice * 3, 1, 127);
            result.push(frame, downspout::generative::status(true, channel),
                        static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity));
        }
        if (unit(parameters, kScaleNotes) > 0.0f
            && downspout::generative::randomUnit(value(parameters, kSeed), chord + 311)
                < unit(parameters, kScaleNotes)
            && state.activeCount < static_cast<int>(state.activeNotes.size())) {
            const int note = std::clamp(60 + root + (minor ? 2 : 9), 0, 127);
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
