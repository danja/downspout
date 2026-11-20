#include "fontana_engine.hpp"
#include "fontana_core_types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace downspout::fontana {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float kMinFilterHz = 100.0f;
constexpr int kDefaultFilterFc = 13500;

struct Range {
    int low = 0;
    int high = 127;
};

struct FilterCoeff {
    float g = 0.0f;
    float k = 1.0f;
    float a1 = 0.0f;
    float a2 = 0.0f;
    float a3 = 0.0f;
    float lastHz = -1.0f;
    float lastQ = -1.0f;
};

struct VoiceSpec {
    GenSet generators;
    std::array<Modulator, 64> modulators {};
    int modulatorCount = 0;
    int sourceIndex = -1;  // -1 => borrowed SampleSource
    int rootNote = 60;
    double start = 0.0;
    double end = 1.0;      // exclusive
    double loopStart = 0.0;
    double loopEnd = 1.0;  // exclusive
    bool loop = false;
};

const std::vector<Modulator>& defaultModulatorCache()
{
    static const std::vector<Modulator> mods = defaultModulators();
    return mods;
}

void copyModulators(std::array<Modulator, 64>& dst, int& count, const std::vector<Modulator>& src) noexcept
{
    count = std::min(static_cast<int>(src.size()), 64);
    for (int i = 0; i < count; ++i)
        dst[static_cast<std::size_t>(i)] = src[static_cast<std::size_t>(i)];
}

int zoneInstrument(const Zone& zone) noexcept
{
    if (zone.instrument >= 0)
        return zone.instrument;
    return zone.generators.has(Gen::Instrument) ? zone.generators.get(Gen::Instrument) : -1;
}

bool isGlobalZone(const Zone& zone) noexcept
{
    return zone.global || (zoneInstrument(zone) < 0 && zone.sample < 0 &&
                           !zone.generators.has(Gen::SampleId));
}

// Key/velocity ranges are read from the raw generator bytes *before* any
// zone layering, so a later zone cannot change the match rule of an earlier one.
Range zoneRange(const Zone& zone, Gen generator, int fallbackLow, int fallbackHigh) noexcept
{
    if (zone.generators.has(generator)) {
        const int raw = zone.generators.get(generator);
        const int low = std::clamp(raw & 0xff, 0, 127);
        const int high = std::clamp((raw >> 8) & 0xff, low, 127);
        return { low, high };
    }
    return { std::clamp(fallbackLow, 0, 127), std::clamp(fallbackHigh, 0, 127) };
}

bool zoneMatches(const Zone& zone, int note, int velocity) noexcept
{
    const Range key = zoneRange(zone, Gen::KeyRange, zone.keyLow, zone.keyHigh);
    const Range vel = zoneRange(zone, Gen::VelRange, zone.velocityLow, zone.velocityHigh);
    return note >= key.low && note <= key.high && velocity >= vel.low && velocity <= vel.high;
}

float effectiveGen(const Voice& voice, Gen generator, float fallback,
                   const std::array<float, kGeneratorCount>& modulation) noexcept
{
    const auto index = static_cast<std::size_t>(generator);
    if (index >= modulation.size())
        return fallback;
    float base = static_cast<float>(voice.generators[index]);
    if (voice.generators[index] == kUnset)
        base = fallback;
    return base + modulation[index];
}

// FluidSynth-style general-controller indices (2 velocity, 3 key, 10 pressure,
// 13 channel pressure, 14 pitch wheel, 16 pitch sensitivity). Bit 7 selects a
// MIDI CC instead. Direction/polarity/transform follow the SF2 source operand.
float sourceValue(std::uint16_t source, const Voice& voice, const EngineState& state,
                  const Parameters& parameters) noexcept
{
    const int index = source & 0x7f;
    const bool isController = ((source >> 7) & 1u) != 0;
    float value = 0.0f;
    if (isController) {
        value = state.cc[std::clamp(index, 0, 127)] / 127.0f;
    } else {
        switch (index) {
        case 0:  value = 1.0f; break;
        case 2:  value = voice.velocity / 127.0f; break;
        case 3:  value = std::clamp(voice.note, 0, 127) / 127.0f; break;
        case 10: value = 0.0f; break;  // polyphonic pressure (not tracked)
        case 13: value = 0.0f; break;  // channel pressure (not tracked)
        case 14: value = (state.channelBend[std::clamp(voice.channel - 1, 0, 15)] + 1.0f) * 0.5f; break;
        case 16: value = std::clamp(parameters.pitchBendRange / 24.0f, 0.0f, 1.0f); break;
        default: value = 0.0f; break;
        }
    }
    if (((source >> 8) & 3u) == 1u)
        value = 1.0f - value;
    const int polarity = static_cast<int>((source >> 10) & 3u);
    if (polarity == 1 || polarity == 3)
        value = -value;
    switch ((source >> 12) & 3u) {
    case 1:  value = value * value; break;
    case 2:  value = value >= 0.0f ? std::sqrt(value) : -std::sqrt(-value); break;
    case 3:  value = value > 0.5f ? 1.0f : 0.0f; break;
    default: break;
    }
    return value;
}

