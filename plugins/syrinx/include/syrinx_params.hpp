#pragma once

#include <array>
#include <cstdint>
#include <cmath>

namespace downspout::syrinx {

// Presets 0-9 are the original contiguous blocks, followed by the masters and status outputs. Later presets (Crow) are
// appended as extra blocks after everything else, so no saved project's parameter index ever moves.
static constexpr std::uint32_t kLegacyPresetCount = 10;
static constexpr std::uint32_t kPresetCount = 11;
static constexpr std::uint32_t kParamsPerPreset = 21;

// Within each preset block (offset from preset_index * kParamsPerPreset):
static constexpr std::uint32_t kPresetParamLevel       = 0;
static constexpr std::uint32_t kPresetParamNoise       = 1;
static constexpr std::uint32_t kPresetParamRoughness   = 2;
static constexpr std::uint32_t kPresetParamTimbre      = 3;
static constexpr std::uint32_t kPresetParamVibRate     = 4;  // normalized 0-1 → 0-20 Hz
static constexpr std::uint32_t kPresetParamVibDepth    = 5;  // normalized 0-1 → 0-300 cents
static constexpr std::uint32_t kPresetParamBend        = 6;  // -1 to +1
static constexpr std::uint32_t kPresetParamHarmonic    = 7;
static constexpr std::uint32_t kPresetParamAMRate      = 8;  // normalized 0-1 → 0-30 Hz
static constexpr std::uint32_t kPresetParamMute        = 9;
static constexpr std::uint32_t kPresetParamPitch       = 10; // -1 to +1 → ±12 semitones
static constexpr std::uint32_t kPresetParamDuration    = 11; // 0-1 → 0.05-2.0 s syllable length
static constexpr std::uint32_t kPresetParamRespiration = 12; // 0-1 breathing depth
static constexpr std::uint32_t kPresetParamAMDepth     = 13; // 0-1 AM modulation depth
static constexpr std::uint32_t kPresetParamFormant1    = 14; // 200-8000 Hz fixed resonance 1
static constexpr std::uint32_t kPresetParamFormant2    = 15; // 200-8000 Hz fixed resonance 2
static constexpr std::uint32_t kPresetParamFormantQ    = 16; // 0.7-20 shared formant Q
static constexpr std::uint32_t kPresetParamCoupling    = 17; // 0-1 two-oscillator coupling
static constexpr std::uint32_t kPresetParamVoiceOffset = 18; // 0-1 secondary voice freq offset (0=unison, 1=+octave)
static constexpr std::uint32_t kPresetParamRegime      = 19; // 0-1 → gammaScale 1-4 (ODE regime shift)
static constexpr std::uint32_t kPresetParamTracheaCm   = 20; // 0-10 cm tracheal tube length (0=simple mix)

// Master parameters (after all preset blocks)
static constexpr std::uint32_t kParamDistance         = kLegacyPresetCount * kParamsPerPreset + 0;
static constexpr std::uint32_t kParamMasterGain       = kLegacyPresetCount * kParamsPerPreset + 1;
static constexpr std::uint32_t kParamSelectedPreset   = kLegacyPresetCount * kParamsPerPreset + 2;

// Global section, appended after the original masters so saved projects keep their indices.
// Phrase: each MIDI note-on plays a run of short syllables instead of one steady tone.
static constexpr std::uint32_t kParamSource           = kLegacyPresetCount * kParamsPerPreset + 3;  // 0 MIDI, 1 Audio, 2 Both
static constexpr std::uint32_t kParamSyllables        = kLegacyPresetCount * kParamsPerPreset + 4;  // 1-16 per note-on
static constexpr std::uint32_t kParamChirp            = kLegacyPresetCount * kParamsPerPreset + 5;  // 0-1 sweep depth and speed
static constexpr std::uint32_t kParamVariation        = kLegacyPresetCount * kParamsPerPreset + 6;  // 0-1 syllable-to-syllable spread
static constexpr std::uint32_t kParamGap              = kLegacyPresetCount * kParamsPerPreset + 7;  // 0-1 silence between syllables
// Audio input analysis
static constexpr std::uint32_t kParamInPitchMult      = kLegacyPresetCount * kParamsPerPreset + 8;  // 1-12 x, tracked pitch multiplier
static constexpr std::uint32_t kParamInRange          = kLegacyPresetCount * kParamsPerPreset + 9;  // 80-800 Hz, centre of the voice
static constexpr std::uint32_t kParamInGate           = kLegacyPresetCount * kParamsPerPreset + 10; // 0-1 noise gate
static constexpr std::uint32_t kParamInResponse       = kLegacyPresetCount * kParamsPerPreset + 11; // 0-1 pitch glide time
static constexpr std::uint32_t kParamInUnvoiced       = kLegacyPresetCount * kParamsPerPreset + 12; // 0-1 sibilants -> turbulence
static constexpr std::uint32_t kParamInMix            = kLegacyPresetCount * kParamsPerPreset + 13; // 0-1 dry(0) / bird(1)
// Output (status) parameters for the UI; written by the engine
static constexpr std::uint32_t kParamOutLevel         = kLegacyPresetCount * kParamsPerPreset + 14; // 0-1 input level after gate
static constexpr std::uint32_t kParamOutInHz          = kLegacyPresetCount * kParamsPerPreset + 15; // tracked input Hz (0 = unvoiced)
static constexpr std::uint32_t kParamOutBirdHz        = kLegacyPresetCount * kParamsPerPreset + 16; // Hz the bird voice is following
static constexpr std::uint32_t kParamOutVoiced        = kLegacyPresetCount * kParamsPerPreset + 17; // 0 silent, 0.5 unvoiced, 1 voiced
static constexpr std::uint32_t kLegacyParameterCount  = kLegacyPresetCount * kParamsPerPreset + 18;
static constexpr std::uint32_t kAppendedPresetBase    = kLegacyParameterCount;   // first block of presets 10 and up
static constexpr std::uint32_t kParameterCount        = kLegacyParameterCount + (kPresetCount - kLegacyPresetCount) * kParamsPerPreset;
static constexpr std::uint32_t kFirstOutputParam      = kParamOutLevel;

inline constexpr std::uint32_t presetParam(std::uint32_t preset, std::uint32_t param)
{
    return preset < kLegacyPresetCount ? preset * kParamsPerPreset + param
                                       : kAppendedPresetBase + (preset - kLegacyPresetCount) * kParamsPerPreset + param;
}

// Which preset a parameter index belongs to, or -1 for a master, phrase, input or status parameter.
inline constexpr int presetOfParam(std::uint32_t index)
{
    if (index < kLegacyPresetCount * kParamsPerPreset) return static_cast<int>(index / kParamsPerPreset);
    if (index >= kAppendedPresetBase && index < kParameterCount)
        return static_cast<int>(kLegacyPresetCount + (index - kAppendedPresetBase) / kParamsPerPreset);
    return -1;
}

struct ParameterSpec {
    const char* name;
    const char* symbol;
    float minimum;
    float maximum;
    float defaultValue;
    bool boolean;
    bool integer;
    bool output = false;
};

static const char* const kPresetNames[kPresetCount] = {
    "Wren", "Thrush", "Warbler", "Finch", "Robin",
    "Nightjar", "Pigeon", "Hummingbird", "Starling", "Custom", "Crow"
};

// Default values per preset [preset][param_within_preset]
//
// Formant Hz values are derived from the avian tracheal quarter-wave tube law:
//   f1 = c / (4 * L_eff),  c ≈ 344 m/s
//   f2 = 3 * f1  (first odd overtone of a stopped tube)
//   Q  = 6.0     (Trachea dampened by soft tissue; from Riede et al. 2006)
//
// Measured references: eastern towhee (45 mm trachea): ~2.0/5.5 kHz (Nelson et al. 2005);
// northern cardinal: ~2/5/8/12 kHz (Riede et al. 2006); white-throated sparrow
// (34–38 mm): ~2.2/6.6 kHz (Riede & Suthers 2009).
//
// Tracheal resonances are FIXED frequencies above the singing range. Prior preset values
// (500–2800 Hz) placed them inside the f0 band, making them redundant with the tracking
// bandpass and preventing cross-pitch spectral identity — the main reason presets did not
// sound species-specific.
//
// Noise floor: real birds have spectral flatness ~0.044 (median over 39 976 syllables,
// Lyrebird harvest). noise=0 gives ~0.0005 — too pure. Small non-zero noise is added to
// presets where it was previously 0.
//
// Regime=0 → gammaScale=1 (tonal). TracheaCm=0 → simple independent-ODE mix.
// TracheaCm=2.0 → physics pi(t) coupling per Laje & Mindlin 2005 Table I default.
// Lyrebird uses 2.0 cm for all species regardless of tracheal acoustic length — the
// bilateral coupling delay is the syringeal chamber path, shorter and more constant.
// Only presets with coupling>0 (Nightjar, Pigeon, Starling) have TracheaCm>0.
static constexpr float kPresetDefaults[kPresetCount][kParamsPerPreset] = {
    // Lv     Noise  Rough  Timb   VibR   VibD   Bend   Harm   AMR    Mute  Pitch  Dur    Resp   AMDep  F1       F2      FQ     Coup   VOffs  Regime TracheaCm
    {  0.80f, 0.04f, 0.00f, 0.15f, 0.60f, 0.20f, 0.30f, 0.70f, 0.00f, 0.00f, 0.00f, 0.10f, 0.00f, 0.00f, 4800.f, 8000.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Wren      L≈1.8cm  no coupling
    {  0.90f, 0.03f, 0.00f, 0.30f, 0.33f, 0.15f, 0.10f, 0.60f, 0.00f, 0.00f, 0.00f, 0.18f, 0.00f, 0.00f, 2500.f, 7500.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Thrush    L≈3.4cm  no coupling
    {  0.85f, 0.03f, 0.00f, 0.20f, 0.30f, 0.25f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.30f, 0.00f, 0.00f, 4300.f, 8000.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Warbler   L≈2.0cm  no coupling
    {  0.80f, 0.05f, 0.00f, 0.10f, 0.67f, 0.10f, 0.40f, 0.30f, 0.00f, 0.00f, 0.08f, 0.08f, 0.00f, 0.00f, 3500.f, 8000.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Finch     L≈2.5cm  no coupling
    {  0.85f, 0.03f, 0.00f, 0.25f, 0.40f, 0.15f, 0.05f, 0.50f, 0.00f, 0.00f, 0.00f, 0.20f, 0.00f, 0.00f, 3900.f, 8000.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Robin     L≈2.2cm  no coupling
    {  0.90f, 0.40f, 0.60f, 0.90f, 0.13f, 0.10f, 0.00f, 0.30f, 0.30f, 0.00f,-0.08f, 0.45f, 0.25f, 0.50f, 2500.f, 7500.f, 6.0f, 0.30f, 0.00f, 0.00f, 2.00f }, // Nightjar  L≈3.4cm  physics coupling (Laje Table I default)
    {  0.70f, 0.08f, 0.10f, 0.30f, 0.20f, 0.05f, 0.00f, 0.40f, 0.00f, 0.00f,-0.17f, 0.55f, 0.40f, 0.00f, 1400.f, 4300.f, 6.0f, 0.20f, 0.00f, 0.00f, 2.00f }, // Pigeon    L≈6.1cm  physics coupling
    {  0.75f, 0.10f, 0.00f, 0.15f, 0.80f, 0.30f, 0.50f, 0.60f, 0.50f, 0.00f, 0.17f, 0.05f, 0.00f, 1.00f, 6000.f, 8000.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Hummingbird L≈1.4cm no coupling
    {  0.85f, 0.15f, 0.20f, 0.70f, 0.47f, 0.20f, 0.15f, 0.80f, 0.00f, 0.00f, 0.00f, 0.18f, 0.10f, 0.30f, 2500.f, 7500.f, 6.0f, 0.10f, 0.00f, 0.00f, 2.00f }, // Starling  L≈3.4cm  physics coupling
    {  0.80f, 0.05f, 0.00f, 0.30f, 0.33f, 0.20f, 0.00f, 0.50f, 0.00f, 0.00f, 0.00f, 0.15f, 0.00f, 0.00f, 3500.f, 8000.f, 6.0f, 0.00f, 0.00f, 0.00f, 0.00f }, // Custom
    // A hoarse, falling "caw": low, nasal formants (~1/2.6 kHz, Q 4), strong breath noise and roughness, a 12 Hz AM rasp,
    // a slightly detuned second voice and the chaotic ODE regime. Syllables 2-3 with a Gap near 0.4 give "caw caw".
    {  0.95f, 0.38f, 0.70f, 0.80f, 0.00f, 0.00f,-0.35f, 0.85f, 0.40f, 0.00f,-0.35f, 0.17f, 0.12f, 0.55f, 1000.f, 2600.f, 4.0f, 0.35f, 0.04f, 0.65f, 2.00f }, // Crow      L≈8cm   chaotic regime, nasal
};

// Build the flat parameter table at compile time via a helper
namespace detail {

constexpr const char* kParamNames[kParameterCount] = {};
constexpr const char* kParamSymbols[kParameterCount] = {};

inline ParameterSpec makePresetSpec(std::uint32_t preset, std::uint32_t p, float defVal)
{
    static const char* names[kParamsPerPreset] = {
        " Level", " Noise", " Roughness", " Timbre",
        " Vibrato Rate", " Vibrato Depth", " Bend", " Harmonic",
        " AM Rate", " Mute",
        " Pitch", " Duration", " Respiration",
        " AM Depth", " Formant 1", " Formant 2", " Formant Q", " Coupling",
        " Voice Offset", " Regime", " Trachea cm"
    };
    static const char* syms[kParamsPerPreset] = {
        "_level", "_noise", "_roughness", "_timbre",
        "_vib_rate", "_vib_depth", "_bend", "_harmonic",
        "_am_rate", "_mute",
        "_pitch", "_duration", "_respiration",
        "_am_depth", "_formant1", "_formant2", "_formant_q", "_coupling",
        "_voice_offset", "_regime", "_trachea_cm"
    };
    static const float mins[kParamsPerPreset]  = { 0,0,0,0, 0,0,-1,0, 0,0, -1,0,0, 0,200,200,0.7f,0, 0, 0,0 };
    static const float maxs[kParamsPerPreset]  = { 1.4f,1,1,1, 1,1,1,1, 1,1, 1,1,1, 1,8000,8000,20,1, 1, 1,10 };
    static const bool  bools[kParamsPerPreset] = { 0,0,0,0, 0,0,0,0, 0,1,  0,0,0,  0,0,0,0,0, 0, 0,0 };
    (void)preset;
    return { names[p], syms[p], mins[p], maxs[p], defVal, bools[p], false };
}

} // namespace detail

// Runtime-initialized flat table (filled in syrinx_engine.cpp / syrinx_params.cpp)
// Access via getParameterSpec(index).
inline ParameterSpec getParameterSpec(std::uint32_t index)
{
    if (const int presetIndex = presetOfParam(index); presetIndex >= 0) {
        const std::uint32_t preset = static_cast<std::uint32_t>(presetIndex);
        const std::uint32_t p     = (index - presetParam(preset, 0));
        const float defVal = kPresetDefaults[preset][p];
        return detail::makePresetSpec(preset, p, defVal);
    }
    switch (index) {
    case kParamDistance:
        return { "Distance",        "distance",         0.0f, 1.0f, 0.25f, false, false };
    case kParamMasterGain:
        return { "Master Gain",     "master_gain",      0.0f, 1.0f, 0.75f, false, false };
    case kParamSelectedPreset:
        return { "Selected Preset", "selected_preset",  0.0f, 10.0f, 0.0f,  false, true  };
    case kParamSource:
        return { "Source",          "source",           0.0f, 2.0f, 0.0f,  false, true  };
    case kParamSyllables:
        return { "Syllables",       "syllables",        1.0f, 16.0f, 3.0f, false, true  };
    case kParamChirp:
        return { "Chirp",           "chirp",            0.0f, 1.0f, 0.6f,  false, false };
    case kParamVariation:
        return { "Variation",       "variation",        0.0f, 1.0f, 0.4f,  false, false };
    case kParamGap:
        return { "Gap",             "gap",              0.0f, 1.0f, 0.35f, false, false };
    case kParamInPitchMult:
        return { "Pitch Multiplier","in_pitch_mult",    1.0f, 12.0f, 6.0f, false, false };
    case kParamInRange:
        return { "Voice Range",     "in_range",         80.0f, 800.0f, 220.0f, false, false };
    case kParamInGate:
        return { "Gate",            "in_gate",          0.0f, 1.0f, 0.15f, false, false };
    case kParamInResponse:
        return { "Pitch Response",  "in_response",      0.0f, 1.0f, 0.3f,  false, false };
    case kParamInUnvoiced:
        return { "Unvoiced Noise",  "in_unvoiced",      0.0f, 1.0f, 0.7f,  false, false };
    case kParamInMix:
        return { "Bird Mix",        "in_mix",           0.0f, 1.0f, 1.0f,  false, false };
    case kParamOutLevel:
        return { "Input Level",     "out_level",        0.0f, 1.0f, 0.0f,  false, false, true };
    case kParamOutInHz:
        return { "Input Pitch",     "out_in_hz",        0.0f, 2000.0f, 0.0f, false, false, true };
    case kParamOutBirdHz:
        return { "Bird Pitch",      "out_bird_hz",      0.0f, 12000.0f, 0.0f, false, false, true };
    case kParamOutVoiced:
        return { "Input State",     "out_voiced",       0.0f, 1.0f, 0.0f,  false, false, true };
    default:
        return { "Unknown", "unknown", 0.0f, 1.0f, 0.0f, false, false };
    }
}

// Preset voice parameters decoded from normalized storage
struct PresetParams {
    float level;
    float noise;
    float roughness;
    float timbre;
    float vibratoRateHz;      // 0-20 Hz
    float vibratoDepthCents;  // 0-300 cents
    float bend;               // -1 to +1
    float harmonic;
    float amRateHz;           // 0-30 Hz
    float pitchSemitones;     // ±12 semitones
    float durationSec;        // 0.05-2.0 s syllable length
    float respiration;        // 0-1 breathing depth
    float amDepth;            // 0-1 AM modulation depth
    float formant1Hz;         // 200-8000 Hz fixed resonance 1
    float formant2Hz;         // 200-8000 Hz fixed resonance 2
    float formantQ;           // 0.7-20 shared formant Q
    float coupling;           // 0-1 two-oscillator coupling
    float voiceOffset;        // 0-1 secondary voice frequency offset (0=unison, 1=+octave)
    float regime;             // 0-1 → gammaScale 1-4 (ODE regime shift, independent of harmonic band)
    float tracheaCm;          // 0-10 cm tracheal tube (0=simple mix, >0=physics pi(t) coupling)

    // Syllable shaping, set by the engine's phrase generator (not stored parameters).
    bool  shaped      = false;  // sin^0.5 amplitude window over durationSec; ignores note-off
    float sweepOct    = 0.0f;   // signed octaves swept over the syllable
    bool  arch        = false;  // sweep goes there and back (sweepOct < 0 gives a valley)
};

inline PresetParams decodePreset(const float* values, std::uint32_t preset)
{
    const std::uint32_t base = presetParam(preset, 0);
    PresetParams p {};
    p.level             = values[base + kPresetParamLevel];
    p.noise             = values[base + kPresetParamNoise];
    p.roughness         = values[base + kPresetParamRoughness];
    p.timbre            = values[base + kPresetParamTimbre];
    p.vibratoRateHz     = values[base + kPresetParamVibRate]  * 20.0f;
    p.vibratoDepthCents = values[base + kPresetParamVibDepth] * 300.0f;
    p.bend              = values[base + kPresetParamBend];
    p.harmonic          = values[base + kPresetParamHarmonic];
    p.amRateHz          = values[base + kPresetParamAMRate]   * 30.0f;
    p.pitchSemitones    = values[base + kPresetParamPitch]    * 12.0f;
    p.durationSec       = 0.05f + values[base + kPresetParamDuration] * 1.95f;
    p.respiration       = values[base + kPresetParamRespiration];
    p.amDepth           = values[base + kPresetParamAMDepth];
    p.formant1Hz        = values[base + kPresetParamFormant1];
    p.formant2Hz        = values[base + kPresetParamFormant2];
    p.formantQ          = values[base + kPresetParamFormantQ];
    p.coupling          = values[base + kPresetParamCoupling];
    p.voiceOffset       = values[base + kPresetParamVoiceOffset];
    p.regime            = values[base + kPresetParamRegime];
    p.tracheaCm         = values[base + kPresetParamTracheaCm];
    return p;
}

} // namespace downspout::syrinx
