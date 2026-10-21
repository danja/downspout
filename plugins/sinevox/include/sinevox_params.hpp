#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace downspout::sinevox {

// ── Parameters ─────────────────────────────────────────────────────────────
//
// Sinevox is a formant tracker with two ways of speaking the result:
//
//   in 1,2  speech (or any signal with resonances) to be analysed
//   in 3,4  carrier, only used in vocoder mode
//   out 1,2 the result
//
// The tracker finds up to eight dominant spectral peaks (formants) in the
// summed 1/2 input and reports a centre frequency, a Q and an amplitude for
// each. Vocoder mode drives a parallel bank of band-pass filters on 3/4 with
// them; sine-wave-speech mode drives a bank of sine oscillators instead.

enum class ParamId : std::uint32_t {
    mode = 0,        // 0 vocoder, 1 sine wave speech, 2 both
    formants,        // tracked formants, 1 - 8
    bypass,          // speech on 1/2 passes straight through

    // Analysis
    gateDb,          // below this input level the tracker reports silence
    bwLimit,         // Hz, wider resonances are not treated as formants
    glideMs,         // frequency / Q smoothing
    attackMs,        // amplitude rise
    releaseMs,       // amplitude fall

    // Synthesis
    shift,           // semitones, transposes every formant
    qScale,          // multiplies every tracked Q
    tilt,            // dB/oct about 1 kHz applied to formant amplitudes
    levelDb,
    dry,             // % of the speech input mixed into the output
    carrierNorm,     // % of carrier level normalisation (vocoder)
    spread,          // % stereo spread of the sines (SWS)
    f0Mix,           // % level of a sine at the tracked fundamental

    // MIDI
    noteShift,       // 0/1, held MIDI note transposes the formants
    midiOut,         // 0 off, 1 formant CCs, 2 formant notes
    outChannel,      // 1 - 16
    ccChannel,       // 1 - 16
    ccShift,
    ccQ,
    ccFormants,
    ccTilt,
    ccMode,

    // Read-only status
    outLevelDb,
    outActive,
    outF0,
    outFreq1, outFreq2, outFreq3, outFreq4, outFreq5, outFreq6, outFreq7, outFreq8,
    outAmp1, outAmp2, outAmp3, outAmp4, outAmp5, outAmp6, outAmp7, outAmp8,
    outQ1, outQ2, outQ3, outQ4, outQ5, outQ6, outQ7, outQ8,
};

inline constexpr std::size_t kParameterCount = 52;
inline constexpr std::size_t kInputParameterCount = 25;
inline constexpr int kMaxFormants = 8;