void evaluateModulators(const EngineState& state, const Parameters& parameters, const Voice& voice,
                        std::array<float, kGeneratorCount>& destination) noexcept
{
    destination.fill(0.0f);
    for (int i = 0; i < voice.modulatorCount; ++i) {
        const Modulator& mod = voice.modulators[static_cast<std::size_t>(i)];
        if (mod.usesLinkedDestination())
            continue;
        const int dest = static_cast<int>(mod.linkedIndex());
        if (dest < 0 || dest >= kGeneratorCount)
            continue;
        float amount = static_cast<float>(mod.amount);
        if (mod.amountSource != 0)
            amount *= sourceValue(mod.amountSource, voice, state, parameters);
        destination[static_cast<std::size_t>(dest)] +=
            amount * sourceValue(mod.source, voice, state, parameters);
    }
}

double framesForTimecents(int timecents, double sampleRate) noexcept
{
    const double seconds = timecentsToSeconds(timecents);
    return seconds > 0.0 ? seconds * sampleRate : 0.0;
}

float volumeEnvelope(const Voice& voice, const std::array<float, kGeneratorCount>& modulation,
                     int note, double sampleRate, float sustain) noexcept
{
    const int keyOffset = note - 60;
    const double delay = framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::DelayVolEnv, 0.0f, modulation))), sampleRate);
    const double attack = std::max(1.0, framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::AttackVolEnv, 0.0f, modulation))), sampleRate));
    const double hold = framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::HoldVolEnv, 0.0f, modulation) +
        keyOffset * effectiveGen(voice, Gen::KeyToVolEnvHold, 0.0f, modulation))), sampleRate);
    const double decay = std::max(1.0, framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::DecayVolEnv, 0.0f, modulation) +
        keyOffset * effectiveGen(voice, Gen::KeyToVolEnvDecay, 0.0f, modulation))), sampleRate));
    const double elapsed = voice.releaseStart;
    if (elapsed < delay)
        return 0.0f;
    if (elapsed < delay + attack)
        return static_cast<float>((elapsed - delay) / attack);
    if (elapsed < delay + attack + hold)
        return 1.0f;
    if (elapsed < delay + attack + hold + decay)
        return 1.0f + (sustain - 1.0f) *
               static_cast<float>((elapsed - delay - attack - hold) / decay);
    return sustain;
}

float modEnvelope(const Voice& voice, const std::array<float, kGeneratorCount>& modulation,
                  int note, double sampleRate, float sustain) noexcept
{
    const int keyOffset = note - 60;
    const double delay = framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::DelayModEnv, 0.0f, modulation))), sampleRate);
    const double attack = std::max(1.0, framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::AttackModEnv, 0.0f, modulation))), sampleRate));
    const double hold = framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::HoldModEnv, 0.0f, modulation) +
        keyOffset * effectiveGen(voice, Gen::KeyToModEnvHold, 0.0f, modulation))), sampleRate);
    const double decay = std::max(1.0, framesForTimecents(static_cast<int>(std::lround(
        effectiveGen(voice, Gen::DecayModEnv, 0.0f, modulation) +
        keyOffset * effectiveGen(voice, Gen::KeyToModEnvDecay, 0.0f, modulation))), sampleRate));
    const double elapsed = voice.releaseStart;
    if (elapsed < delay)
        return 0.0f;
    if (elapsed < delay + attack)
        return static_cast<float>((elapsed - delay) / attack);
    if (elapsed < delay + attack + hold)
        return 1.0f;
    if (elapsed < delay + attack + hold + decay)
        return 1.0f + (sustain - 1.0f) *
               static_cast<float>((elapsed - delay - attack - hold) / decay);
    return sustain;
}

float readInterpolated(const float* data, std::size_t frames, int channels,
                       double position, int channel) noexcept
{
    if (data == nullptr || frames == 0 || channels <= 0 || position < 0.0)
        return 0.0f;
    const double clamped = std::min(position, static_cast<double>(frames - 1));
    const auto first = static_cast<std::size_t>(clamped);
    const auto second = std::min(first + 1, frames - 1);
    const int ch = std::clamp(channel, 0, channels - 1);
    const float a = data[first * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)];
    const float b = data[second * static_cast<std::size_t>(channels) + static_cast<std::size_t>(ch)];
    return a + static_cast<float>(clamped - static_cast<double>(first)) * (b - a);
}

void updateFilterCoefficients(FilterCoeff& coeff, float hz, float q, double sampleRate) noexcept
{
    if (hz == coeff.lastHz && q == coeff.lastQ)
        return;
    coeff.g = static_cast<float>(std::tan(kPi * static_cast<double>(hz) / sampleRate));
    coeff.k = 1.0f / std::max(0.01f, q);
    coeff.a1 = 1.0f / (1.0f + coeff.g * (coeff.g + coeff.k));
    coeff.a2 = coeff.g * coeff.a1;
    coeff.a3 = coeff.g * coeff.a2;
    coeff.lastHz = hz;
    coeff.lastQ = q;
}

