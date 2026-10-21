#pragma once

#include "sinevox_params.hpp"

#include <array>
#include <complex>
#include <cstdint>
#include <optional>
#include <string>

namespace downspout::sinevox {

struct Parameters {
    float mode = 0.0f;
    float formants = 4.0f;
    float bypass = 0.0f;

    float gateDb = -55.0f;
    float bwLimit = 700.0f;
    float glideMs = 25.0f;
    float attackMs = 6.0f;
    float releaseMs = 70.0f;

    float shift = 0.0f;
    float qScale = 1.0f;
    float tilt = 0.0f;
    float levelDb = 0.0f;
    float dry = 0.0f;
    float carrierNorm = 100.0f;
    float spread = 30.0f;
    float f0Mix = 0.0f;

    float noteShift = 0.0f;
    float midiOut = 0.0f;
    float outChannel = 1.0f;
    float ccChannel = 1.0f;
    float ccShift = 1.0f;
    float ccQ = 2.0f;
    float ccFormants = 3.0f;
    float ccTilt = 4.0f;
    float ccMode = 5.0f;
};

inline constexpr int kMaxLpcOrder = 2 * kMaxFormants + 2;
inline constexpr int kMaxAnalysisWindow = 1024;
inline constexpr int kCoefInterval = 16;  // samples between filter coefficient updates

struct Biquad {
    float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f, a1 = 0.0f, a2 = 0.0f;
    float z1 = 0.0f, z2 = 0.0f;
};

// LPC formant analyser. Runs on the 1/2 input, decimated to ~16 kHz so that a
// modest all-pole model spends its poles on the speech band.
struct Analyzer {
    double sampleRate = 0.0;
    int decim = 3;
    double rate = 16000.0;   // analysis rate, sampleRate / decim
    int hop = 80;            // analysis samples between frames
    int window = 400;        // analysis samples per frame
    int decimCount = 0;
    int sinceHop = 0;
    int filled = 0;
    int pos = 0;
    float prevRaw = 0.0f;
    std::array<Biquad, 2> lowpass {};
    std::array<float, kMaxAnalysisWindow> raw {};
    std::array<float, kMaxAnalysisWindow> emph {};
    std::array<std::complex<double>, kMaxLpcOrder> roots {};
    int rootOrder = 0;
    bool haveRoots = false;
    float levelDb = -90.0f;   // last frame's input level
    float f0Hz = 0.0f;        // last frame's fundamental, 0 when unvoiced
    float f0Level = 0.0f;     // last frame's RMS when voiced
};

// One tracked formant. The `tgt` values are what the last analysis frame asked
// for; the `sm` values are the audio-rate smoothed versions the synths read.
struct Formant {
    float tgtFreq = 0.0f;
    float tgtQ = 1.0f;
    float tgtAmp = 0.0f;     // linear
    float smLogFreq = 0.0f;  // log2 Hz
    float smQ = 1.0f;
    float smAmp = 0.0f;

    // Sine-wave-speech oscillator.
    double phase = 0.0;

    // Vocoder band, per channel.
    float ic1[2] = {0.0f, 0.0f};
    float ic2[2] = {0.0f, 0.0f};
    // Cached per-sub-block coefficients.
    float k = 1.0f, a1 = 0.0f, a2 = 0.0f, a3 = 0.0f;
    float voxScale = 0.0f;    // vocoder band gain, before the smoothed amplitude
    float sineScale = 0.0f;   // tilt gain for the sine, before the smoothed amplitude
    double phaseInc = 0.0;
    float panL = 0.7071f, panR = 0.7071f;
};

struct MidiOutEvent {
    std::uint32_t frame = 0;
    std::uint8_t data[3] = {0, 0, 0};
};

inline constexpr int kMaxMidiOut = 48;

struct MidiOutBlock {
    std::array<MidiOutEvent, kMaxMidiOut> events {};
    int count = 0;
};

struct EngineState {
    Analyzer analyzer {};
    std::array<Formant, kMaxFormants> formants {};
    double sampleRate = 0.0;

    // Fundamental sine.
    float f0SmLogFreq = 0.0f;
    float f0SmAmp = 0.0f;
    double f0Phase = 0.0;

    // Smoothers.
    float shiftSmooth = 0.0f;       // semitones, includes the held-note offset
    float carrierEnv = 0.0f;        // mean square of the carrier
    int coefCount = 0;

    // MIDI input.
    std::array<std::uint8_t, 16> heldNotes {};
    int heldCount = 0;

    // MIDI output.
    std::array<int, kMaxFormants> sentFreqCc {};
    std::array<int, kMaxFormants> sentAmpCc {};
    std::array<int, kMaxFormants> soundingNote {};
    std::array<std::uint32_t, kMaxFormants> noteHold {};
    std::uint32_t samplesSinceMidi = 0;
    int lastMidiOut = 0;
    int lastOutChannel = 0;

    // Measured outputs the wrapper publishes as status parameters.
    float outLevelDb = -90.0f;
    int outActive = 0;
    float outF0 = 0.0f;
    std::array<float, kMaxFormants> outFreq {};
    std::array<float, kMaxFormants> outAmpDb {};
    std::array<float, kMaxFormants> outQ {};
};

// ── API ────────────────────────────────────────────────────────────────────

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;

// Resets runtime state (filters, oscillators, trackers, held notes). Never
// touches the patch: the host calls this on activate().
void activate(EngineState& state) noexcept;

// Frequency in Hz that a formant sounds at after Shift and held-note offset.
[[nodiscard]] float effectiveFrequency(float hz, float shiftSemitones, double sampleRate) noexcept;

// Handles one raw MIDI message. CC overrides write into `p`; returns true if
// they changed anything.
bool handleMidi(EngineState& state, Parameters& p, const std::uint8_t* data, std::uint32_t size) noexcept;

void processBlock(EngineState& state,
                  const Parameters& params,
                  std::uint32_t frames,
                  double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept;

// Collects the MIDI output for the block just processed. Call once per block
// after processBlock.
void collectMidiOut(EngineState& state, const Parameters& params,
                    std::uint32_t frames, MidiOutBlock& out) noexcept;

[[nodiscard]] std::string serializeParameters(const Parameters& p);
[[nodiscard]] std::optional<Parameters> deserializeParameters(const std::string& text);

}  // namespace downspout::sinevox