struct ParamSpec {
    const char* symbol;
    const char* name;
    const char* unit;  // empty string when unitless
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

inline constexpr std::array<ParamSpec, kParameterCount> kParameterSpecs = {{
    {"mode", "Mode", "", 0.0f, 2.0f, 0.0f, true, false},
    {"formants", "Formants", "", 1.0f, 8.0f, 4.0f, true, false},
    {"bypass", "Bypass", "", 0.0f, 1.0f, 0.0f, true, false},

    {"gate_db", "Gate", "dB", -80.0f, -20.0f, -55.0f, false, false},
    {"bw_limit", "BW Limit", "Hz", 100.0f, 3000.0f, 700.0f, false, false},
    {"glide_ms", "Glide", "ms", 1.0f, 200.0f, 25.0f, false, false},
    {"attack_ms", "Attack", "ms", 1.0f, 100.0f, 6.0f, false, false},
    {"release_ms", "Release", "ms", 5.0f, 500.0f, 70.0f, false, false},

    {"shift", "Shift", "st", -12.0f, 12.0f, 0.0f, false, false},
    {"q_scale", "Q Scale", "x", 0.25f, 4.0f, 1.0f, false, false},
    {"tilt", "Tilt", "dB/oct", -12.0f, 12.0f, 0.0f, false, false},
    {"level_db", "Level", "dB", -24.0f, 24.0f, 0.0f, false, false},
    {"dry", "Speech", "%", 0.0f, 100.0f, 0.0f, false, false},
    {"carrier_norm", "Carrier Norm", "%", 0.0f, 100.0f, 100.0f, false, false},
    {"spread", "Sine Spread", "%", 0.0f, 100.0f, 30.0f, false, false},
    {"f0_mix", "Fundamental", "%", 0.0f, 100.0f, 0.0f, false, false},

    {"note_shift", "Note Shift", "", 0.0f, 1.0f, 0.0f, true, false},
    {"midi_out", "MIDI Out", "", 0.0f, 2.0f, 0.0f, true, false},
    {"out_channel", "Out Channel", "", 1.0f, 16.0f, 1.0f, true, false},
    {"cc_channel", "CC Channel", "", 1.0f, 16.0f, 1.0f, true, false},
    {"cc_shift", "CC Shift", "", 0.0f, 127.0f, 1.0f, true, false},
    {"cc_q", "CC Q Scale", "", 0.0f, 127.0f, 2.0f, true, false},
    {"cc_formants", "CC Formants", "", 0.0f, 127.0f, 3.0f, true, false},
    {"cc_tilt", "CC Tilt", "", 0.0f, 127.0f, 4.0f, true, false},
    {"cc_mode", "CC Mode", "", 0.0f, 127.0f, 5.0f, true, false},

    {"out_level_db", "Input Level", "dB", -90.0f, 0.0f, -90.0f, false, true},
    {"out_active", "Active", "", 0.0f, 8.0f, 0.0f, true, true},
    {"out_f0", "Fundamental Freq", "Hz", 0.0f, 1000.0f, 0.0f, false, true},
    {"out_freq_1", "F1 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_2", "F2 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_3", "F3 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_4", "F4 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_5", "F5 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_6", "F6 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_7", "F7 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_freq_8", "F8 Freq", "Hz", 0.0f, 20000.0f, 0.0f, false, true},
    {"out_amp_1", "F1 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_2", "F2 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_3", "F3 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_4", "F4 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_5", "F5 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_6", "F6 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_7", "F7 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_amp_8", "F8 Amp", "dB", -90.0f, 6.0f, -90.0f, false, true},
    {"out_q_1", "F1 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_2", "F2 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_3", "F3 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_4", "F4 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_5", "F5 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_6", "F6 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_7", "F7 Q", "", 0.0f, 100.0f, 0.0f, false, true},
    {"out_q_8", "F8 Q", "", 0.0f, 100.0f, 0.0f, false, true},
}};

enum class ModeId : std::uint32_t {
    vocoder = 0,
    sineWave,
    both,
    count,
};

enum class MidiOutId : std::uint32_t {
    off = 0,
    controllers,
    notes,
    count,
};

// ── Fixed MIDI maps ────────────────────────────────────────────────────────
//
// Input: CC 1-5 follow the Downspout convention (Drift's default lanes). Each
// controller number is a parameter, and 0 turns that override off.
//
// Output (midiOut = controllers): formant i (0-7) frequency is sent on CC
// 20+i, mapped logarithmically 80 Hz - 8 kHz onto 0-127, and its amplitude on
// CC 102+i, mapped -60 dB - 0 dB onto 0-127. Both are in the undefined
// controller ranges so they do not collide with GM controllers.

inline constexpr std::uint8_t kOutFreqCcBase = 20;
inline constexpr std::uint8_t kOutAmpCcBase = 102;
inline constexpr float kOutFreqMinHz = 80.0f;
inline constexpr float kOutFreqMaxHz = 8000.0f;
inline constexpr float kOutAmpMinDb = -60.0f;

// A formant is "sounding" for MIDI notes when it is above this level.
inline constexpr float kNoteOnDb = -50.0f;

[[nodiscard]] constexpr std::uint32_t index(const ParamId id) noexcept
{
    return static_cast<std::uint32_t>(id);
}

// The fundamental (F0) is tracked separately from the formants, by normalised
// autocorrelation of the speech input. Fundamental Mix adds a sine at that pitch
// to the output in every mode; 0 % (the default) leaves the output unchanged.

// Bus layout: 0,1 speech, 2,3 carrier. Two outputs.
inline constexpr std::size_t kAudioInputs = 4;
inline constexpr std::size_t kAudioOutputs = 2;

}  // namespace downspout::sinevox