float runFilter(FilterCoeff& coeff, Voice& voice, float input) noexcept
{
    const float v3 = input - voice.filterState[1];
    const float v1 = coeff.a1 * voice.filterState[0] + coeff.a2 * v3;
    const float v2 = voice.filterState[1] + coeff.a2 * voice.filterState[0] + coeff.a3 * v3;
    voice.filterState[0] = 2.0f * v1 - voice.filterState[0];
    voice.filterState[1] = 2.0f * v2 - voice.filterState[1];
    return v2;
}

int effectiveClass(const Voice& voice) noexcept
{
    return voice.exclusiveClass < 0 ? -voice.exclusiveClass - 1 : voice.exclusiveClass;
}

double releaseSeconds(const Voice& voice, const Parameters& parameters) noexcept
{
    double seconds = timecentsToSeconds(voice.generators[static_cast<std::size_t>(Gen::ReleaseVolEnv)]);
    if (seconds <= 0.0)
        seconds = parameters.releaseMs * 0.001;
    return std::clamp(seconds, 0.001, 2.0);
}

void beginRelease(Voice& voice, double sampleRate, double seconds) noexcept
{
    if (!voice.active || voice.release)
        return;
    if (voice.volume <= 0.0f) {
        voice.active = false;
        return;
    }
    voice.release = true;
    const float frames = static_cast<float>(std::max(1.0, seconds * sampleRate));
    voice.releaseStart = voice.volume;
    voice.releaseRate = voice.volume / frames;
    voice.modStep = voice.modEnvelope / frames;
    voice.modTarget = 0.0f;
}

void beginRelease(Voice& voice, const Parameters& parameters, double sampleRate) noexcept
{
    beginRelease(voice, sampleRate, releaseSeconds(voice, parameters));
}

int allocateVoice(EngineState& state, int note, int channel, std::uint64_t age) noexcept
{
    for (int i = 0; i < kMaxVoices; ++i) {
        const Voice& v = state.voices[static_cast<std::size_t>(i)];
        if (v.active && !v.release && v.note == note && v.channel == channel)
            return i;
    }
    for (int i = 0; i < kMaxVoices; ++i)
        if (!state.voices[static_cast<std::size_t>(i)].active)
            return i;
    int oldest = 0;
    std::uint64_t youngestAge = state.voices[0].age;
    for (int i = 1; i < kMaxVoices; ++i) {
        const std::uint64_t ageOfVoice = state.voices[static_cast<std::size_t>(i)].age;
        if (ageOfVoice < youngestAge) {
            youngestAge = ageOfVoice;
            oldest = i;
        }
    }
    (void)age;
    return oldest;
}

int sliceIndexFor(const SliceSet& slices, int note, int baseNote) noexcept
{
    int index = note - baseNote;
    if (index < 0)
        index = 0;
    if (index >= static_cast<int>(slices.slices.size()))
        index = static_cast<int>(slices.slices.size()) - 1;
    return std::max(0, index);
}

bool buildFontSpec(const SoundFont& font, const ResolvedVoice& resolved, VoiceSpec& spec) noexcept
{
    if (!resolved.found || resolved.sampleIndex < 0 ||
        resolved.sampleIndex >= static_cast<int>(font.samples.size()))
        return false;
    const Sample& sample = font.samples[static_cast<std::size_t>(resolved.sampleIndex)];
    if (sample.data.empty())
        return false;
    spec.generators = resolved.generators;
    copyModulators(spec.modulators, spec.modulatorCount, resolved.modulators);
    spec.sourceIndex = resolved.sampleIndex;
    const int root = spec.generators.get(Gen::OverridingRootKey, -1);
    spec.rootNote = root >= 0 ? root : sample.originalPitch;

    const long long total = static_cast<long long>(sample.data.size());
    const auto clampFrame = [total](long long frame) noexcept {
        return std::clamp(frame, 0LL, std::max(0LL, total - 1));
    };
    const long long start = clampFrame(static_cast<long long>(sample.start) +
        spec.generators.get(Gen::Start, 0) +
        (static_cast<long long>(spec.generators.get(Gen::StartCoarse, 0)) << 15));
    const long long end = clampFrame(static_cast<long long>(sample.end) +
        spec.generators.get(Gen::End, 0) +
        (static_cast<long long>(spec.generators.get(Gen::EndCoarse, 0)) << 15));
    long long loopStart = clampFrame(static_cast<long long>(sample.loopStart) +
        spec.generators.get(Gen::StartLoop, 0) +
        (static_cast<long long>(spec.generators.get(Gen::StartLoopCoarse, 0)) << 15));
    long long loopEnd = clampFrame(static_cast<long long>(sample.loopEnd) +
        spec.generators.get(Gen::EndLoop, 0) +
        (static_cast<long long>(spec.generators.get(Gen::EndLoopCoarse, 0)) << 15));
    if (loopEnd < loopStart)
        loopEnd = loopStart;
    if (loopEnd < start || loopStart > end) {
        loopStart = start;
        loopEnd = end;
    }
    spec.start = static_cast<double>(start);
    spec.end = static_cast<double>(end) + 1.0;
    spec.loopStart = static_cast<double>(loopStart);
    spec.loopEnd = static_cast<double>(loopEnd) + 1.0;
    const int modes = spec.generators.has(Gen::SampleModes)
                          ? spec.generators.get(Gen::SampleModes)
                          : static_cast<int>(sample.sampleType & 1u);
    spec.loop = (modes & 1) != 0;
    return true;
}

