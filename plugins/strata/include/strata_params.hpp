#pragma once

#include "strata_core.hpp"

#include <array>
#include <cstdint>

namespace downspout::strata {

// Parameter indices are stable: never reorder, only append.
enum ParameterIndex : std::uint32_t {
    kOsc1Table = 0, kOsc1Position, kOsc1Warp, kOsc1WarpAmount, kOsc1Transpose, kOsc1Fine, kOsc1Level,
    kOsc2Table, kOsc2Position, kOsc2Warp, kOsc2WarpAmount, kOsc2Transpose, kOsc2Fine, kOsc2Level,
    kSubLevel, kSubOctave,
    kUnisonMode, kUnisonVoices, kUnisonAmount,
    kFilterType, kCutoff, kResonance, kKeyTrack, kEnvAmount, kEnvPosition,
    kAmpAttack, kAmpDecay, kAmpSustain, kAmpRelease,
    kFltAttack, kFltDecay, kFltSustain, kFltRelease,
    kLfoRate, kLfoShape, kLfoPosition, kLfoPitch, kLfoCutoff,
    kMaster, kVelocity, kBendRange, kPlayMode, kGlide,
    kParameterCount
};

struct ParameterSpec {
    const char* symbol;
    const char* name;
    const char* unit;
    float minimum, maximum, defaultValue;
    bool logarithmic;
    bool integer;
};

inline constexpr std::array<ParameterSpec, kParameterCount> kParameterSpecs = {{
    {"osc1_table",    "Osc 1 Table",      "",   0.0f,    4.0f,    0.0f, false, true },
    {"osc1_position", "Osc 1 Position",   "",   0.0f,    1.0f,    0.2f, false, false},
    {"osc1_warp",     "Osc 1 Warp",       "",   0.0f,    3.0f,    0.0f, false, true },
    {"osc1_warp_amt", "Osc 1 Warp Amount","",   0.0f,    1.0f,    0.0f, false, false},
    {"osc1_transpose","Osc 1 Transpose",  "st", -24.0f,  24.0f,   0.0f, false, true },
    {"osc1_fine",     "Osc 1 Fine",       "ct", -50.0f,  50.0f,   0.0f, false, false},
    {"osc1_level",    "Osc 1 Level",      "",   0.0f,    1.0f,    0.8f, false, false},
    {"osc2_table",    "Osc 2 Table",      "",   0.0f,    4.0f,    0.0f, false, true },
    {"osc2_position", "Osc 2 Position",   "",   0.0f,    1.0f,    0.2f, false, false},
    {"osc2_warp",     "Osc 2 Warp",       "",   0.0f,    3.0f,    0.0f, false, true },
    {"osc2_warp_amt", "Osc 2 Warp Amount","",   0.0f,    1.0f,    0.0f, false, false},
    {"osc2_transpose","Osc 2 Transpose",  "st", -24.0f,  24.0f,   0.0f, false, true },
    {"osc2_fine",     "Osc 2 Fine",       "ct", -50.0f,  50.0f,   0.0f, false, false},
    {"osc2_level",    "Osc 2 Level",      "",   0.0f,    1.0f,    0.8f, false, false},
    {"sub_level",     "Sub Level",        "",   0.0f,    1.0f,    0.3f, false, false},
    {"sub_octave",    "Sub Octave",       "",   0.0f,    1.0f,    0.0f, false, true },
    {"unison_mode",   "Unison Mode",      "",   0.0f,    2.0f,    0.0f, false, true },
    {"unison_voices", "Unison Voices",    "",   1.0f,    8.0f,    2.0f, false, true },
    {"unison_amount", "Unison Amount",    "",   0.0f,    1.0f,    0.15f,false, false},
    {"filter_type",   "Filter Type",      "",   0.0f,    4.0f,    0.0f, false, true },
    {"cutoff",        "Cutoff",           "Hz", 20.0f,   20000.0f,4000.0f, true, false},
    {"resonance",     "Resonance",        "",   0.0f,    1.0f,    0.2f, false, false},
    {"key_track",     "Key Track",        "",   0.0f,    1.0f,    0.0f, false, false},
    {"env_amount",    "Filter Env Amount","",  -1.0f,    1.0f,    0.0f, false, false},
    {"env_position",  "Filter Env Position","",-1.0f,    1.0f,    0.0f, false, false},
    {"amp_attack",    "Amp Attack",       "ms", 0.5f,    5000.0f, 5.0f, true,  false},
    {"amp_decay",     "Amp Decay",        "ms", 1.0f,    8000.0f, 300.0f, true, false},
    {"amp_sustain",   "Amp Sustain",      "",   0.0f,    1.0f,    0.7f, false, false},
    {"amp_release",   "Amp Release",      "ms", 1.0f,    8000.0f, 250.0f, true, false},
    {"flt_attack",    "Filter Attack",    "ms", 0.5f,    5000.0f, 10.0f, true,  false},
    {"flt_decay",     "Filter Decay",     "ms", 1.0f,    8000.0f, 400.0f, true, false},
    {"flt_sustain",   "Filter Sustain",   "",   0.0f,    1.0f,    0.3f, false, false},
    {"flt_release",   "Filter Release",   "ms", 1.0f,    8000.0f, 300.0f, true, false},
    {"lfo_rate",      "LFO Rate",         "Hz", 0.05f,   20.0f,   2.0f, true,  false},
    {"lfo_shape",     "LFO Shape",        "",   0.0f,    4.0f,    0.0f, false, true },
    {"lfo_position",  "LFO to Position",  "",   0.0f,    1.0f,    0.0f, false, false},
    {"lfo_pitch",     "LFO to Pitch",     "",   0.0f,    1.0f,    0.0f, false, false},
    {"lfo_cutoff",    "LFO to Cutoff",    "",   0.0f,    1.0f,    0.0f, false, false},
    {"master",        "Master",           "",   0.0f,    1.0f,    0.6f, false, false},
    {"velocity",      "Velocity",         "",   0.0f,    1.0f,    0.7f, false, false},
    {"bend_range",    "Bend Range",       "st", 0.0f,    24.0f,   2.0f, false, false},
    {"play_mode",     "Play Mode",        "",   0.0f,    2.0f,    0.0f, false, true },
    {"glide",         "Glide",            "ms", 0.0f,    2000.0f, 0.0f, false, false},
}};

inline float& field(Parameters& p, const std::uint32_t index) noexcept
{
    static float unused = 0.0f;
    if (index <= kOsc2Level) {
        OscParams& o = p.osc[index >= kOsc2Table ? 1 : 0];
        switch ((index >= kOsc2Table ? index - kOsc2Table : index)) {
        case 0: return o.table;
        case 1: return o.position;
        case 2: return o.warpMode;
        case 3: return o.warpAmount;
        case 4: return o.transpose;
        case 5: return o.fineCents;
        default: return o.level;
        }
    }
    switch (index) {
    case kSubLevel:      return p.subLevel;
    case kSubOctave:     return p.subOctave;
    case kUnisonMode:    return p.unisonMode;
    case kUnisonVoices:  return p.unisonVoices;
    case kUnisonAmount:  return p.unisonAmount;
    case kFilterType:    return p.filterType;
    case kCutoff:        return p.cutoffHz;
    case kResonance:     return p.resonance;
    case kKeyTrack:      return p.keyTrack;
    case kEnvAmount:     return p.envAmount;
    case kEnvPosition:   return p.envPosition;
    case kAmpAttack:     return p.ampAttackMs;
    case kAmpDecay:      return p.ampDecayMs;
    case kAmpSustain:    return p.ampSustain;
    case kAmpRelease:    return p.ampReleaseMs;
    case kFltAttack:     return p.fltAttackMs;
    case kFltDecay:      return p.fltDecayMs;
    case kFltSustain:    return p.fltSustain;
    case kFltRelease:    return p.fltReleaseMs;
    case kLfoRate:       return p.lfoRateHz;
    case kLfoShape:      return p.lfoShape;
    case kLfoPosition:   return p.lfoPosition;
    case kLfoPitch:      return p.lfoPitch;
    case kLfoCutoff:     return p.lfoCutoff;
    case kMaster:        return p.master;
    case kVelocity:      return p.velocity;
    case kBendRange:     return p.bendRange;
    case kPlayMode:      return p.playMode;
    case kGlide:         return p.glideMs;
    default:             return unused;
    }
}

inline float field(const Parameters& p, const std::uint32_t index) noexcept
{
    return field(const_cast<Parameters&>(p), index);
}

}  // namespace downspout::strata
