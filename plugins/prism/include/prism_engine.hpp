#pragma once

#include "prism_fft.hpp"
#include "prism_params.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace downspout::prism {

// Spectral delay. Each channel is an overlapping STFT (sqrt-Hann analysis and synthesis
// windows, hop = N/8). Every frequency bin is read back from the spectrum stored some
// whole number of frames ago, and the rest of the delay (under half a hop) is applied as
// a phase rotation. The delay and feedback of a bin come from the band values,
// interpolated in log frequency. The dry path is delayed by N samples to line up.
struct Engine {
    struct Channel {
        std::vector<float> fifo;      // last N input samples, circular
        std::vector<float> accum;     // overlap-add output, circular
        std::vector<float> history;   // rows x bins x (re, im)
        std::vector<int> delayFrames; // whole frames of delay per bin
        std::vector<float> phase;     // bins x (cos, sin): the fractional-frame rotation
        std::vector<float> gain;      // per-bin compensation for that rotation
        int position = 0;
        std::int64_t frame = 0;
    };

    double sampleRate = 48000.0;
    bool allocated = false;

    int sizeIndex = -1;
    int fftSize = 0;
    int hop = 0;
    int bins = 0;
    int rows = 0;
    int maxFrames = 0;
    int hopCounter = 0;
    std::size_t historyCapacity = 0;

    std::array<Fft, kFftSizeCount> ffts;
    std::array<std::vector<float>, kFftSizeCount> windows;
    std::array<float, kFftSizeCount> cola {};

    std::vector<float> scratch;
    std::array<Channel, 2> channels;

    // Per frame, shared by both channels.
    std::vector<int> binBand;
    std::vector<float> binFrac;
    std::vector<float> binCut;
    std::vector<float> binFeedback;
    std::vector<float> soloTarget;
    std::vector<float> soloWeight;
    int mappedBands = 0;
    float mappedLowCut = -1.0f;
    float mappedHighCut = -1.0f;
    double bpm = kDefaultTempo;  // host tempo for synced delays
    float mixSmoothed = 0.5f;
    bool mixPrimed = false;  // the first block starts the mix at its parameter value

    // One-pole smoothed band values: delay L, delay R, feedback, per band.
    std::array<float, kMaxBands * 3> smoothed {};
    bool smoothedValid = false;
};

// Allocates for a sample rate. Not real-time safe; call from activate / sampleRateChanged.
void activate(Engine& engine, double sampleRate);

// The tempo synced delays follow. Call before processBlock; invalid values mean 120 BPM.
void setTempo(Engine& engine, double bpm) noexcept;

// Clears audio state only; no parameter is touched.
void reset(Engine& engine);

// Latency in samples for the active resolution.
[[nodiscard]] int latencySamples(const Engine& engine) noexcept;

void processBlock(Engine& engine, const Parameters& params, const float* inL, const float* inR, float* outL,
                  float* outR, std::size_t frames);

}  // namespace downspout::prism