bool buildSliceSpec(const SliceSet& slices, const SampleSource& source, int sliceIndex,
                    const Parameters& parameters, VoiceSpec& spec) noexcept
{
    if (sliceIndex < 0 || sliceIndex >= static_cast<int>(slices.slices.size()) || source.empty())
        return false;
    const Slice& slice = slices.slices[static_cast<std::size_t>(sliceIndex)];
    const std::size_t frames = source.frameCount();
    if (frames == 0)
        return false;
    const std::size_t last = frames - 1;
    const std::size_t start = std::min(static_cast<std::size_t>(slice.start), last);
    const std::size_t end = std::max(start, std::min(static_cast<std::size_t>(slice.end), last));
    spec.generators = defaultGenerators();
    copyModulators(spec.modulators, spec.modulatorCount, defaultModulatorCache());
    spec.sourceIndex = -1;
    const int base = static_cast<int>(std::lround(parameters.sliceBaseNote));
    spec.rootNote = slice.rootNote >= 0 ? slice.rootNote : base + sliceIndex;
    spec.start = static_cast<double>(start);
    spec.end = static_cast<double>(end) + 1.0;
    if (source.loopEnd > source.loopStart && source.loopEnd <= frames) {
        spec.loopStart = source.loopStart;
        spec.loopEnd = static_cast<double>(source.loopEnd) + 1.0;
    } else {
        spec.loopStart = spec.start;
        spec.loopEnd = spec.end;
    }
    spec.loop = parameters.sliceLoop > 0.5f || source.loopEnabled;
    return true;
}

bool buildSampleSpec(const SampleSource& source, const Parameters& parameters, VoiceSpec& spec) noexcept
{
    if (source.empty())
        return false;
    const std::size_t frames = source.frameCount();
    spec.generators = defaultGenerators();
    copyModulators(spec.modulators, spec.modulatorCount, defaultModulatorCache());
    spec.sourceIndex = -1;
    spec.rootNote = source.rootNote;
    spec.start = 0.0;
    spec.end = static_cast<double>(frames);
    spec.loopStart = source.loopStart;
    spec.loopEnd = source.loopEnd < frames ? static_cast<double>(source.loopEnd) + 1.0
                                          : static_cast<double>(frames);
    spec.loop = source.loopEnabled;
    (void)parameters;
    return true;
}

void launchVoice(EngineState& state, const Parameters& parameters, const VoiceSpec& spec,
                 int note, int channel, int velocity, bool preview, double sampleRate,
                 std::uint64_t age) noexcept
{
    const int exclusive = preview ? 0 : spec.generators.get(Gen::ExclusiveClass, 0);
    if (exclusive > 0) {
        for (Voice& other : state.voices) {
            if (other.active && !other.release && effectiveClass(other) == exclusive)
                beginRelease(other, parameters, sampleRate);
        }
    }
    const int index = allocateVoice(state, note, channel, age);
    Voice& voice = state.voices[static_cast<std::size_t>(index)];
    voice = {};
    voice.active = true;
    voice.preview = preview;
    voice.note = note;
    voice.channel = channel;
    voice.velocity = velocity;
    voice.sourceIndex = spec.sourceIndex;
    voice.rootNote = spec.rootNote;
    voice.exclusiveClass = std::max(0, exclusive);
    voice.playbackRate = 1.0f;
    voice.position = spec.start;
    voice.endPosition = spec.end;
    voice.loopStart = spec.loopStart;
    voice.loopEnd = spec.loopEnd;
    voice.loop = spec.loop;
    voice.pan = std::clamp(spec.generators.get(Gen::Pan, 0) / 500.0f, -1.0f, 1.0f);
    voice.attenuationDb = static_cast<float>(spec.generators.get(Gen::InitialAttenuation, 0));
    voice.age = age;
    voice.filterCutoff = static_cast<float>(centsToHz(
        spec.generators.get(Gen::InitialFilterFc, kDefaultFilterFc)));
    const float qCb = std::max(0.0f, static_cast<float>(spec.generators.get(Gen::InitialFilterQ, 0)));
    voice.filterQ = std::clamp(0.707f * std::pow(10.0f, qCb / 2000.0f), 0.5f, 20.0f);
    voice.volumeTarget = 1.0f;
    voice.modTarget = 0.0f;
    voice.modulatorCount = spec.modulatorCount;
    voice.modulators = spec.modulators;
    std::copy(spec.generators.value.begin(), spec.generators.value.end(), voice.generators.begin());
}

void noteOff(EngineState& state, const Parameters& parameters, int channel, int note,
             double sampleRate) noexcept
{
    const bool sustained = state.cc[64] >= 64;
    for (Voice& voice : state.voices) {
        if (!voice.active || voice.release || voice.preview)
            continue;
        if (voice.channel != channel || voice.note != note)
            continue;
        if (sustained) {
            // Negative exclusive class means "note is off but sustain is held".
            if (voice.exclusiveClass >= 0)
                voice.exclusiveClass = -voice.exclusiveClass - 1;
        } else {
            beginRelease(voice, parameters, sampleRate);
        }
    }
}

