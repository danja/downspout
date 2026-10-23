#pragma once

#include "quartet_core.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

namespace downspout::quartet {

// Parameter indices are stable: never reorder, only append.
// Four blocks of ten per operator (waveform, fixed mode, ratio, fine, fixed Hz, level, attack, decay, sustain, release),
// then the global controls.
enum OperatorParameter : std::uint32_t { kWave = 0, kFixedMode, kRatio, kFine, kFixedHz, kLevel, kAttack, kDecay, kSustain, kRelease, kPerOperator };

inline constexpr std::uint32_t kGlobalBase = kOperators * kPerOperator;
enum GlobalParameter : std::uint32_t {
    kAlgorithm = kGlobalBase, kFeedback, kDepth,
    kPitchEnvOn, kPitchEnvSemitones, kPitchAttack, kPitchDecay, kPitchSustain, kPitchRelease,
    kFilterType, kCutoff, kResonance, kKeyTrack,
    kMaster, kVelocity, kBendRange, kQuality, kPlayMode, kGlide,
    kParameterCount
};

[[nodiscard]] constexpr std::uint32_t operatorParameter(const int op, const OperatorParameter p) noexcept
{
    return static_cast<std::uint32_t>(op) * kPerOperator + p;
}

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
};

// Built once: names and symbols need storage that outlives the host's calls.
class Specs {
public:
    Specs()
    {
        const Parameters d;
        static constexpr const char* kOpNames[kOperators] = {"A", "B", "C", "D"};
        for (int o = 0; o < kOperators; ++o) {
            const OperatorParams& x = d.op[static_cast<size_t>(o)];
            op(o, kWave, "wave", "Waveform", "", 0.0f, 2.0f, x.wave, false, true, kOpNames[o]);
            op(o, kFixedMode, "fixed", "Fixed Frequency", "", 0.0f, 1.0f, x.fixedMode, false, true, kOpNames[o]);
            op(o, kRatio, "ratio", "Ratio", "x", 0.25f, 32.0f, x.ratio, true, false, kOpNames[o]);
            op(o, kFine, "fine", "Fine", "ct", -50.0f, 50.0f, x.fineCents, false, false, kOpNames[o]);
            op(o, kFixedHz, "fixed_hz", "Fixed Hz", "Hz", 10.0f, 20000.0f, x.fixedHz, true, false, kOpNames[o]);
            op(o, kLevel, "level", "Level", "", 0.0f, 1.0f, x.level, false, false, kOpNames[o]);
            op(o, kAttack, "attack", "Attack", "ms", 0.5f, 5000.0f, x.attackMs, true, false, kOpNames[o]);
            op(o, kDecay, "decay", "Decay", "ms", 1.0f, 8000.0f, x.decayMs, true, false, kOpNames[o]);
            op(o, kSustain, "sustain", "Sustain", "", 0.0f, 1.0f, x.sustain, false, false, kOpNames[o]);
            op(o, kRelease, "release", "Release", "ms", 1.0f, 8000.0f, x.releaseMs, true, false, kOpNames[o]);
        }
        global(kAlgorithm, "algorithm", "Algorithm", "", 0.0f, 10.0f, d.algorithm, false, true);
        global(kFeedback, "feedback", "Feedback", "", 0.0f, 1.0f, d.feedback, false, false);
        global(kDepth, "depth", "FM Depth", "cyc", 0.0f, 4.0f, d.depth, false, false);
        global(kPitchEnvOn, "pitch_env", "Pitch Envelope", "", 0.0f, 1.0f, d.pitchEnvOn, false, true);
        global(kPitchEnvSemitones, "pitch_env_amount", "Pitch Envelope Amount", "st", -48.0f, 48.0f, d.pitchEnvSemitones, false, false);
        global(kPitchAttack, "pitch_attack", "Pitch Attack", "ms", 0.5f, 5000.0f, d.pitchAttackMs, true, false);
        global(kPitchDecay, "pitch_decay", "Pitch Decay", "ms", 1.0f, 8000.0f, d.pitchDecayMs, true, false);
        global(kPitchSustain, "pitch_sustain", "Pitch Sustain", "", 0.0f, 1.0f, d.pitchSustain, false, false);
        global(kPitchRelease, "pitch_release", "Pitch Release", "ms", 1.0f, 8000.0f, d.pitchReleaseMs, true, false);
        global(kFilterType, "filter_type", "Filter Type", "", 0.0f, 4.0f, d.filterType, false, true);
        global(kCutoff, "cutoff", "Cutoff", "Hz", 20.0f, 20000.0f, d.cutoffHz, true, false);
        global(kResonance, "resonance", "Resonance", "", 0.0f, 1.0f, d.resonance, false, false);
        global(kKeyTrack, "key_track", "Key Track", "", 0.0f, 1.0f, d.keyTrack, false, false);
        global(kMaster, "master", "Master", "", 0.0f, 1.0f, d.master, false, false);
        global(kVelocity, "velocity", "Velocity", "", 0.0f, 1.0f, d.velocity, false, false);
        global(kBendRange, "bend_range", "Bend Range", "st", 0.0f, 24.0f, d.bendRange, false, false);
        global(kQuality, "quality", "Quality", "", 0.0f, 1.0f, d.quality, false, true);
        global(kPlayMode, "play_mode", "Play Mode", "", 0.0f, 2.0f, d.playMode, false, true);
        global(kGlide, "glide", "Glide", "ms", 0.0f, 2000.0f, d.glideMs, false, false);
    }
    [[nodiscard]] const ParameterSpec& operator[](const std::uint32_t i) const { return specs_[i]; }

private:
    void put(const std::uint32_t i, const char* unit, const float lo, const float hi, const float def, const bool log, const bool integer)
    {
        specs_[i] = {symbols_[i], names_[i], unit, lo, hi, def, log, integer};
    }
    void global(const std::uint32_t i, const char* sym, const char* name, const char* unit, const float lo, const float hi,
                const float def, const bool log, const bool integer)
    {
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "%s", sym);
        std::snprintf(names_[i], sizeof(names_[i]), "%s", name);
        put(i, unit, lo, hi, def, log, integer);
    }
    void op(const int o, const OperatorParameter p, const char* sym, const char* name, const char* unit, const float lo, const float hi,
            const float def, const bool log, const bool integer, const char* opName)
    {
        const std::uint32_t i = operatorParameter(o, p);
        std::snprintf(symbols_[i], sizeof(symbols_[i]), "op%c_%s", opName[0] + 32, sym);
        std::snprintf(names_[i], sizeof(names_[i]), "Op %s %s", opName, name);
        put(i, unit, lo, hi, def, log, integer);
    }

    std::array<ParameterSpec, kParameterCount> specs_ {};
    char symbols_[kParameterCount][28] {};
    char names_[kParameterCount][32] {};
};

