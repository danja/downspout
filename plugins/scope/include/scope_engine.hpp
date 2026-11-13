#pragma once

#include "scope_params.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace downspout::scope {

struct Engine {
    double sampleRate = 48000.0;
    bool allocated = false;
    bool demo = false;
    std::vector<float> fifo;
    std::vector<float> window;
    float windowMean = 1.0f;
    int fifoPos = 0;
    int hopCounter = 0;
    int filled = 0;
    std::vector<std::complex<float>> fftBuf;
    // smoothed spectrum (dB) per FFT bin
    std::vector<float> spectrum;
    // log-spaced bands for the UI
    std::array<float, kBands> bands {};
    // waveform samples for the oscilloscope view
    std::array<float, kWaveformSize> waveformL {};
    std::array<float, kWaveformSize> waveformR {};
    int waveformPos = 0;
    int waveformCounter = 0;
    bool waveformFresh = false;
    // set when a new FFT frame (and therefore a new band set) is ready
    bool fresh = false;
    bool primed = false;
    float demoPhase = 0.0f;
    float peakL = 0.0f, peakR = 0.0f;
    float rmsL = 0.0f, rmsR = 0.0f;
    float corr = 0.0f;
    float momentaryL = -120.0f, momentaryR = -120.0f;
    float shortL = -120.0f, shortR = -120.0f;
    float integratedL = -120.0f, integratedR = -120.0f;
    float accLL = 0.0f, accRR = 0.0f, accLR = 0.0f;
};

void activate(Engine& e, double sr);
void reset(Engine& e);
inline int latencySamples(const Engine&) noexcept { return 0; }
void processBlock(Engine& e, const Params& p, const float* inL, const float* inR, float* outL, float* outR, std::uint32_t frames);

// Band edges in Hz for band i (0 <= i <= kBands). Log-spaced from 20 Hz to 20 kHz.
float bandEdgeHz(int band) noexcept;
float bandCentreHz(int band) noexcept;

// accessors for the DPF wrapper
const std::array<float, kBands>& getBands(const Engine& e);
float getPeakL(const Engine& e);
float getPeakR(const Engine& e);
float getRmsL(const Engine& e);
float getRmsR(const Engine& e);
float getCorr(const Engine& e);
float getMomentaryLufs(const Engine& e);
float getMomentaryRufs(const Engine& e);
float getShortLufs(const Engine& e);
float getShortRufs(const Engine& e);
float getIntegratedLufs(const Engine& e);
float getIntegratedRufs(const Engine& e);
const std::array<float, kWaveformSize>& getWaveformL(const Engine& e);
const std::array<float, kWaveformSize>& getWaveformR(const Engine& e);

}  // namespace downspout::scope