void noteOn(EngineState& state, const Parameters& parameters, const SoundFont* font,
            const SampleSource* source, const SliceSet* slices, int channel, int note,
            int velocity, double sampleRate, std::uint64_t age)
{
    if (velocity <= 0) {
        noteOff(state, parameters, channel, note, sampleRate);
        return;
    }
    const int effectiveNote = note + static_cast<int>(std::lround(parameters.transpose));
    VoiceSpec spec;
    bool ok = false;
    if (static_cast<int>(std::lround(parameters.sourceMode)) == 1) {
        if (slices != nullptr && source != nullptr && !slices->empty() && !source->empty()) {
            const int index = sliceIndexFor(*slices, effectiveNote,
                                            static_cast<int>(std::lround(parameters.sliceBaseNote)));
            ok = buildSliceSpec(*slices, *source, index, parameters, spec);
        } else if (source != nullptr) {
            ok = buildSampleSpec(*source, parameters, spec);
        }
    }
    if (!ok && font != nullptr && !font->empty()) {
        const ResolvedVoice resolved = resolveNote(*font,
            static_cast<int>(std::lround(parameters.bank)),
            static_cast<int>(std::lround(parameters.program)), effectiveNote, velocity);
        ok = buildFontSpec(*font, resolved, spec);
    }
    if (!ok && source != nullptr)
        ok = buildSampleSpec(*source, parameters, spec);
    if (ok)
        launchVoice(state, parameters, spec, note, channel, velocity, false, sampleRate, age);
}

void triggerPreview(EngineState& state, const Parameters& parameters, const SoundFont* font,
                    const SampleSource* source, const SliceSet* slices, double sampleRate,
                    std::uint64_t age)
{
    VoiceSpec spec;
    bool ok = false;
    if (slices != nullptr && source != nullptr && !slices->empty())
        ok = buildSliceSpec(*slices, *source, 0, parameters, spec);
    else if (source != nullptr)
        ok = buildSampleSpec(*source, parameters, spec);
    else if (font != nullptr && !font->empty()) {
        const ResolvedVoice resolved = resolveNote(*font,
            static_cast<int>(std::lround(parameters.bank)),
            static_cast<int>(std::lround(parameters.program)), 60, 100);
        ok = buildFontSpec(*font, resolved, spec);
    }
    if (ok)
        launchVoice(state, parameters, spec,
                    static_cast<int>(std::lround(parameters.sliceBaseNote)), 1, 100,
                    true, sampleRate, age);
}

void handleControlChange(EngineState& state, const Parameters& parameters, int number,
                         int value, double sampleRate) noexcept
{
    if (number < 0 || number > 127)
        return;
    const bool sustainWasHeld = state.cc[64] >= 64;
    state.cc[static_cast<std::size_t>(number)] = value;
    if (number == 64 && sustainWasHeld && value < 64) {
        for (Voice& voice : state.voices) {
            if (voice.active && voice.exclusiveClass < 0) {
                voice.exclusiveClass = -voice.exclusiveClass - 1;
                beginRelease(voice, parameters, sampleRate);
            }
        }
    }
}

void handleMidi(EngineState& state, const Parameters& parameters, const SoundFont* font,
                const SampleSource* source, const SliceSet* slices, const MidiInputEvent& event,
                double sampleRate, std::uint64_t age, float& lastNote)
{
    if (event.size < 2)
        return;
    const int status = event.data[0] & 0xf0u;
    const int channel = (event.data[0] & 0x0fu) + 1;
    const int globalChannel = static_cast<int>(std::lround(parameters.midiChannel));
    if (globalChannel > 0 && channel != globalChannel)
        return;
    const int data1 = event.data[1] & 0x7fu;
    const int data2 = event.size >= 3 ? event.data[2] & 0x7fu : 0;
    switch (status) {
    case 0x90:
        if (data2 > 0) {
            lastNote = static_cast<float>(data1);
            noteOn(state, parameters, font, source, slices, channel, data1, data2, sampleRate, age);
        } else {
            noteOff(state, parameters, channel, data1, sampleRate);
        }
        break;
    case 0x80:
        noteOff(state, parameters, channel, data1, sampleRate);
        break;
    case 0xb0:
        handleControlChange(state, parameters, data1, data2, sampleRate);
        break;
    case 0xe0: {
        const int value14 = data1 | (data2 << 7);
        state.channelBend[static_cast<std::size_t>(channel - 1)] = std::clamp(
            (static_cast<float>(value14) - 8192.0f) / 8192.0f, -1.0f, 1.0f);
        break;
    }
    default:
        break;
    }
}

