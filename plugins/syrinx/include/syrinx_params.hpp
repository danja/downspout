#pragma once

#include <array>
#include <cstdint>
#include <cmath>

namespace downspout::syrinx {

static constexpr std::uint32_t kPresetCount = 10;
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
static constexpr std::uint32_t kParamDistance         = kPresetCount * kParamsPerPreset + 0;
static constexpr std::uint32_t kParamMasterGain       = kPresetCount * kParamsPerPreset + 1;
static constexpr std::uint32_t kParamSelectedPreset   = kPresetCount * kParamsPerPreset + 2;
static constexpr std::uint32_t kParameterCount        = kPresetCount * kParamsPerPreset + 3;

inline constexpr std::uint32_t presetParam(std::uint32_t preset, std::uint32_t param)
{
    return preset * kParamsPerPreset + param;
}

struct ParameterSpec {
    const char* name;
    const char* symbol;
    float minimum;
    float maximum;
    float defaultValue;
    bool boolean;
    bool integer;
};

static const char* const kPresetNames[kPresetCount] = {
    "Wren", "Thrush", "Warbler", "Finch", "Robin",
    "Nightjar", "Pigeon", "Hummingbird", "Starling", "Custom"
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
    if (index < kPresetCount * kParamsPerPreset) {
        const std::uint32_t preset = index / kParamsPerPreset;
        const std::uint32_t p     = index % kParamsPerPreset;
        const float defVal = kPresetDefaults[preset][p];
        return detail::makePresetSpec(preset, p, defVal);
    }
    switch (index) {
    case kParamDistance:
        return { "Distance",        "distance",         0.0f, 1.0f, 0.25f, false, false };
    case kParamMasterGain:
        return { "Master Gain",     "master_gain",      0.0f, 1.0f, 0.75f, false, false };
    case kParamSelectedPreset:
        return { "Selected Preset", "selected_preset",  0.0f, 9.0f, 0.0f,  false, true  };
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
};

inline PresetParams decodePreset(const float* values, std::uint32_t preset)
{
    const std::uint32_t base = preset * kParamsPerPreset;
    PresetParams p;
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
