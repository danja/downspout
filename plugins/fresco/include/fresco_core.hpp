#pragma once

#include "fresco_image.hpp"

#include <array>
#include <cstdint>

namespace downspout::fresco {

// Fresco paints a picture into the audio's spectrogram. Short-time Fourier analysis (2048-point, Hann, hop 512) is run on the
// input; each frame's magnitudes are replaced by the picture's column under the playhead (rows map to frequency, brightness to
// level) and the frame is rebuilt and overlap-added. The phases come from the input (so the result keeps the input's timing and
// pitch character) or from a bank of bin-centred sinusoids (the "Noise" source: a clean, steady painting tone).
//
// Clean-room: an STFT with magnitudes replaced, from the standard definition. It shares no code with any other plugin.

inline constexpr int kFrame = 2048;                  // FFT size
inline constexpr int kHop = 512;                     // 75 % overlap
inline constexpr int kBins = kFrame / 2 + 1;
inline constexpr int kLatency = kFrame;              // samples, reported to the host (measured in the tests)
inline constexpr float kBinAmplitude = 0.02f;        // amplitude of a sinusoid at a white pixel

enum class Source : int { Input = 0, Tone = 1 };
inline constexpr int kSourceCount = 2;

struct Parameters {
    float lengthS = 10.0f;       // seconds for the playhead to cross the picture
    float rangeDb = 60.0f;       // level of a nearly black pixel (white is 0 dB)
    float contrast = 1.0f;       // gamma on the pixel brightness
    float minHz = 100.0f;        // bottom edge of the picture
    float maxHz = 16000.0f;      // top edge
    float logAxis = 1.0f;        // 1 = rows are log-spaced in frequency (a spectrogram's usual axis), 0 = linear
    float invert = 0.0f;         // 1 = dark pixels are loud
    float source = 1.0f;         // Source
    float follow = 1.0f;         // 0..1: how much the input level shapes the output (1 = silence in, silence out)
    float transport = 0.0f;      // 1 = playhead follows the host timeline
    float mix = 1.0f;
    float outputDb = 0.0f;
};

struct Transport {
    bool playing = false;
    bool valid = false;
    double bpm = 0.0;
    double beatPosition = 0.0;
};

struct Meters {
    float playhead = 0.0f;       // 0..1 across the picture
    float level = 0.0f;          // 0..1 mean brightness of the current column (after the mapping)
};

struct Channel {
    std::array<float, kFrame> in {}, acc {}, queue {}, dry {};
    std::array<float, kBins> noiseCos {}, noiseSin {};
    int inPos = 0, accPos = 0, queuePos = 0, dryPos = 0;
};

struct EngineState {
    std::array<Channel, 2> ch {};
    int hopCount = 0;
    std::uint64_t frameIndex = 0;
    double timeS = 0.0;          // free-running playhead position, seconds
    std::uint32_t rng = 1;
    float mixSmooth = 1.0f;
    bool mixStarted = false;     // the smoother starts at the parameter, not at a fixed value
    Meters meters {};
};

[[nodiscard]] Parameters clampParameters(const Parameters& p) noexcept;
void resetState(EngineState& state) noexcept;

// Magnitude (linear, 0..1; 1 = white at 0 dB) the picture asks for at each bin for a playhead position in 0..1. Exposed for tests and the panel.
void columnMagnitudes(const Image& image, const Parameters& p, float sampleRate, double playhead, float* mags /* kBins */) noexcept;

// `image` may be null or empty: the output is then silent (wet) so a missing file is obvious.
void processBlock(EngineState& state, const Parameters& params, const Image* image, float sampleRate, std::uint32_t frames, const Transport& transport,
                  const float* inLeft, const float* inRight, float* outLeft, float* outRight) noexcept;

}  // namespace downspout::fresco