void renderVoice(EngineState& state, const Parameters& parameters, const SoundFont* font,
                 const SampleSource* source, FilterCoeff& filter, Voice& voice, int note,
                 double sampleRate, float bendCents, std::uint32_t frame,
                 AudioBlock audio, bool stereo) noexcept
{
    std::array<float, kGeneratorCount> modulation {};
    evaluateModulators(state, parameters, voice, modulation);
    const auto gen = [&voice, &modulation](Gen generator, float fallback) noexcept {
        return effectiveGen(voice, generator, fallback, modulation);
    };

    const float sustain = centibelsToGain(static_cast<int>(std::lround(gen(Gen::SustainVolEnv, 0.0f))));
    const float modSustain = centibelsToGain(static_cast<int>(std::lround(gen(Gen::SustainModEnv, 0.0f))));
    const int currentNote = note + static_cast<int>(std::lround(parameters.transpose));

    if (!voice.release) {
        voice.releaseStart += 1.0f;
        voice.volume = volumeEnvelope(voice, modulation, currentNote, sampleRate, sustain);
        voice.modEnvelope = modEnvelope(voice, modulation, currentNote, sampleRate, modSustain);
        const float fadeFrames = static_cast<float>(parameters.attackMs * 0.001 * sampleRate);
        if (fadeFrames > 0.0f && voice.releaseStart < fadeFrames)
            voice.volume *= voice.releaseStart / fadeFrames;
    } else {
        voice.volume -= voice.releaseRate;
        voice.modEnvelope = std::max(0.0f, voice.modEnvelope - voice.modStep);
        if (voice.volume <= 0.0f) {
            voice.active = false;
            return;
        }
    }

    const double elapsed = voice.release ? 0.0 : static_cast<double>(voice.releaseStart);
    const double modDelay = framesForTimecents(
        static_cast<int>(std::lround(gen(Gen::DelayModLfo, 0.0f))), sampleRate);
    const double vibDelay = framesForTimecents(
        static_cast<int>(std::lround(gen(Gen::DelayVibLfo, 0.0f))), sampleRate);
    const double modFreq = centsToHz(gen(Gen::FreqModLfo, 0.0f));
    const double vibFreq = centsToHz(gen(Gen::FreqVibLfo, 0.0f));
    voice.lfoPhase = static_cast<float>(std::fmod(
        static_cast<double>(voice.lfoPhase) + 2.0 * kPi * modFreq / sampleRate, 2.0 * kPi));
    voice.vibPhase = static_cast<float>(std::fmod(
        static_cast<double>(voice.vibPhase) + 2.0 * kPi * vibFreq / sampleRate, 2.0 * kPi));
    const float modLfo = elapsed >= modDelay ? std::sin(voice.lfoPhase) : 0.0f;
    const float vibLfo = elapsed >= vibDelay ? std::sin(voice.vibPhase) : 0.0f;

    double sourceRate = 44100.0;
    int pitchCorrection = 0;
    const float* data = nullptr;
    std::size_t frames = 0;
    int channels = 1;
    if (voice.sourceIndex >= 0 && font != nullptr &&
        voice.sourceIndex < static_cast<int>(font->samples.size())) {
        const Sample& sample = font->samples[static_cast<std::size_t>(voice.sourceIndex)];
        data = sample.data.data();
        frames = sample.data.size();
        channels = 1;
        sourceRate = sample.sampleRate != 0 ? sample.sampleRate : 44100.0;
        pitchCorrection = sample.pitchCorrection;
    } else if (source != nullptr) {
        data = source->data.data();
        frames = source->frameCount();
        channels = source->channels;
        sourceRate = source->sampleRate != 0 ? source->sampleRate : 44100.0;
    }
    if (data == nullptr || frames == 0) {
        voice.active = false;
        return;
    }

    float tuneCents = gen(Gen::CoarseTune, 0.0f) * 100.0f + gen(Gen::FineTune, 0.0f) +
        static_cast<float>(currentNote - voice.rootNote) * gen(Gen::ScaleTuning, 100.0f) +
        static_cast<float>(pitchCorrection) + parameters.transpose * 100.0f + bendCents +
        gen(Gen::ModEnvToPitch, 0.0f) * voice.modEnvelope +
        gen(Gen::ModLfoToPitch, 0.0f) * modLfo + gen(Gen::VibLfoToPitch, 0.0f) * vibLfo;
    tuneCents = std::clamp(tuneCents, -9600.0f, 9600.0f);
    voice.playbackRate = static_cast<float>(std::pow(2.0, static_cast<double>(tuneCents) / 1200.0) *
                                            sourceRate / sampleRate);

    float cutoffCents = gen(Gen::InitialFilterFc, static_cast<float>(kDefaultFilterFc)) +
        gen(Gen::ModEnvToFilterFc, 0.0f) * voice.modEnvelope +
        gen(Gen::ModLfoToFilterFc, 0.0f) * modLfo;
    float cutoff = static_cast<float>(centsToHz(cutoffCents));
    const float nyquist = static_cast<float>(sampleRate) * 0.45f;
    cutoff = std::clamp(cutoff, kMinFilterHz, std::max(kMinFilterHz, std::min(20000.0f, nyquist)));
    const float q = std::clamp(0.707f * std::pow(10.0f,
        std::max(0.0f, gen(Gen::InitialFilterQ, 0.0f)) / 2000.0f), 0.5f, 20.0f);
    voice.filterCutoff = cutoff;
    voice.filterQ = q;
    updateFilterCoefficients(filter, cutoff, q, sampleRate);

    const float left = readInterpolated(data, frames, channels, voice.position, 0);
    const float right = channels > 1 ? readInterpolated(data, frames, channels, voice.position, 1)
                                     : left;
    const float filtered = runFilter(filter, voice, 0.5f * (left + right));

    const int attenuationCents = static_cast<int>(std::lround(
        gen(Gen::InitialAttenuation, 0.0f) + gen(Gen::ModLfoToVolume, 0.0f) * modLfo));
    const float controllerGain = state.cc[7] > 0 ? state.cc[7] / 127.0f : 100.0f / 127.0f;
    const float expression = state.cc[11] > 0 ? state.cc[11] / 127.0f : 1.0f;
    const float gain = voice.velocity / 127.0f * voice.volume * centibelsToGain(attenuationCents) *
                       parameters.masterVolume * controllerGain * expression;
    const float angle = (std::clamp(voice.pan, -1.0f, 1.0f) + 1.0f) *
                        static_cast<float>(kPi) * 0.25f;
    const float panL = std::cos(angle);
    const float panR = std::sin(angle);
    if (audio.outputs[0] != nullptr)
        audio.outputs[0][frame] += filtered * gain * panL;
    if (stereo && audio.outputs[1] != nullptr)
        audio.outputs[1][frame] += filtered * gain * panR;

    voice.position += voice.playbackRate;
    if (voice.loop && voice.loopEnd > voice.loopStart && voice.position >= voice.loopEnd) {
        const double length = voice.loopEnd - voice.loopStart;
        voice.position = voice.loopStart + std::fmod(voice.position - voice.loopStart, length);
    }
    if (!voice.release && voice.position >= voice.endPosition)
        beginRelease(voice, sampleRate, 0.005);
}

}  // namespace