[[nodiscard]] inline const Specs& specs()
{
    static const Specs s;
    return s;
}

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    if (index < kGlobalBase) {
        OperatorParams& o = p.op[index / kPerOperator];
        switch (index % kPerOperator) {
        case kWave:      return o.wave;
        case kFixedMode: return o.fixedMode;
        case kRatio:     return o.ratio;
        case kFine:      return o.fineCents;
        case kFixedHz:   return o.fixedHz;
        case kLevel:     return o.level;
        case kAttack:    return o.attackMs;
        case kDecay:     return o.decayMs;
        case kSustain:   return o.sustain;
        default:         return o.releaseMs;
        }
    }
    switch (index) {
    case kAlgorithm:         return p.algorithm;
    case kFeedback:          return p.feedback;
    case kDepth:             return p.depth;
    case kPitchEnvOn:        return p.pitchEnvOn;
    case kPitchEnvSemitones: return p.pitchEnvSemitones;
    case kPitchAttack:       return p.pitchAttackMs;
    case kPitchDecay:        return p.pitchDecayMs;
    case kPitchSustain:      return p.pitchSustain;
    case kPitchRelease:      return p.pitchReleaseMs;
    case kFilterType:        return p.filterType;
    case kCutoff:            return p.cutoffHz;
    case kResonance:         return p.resonance;
    case kKeyTrack:          return p.keyTrack;
    case kMaster:            return p.master;
    case kVelocity:          return p.velocity;
    case kBendRange:         return p.bendRange;
    case kQuality:           return p.quality;
    case kPlayMode:          return p.playMode;
    case kGlide:             return p.glideMs;
    default:                 return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::quartet
