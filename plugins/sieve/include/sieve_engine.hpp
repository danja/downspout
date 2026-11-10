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
        std::vector<float> accum;    // overlap-add ring, size 2 * fftSize
        std::vector<float> window;   // periodic sqrt-hann, size fftSize
        int fifoPos = 0;             // next fifo slot to write (oldest sample)
        int outPos = 0;              // accum slot of the sample being output
        int hopCounter = 0;
        std::uint64_t samples = 0;   // samples seen, for the start-up delay
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
// Set or clear one held note. Used for the "Held MIDI" source; the caller is
// responsible for channel filtering.
void setHeldNote(Engine& e, int note, bool down) noexcept;

// True for a note-on/note-off status on the selected channel. `midiCh` is 0 for
// all channels, otherwise 1-16. Other message types never match.
[[nodiscard]] bool midiChannelMatches(std::uint8_t status, float midiCh) noexcept;

// Apply one note event if it passes the channel filter. A note-on with velocity 0
// is a note-off. Returns whether the event matched.
bool handleMidiNote(Engine& e, std::uint8_t status, std::uint8_t note,
                    std::uint8_t velocity, float midiCh) noexcept;

void processBlock(Engine& e, const Params& p, const float* inL, const float* inR,
                  float* outL, float* outR, std::uint32_t frames);

}  // namespace downspout::sieve