Parameters clampParameters(const Parameters& parameters) noexcept
{
    Parameters out = parameters;
    out.masterVolume     = std::clamp(out.masterVolume, 0.0f, 1.0f);
    out.midiChannel      = std::clamp(out.midiChannel, 0.0f, 16.0f);
    out.bank             = std::clamp(out.bank, 0.0f, 127.0f);
    out.program          = std::clamp(out.program, 0.0f, 127.0f);
    out.transpose        = std::clamp(out.transpose, -24.0f, 24.0f);
    out.pitchBendRange   = std::clamp(out.pitchBendRange, 0.0f, 24.0f);
    out.attackMs         = std::clamp(out.attackMs, 0.0f, 500.0f);
    out.releaseMs        = std::clamp(out.releaseMs, 0.0f, 2000.0f);
    out.sourceMode       = std::clamp(out.sourceMode, 0.0f, 1.0f);
    out.sliceBaseNote    = std::clamp(out.sliceBaseNote, 0.0f, 96.0f);
    out.sliceLoop        = std::clamp(out.sliceLoop, 0.0f, 1.0f);
    out.filterEnabled    = std::clamp(out.filterEnabled, 0.0f, 1.0f);
    out.filterCutoff     = std::clamp(out.filterCutoff, 100.0f, 20000.0f);
    return out;
}

void activate(EngineState& state, double sampleRate)
{
    (void)sampleRate;
    resetVoices(state);
    state.channelBend.fill(0.0f);
    state.cc.fill(0);
    state.cc[7] = 100;
    state.cc[11] = 127;
    state.seenPreviewSerial = 0;
    state.frameCounter = 0;
}

void resetVoices(EngineState& state) noexcept
{
    for (Voice& voice : state.voices)
        voice = {};
}

double timecentsToSeconds(int timecents) noexcept
{
    if (timecents <= -12000)
        return 0.0;
    return std::min(60.0, std::pow(2.0, static_cast<double>(timecents) / 1200.0));
}

float centibelsToGain(int centibels) noexcept
{
    if (centibels <= 0)
        return 1.0f;
    return static_cast<float>(std::pow(10.0, -static_cast<double>(centibels) / 2000.0));
}

double centsToHz(double cents) noexcept
{
    return 8.1757989156 * std::pow(2.0, cents / 1200.0);
}

