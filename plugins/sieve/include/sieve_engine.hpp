#pragma once

#include "sieve_params.hpp"

#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace downspout::sieve {

struct Engine {
    double sampleRate = 48000.0;
    bool allocated = false;

    // STFT state per channel
    struct Channel {
        std::vector<float> fifo;     // input ring, size fftSize
        std::vector<float> accum;    // output overlap-add ring
        std::vector<float> window;   // sqrt-hann
        int fifoPos = 0;
        int hopCounter = 0;
        std::int64_t frameCounter = 0;
    };
    std::array<Channel, 2> channels;

    // FFT buffers
    std::vector<std::complex<float>> fftBuf;
    std::vector<std::complex<float>> mask; // per bin gain

    // MIDI held notes
    std::array<bool, 128> heldNotes {};
    int rootPc = 0;
    int heldCount = 0;

    // latency
    int latency = 0;
};

void activate(Engine& e, double sampleRate);
void reset(Engine& e);
int latencySamples(const Engine& e) noexcept;
void processBlock(Engine& e, const Params& p, const float* inL, const float* inR,
                  float* outL, float* outR, std::uint32_t frames,
                  const std::uint8_t* midi, std::uint32_t midiCount);

}  // namespace downspout::sieve