ResolvedVoice resolveNote(const SoundFont& font, int bank, int program, int note, int velocity)
{
    ResolvedVoice result;
    const Preset* preset = font.findPreset(bank, program);
    if (preset == nullptr)
        return result;

    const Zone* presetGlobal = nullptr;
    const Zone* presetZone = nullptr;
    for (const Zone& zone : preset->zones) {
        if (isGlobalZone(zone)) {
            if (presetGlobal == nullptr)
                presetGlobal = &zone;
            continue;
        }
        if (zoneInstrument(zone) >= 0 && zoneMatches(zone, note, velocity)) {
            presetZone = &zone;
            break;
        }
    }
    if (presetZone == nullptr)
        return result;
    const int instrumentIndex = zoneInstrument(*presetZone);
    if (instrumentIndex < 0 || instrumentIndex >= static_cast<int>(font.instruments.size()))
        return result;
    const Instrument& instrument = font.instruments[static_cast<std::size_t>(instrumentIndex)];

    const Zone* instrumentGlobal = nullptr;
    const Zone* instrumentZone = nullptr;
    for (const Zone& zone : instrument.zones) {
        if (isGlobalZone(zone)) {
            if (instrumentGlobal == nullptr)
                instrumentGlobal = &zone;
            continue;
        }
        if (zoneMatches(zone, note, velocity)) {
            instrumentZone = &zone;
            break;
        }
    }

    if (instrumentZone == nullptr)
        return result;  // no matching key/velocity zone in this instrument

    GenSet generators = defaultGenerators();
    if (presetGlobal != nullptr)
        generators.overlay(presetGlobal->generators);
    generators.overlay(presetZone->generators);
    if (instrumentGlobal != nullptr)
        generators.overlay(instrumentGlobal->generators);
    if (instrumentZone != nullptr && instrumentZone != instrumentGlobal)
        generators.overlay(instrumentZone->generators);

    int sampleIndex = -1;
    if (instrumentZone != nullptr) {
        if (instrumentZone->sample >= 0)
            sampleIndex = instrumentZone->sample;
        else if (instrumentZone->generators.has(Gen::SampleId))
            sampleIndex = instrumentZone->generators.get(Gen::SampleId);
    }
    if (sampleIndex < 0 && generators.has(Gen::SampleId))
        sampleIndex = generators.get(Gen::SampleId);
    if (sampleIndex < 0 && presetZone->sample >= 0)
        sampleIndex = presetZone->sample;
    if (sampleIndex >= 0 && sampleIndex >= static_cast<int>(font.samples.size()))
        sampleIndex = -1;

    if (instrumentZone != nullptr && !instrumentZone->modulators.empty())
        result.modulators = instrumentZone->modulators;
    else if (instrumentGlobal != nullptr && !instrumentGlobal->modulators.empty())
        result.modulators = instrumentGlobal->modulators;
    else if (!presetZone->modulators.empty())
        result.modulators = presetZone->modulators;
    else if (presetGlobal != nullptr && !presetGlobal->modulators.empty())
        result.modulators = presetGlobal->modulators;
    else
        result.modulators = defaultModulators();

    result.generators = generators;
    result.sampleIndex = sampleIndex;
    result.found = true;
    return result;
}

void processBlock(EngineState& state,
                  Parameters& parameters,
                  const SoundFont* font,
                  const SampleSource* sample,
                  const SliceSet* slices,
                  const std::uint32_t previewSerial,
                  const MidiInputEvent* midiEvents,
                  std::uint32_t midiCount,
                  std::uint32_t frames,
                  double hostSampleRate,
                  AudioBlock audio)
{
    // Status fields are the only values written back to the caller; all
    // audible settings are read from the local clamped copy.
    Parameters& output = parameters;
    const Parameters p = clampParameters(parameters);
    float lastNote = parameters.statusLastNote;

    if (audio.outputs[0] != nullptr && frames > 0)
        std::fill_n(audio.outputs[0], frames, 0.0f);
    if (audio.outputs[1] != nullptr && frames > 0)
        std::fill_n(audio.outputs[1], frames, 0.0f);

    const auto publishStatus = [&]() noexcept {
        int active = 0;
        for (const Voice& voice : state.voices)
            if (voice.active)
                ++active;
        output.statusBank = p.bank;
        output.statusProgram = p.program;
        output.statusVoices = static_cast<float>(active);
        output.statusLastNote = lastNote;
    };

    if (frames == 0 || audio.channelCount < 1) {
        publishStatus();
        return;
    }
    const double sampleRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    const bool stereo = audio.channelCount >= 2 && audio.outputs[1] != nullptr;
    if (midiEvents == nullptr)
        midiCount = 0;

    if (previewSerial != state.seenPreviewSerial) {
        state.seenPreviewSerial = previewSerial;
        triggerPreview(state, p, font, sample, slices, sampleRate, state.frameCounter);
    }

    std::array<FilterCoeff, kMaxVoices> filters {};
    std::uint32_t midiIndex = 0;
    for (std::uint32_t frame = 0; frame < frames; ++frame) {
        while (midiIndex < midiCount && midiEvents[midiIndex].frame <= frame) {
            handleMidi(state, p, font, sample, slices, midiEvents[midiIndex],
                       sampleRate, state.frameCounter + frame, lastNote);
            ++midiIndex;
        }

        for (int i = 0; i < kMaxVoices; ++i) {
            Voice& voice = state.voices[static_cast<std::size_t>(i)];
            if (!voice.active)
                continue;
            const int channel = std::clamp(voice.channel - 1, 0, 15);
            const float bendCents = state.channelBend[static_cast<std::size_t>(channel)] *
                                    p.pitchBendRange * 100.0f;
            renderVoice(state, p, font, sample, filters[static_cast<std::size_t>(i)], voice,
                        voice.note, sampleRate, bendCents, frame, audio, stereo);
        }

        // Soft saturation keeps summed voices inside [-1, 1] without a hard clip.
        if (audio.outputs[0] != nullptr)
            audio.outputs[0][frame] = std::tanh(audio.outputs[0][frame]);
        if (stereo)
            audio.outputs[1][frame] = std::tanh(audio.outputs[1][frame]);
    }

    state.frameCounter += frames;
    publishStatus();
}

}  // namespace downspout::fontana
