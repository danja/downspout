#include "sieve_engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace downspout::sieve {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Periodic sqrt-Hann analysis and synthesis windows with 75 % overlap sum to a
// constant 2 (the mean of a Hann window is 1/2 and four frames overlap), so the
// overlap-add has to be scaled by its reciprocal to pass a unity mask at 0 dB.
constexpr float kOverlapGain = 2.0f * (float)kHop / (float)kFftSize;

void fft(std::complex<float>* a, int n, bool inverse) {
    // bit reversal
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (int len = 2; len <= n; len <<= 1) {
        float ang = (inverse ? 2.0f : -2.0f) * kPi / (float)len;
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (int j = 0; j < len / 2; ++j) {
                std::complex<float> u = a[i + j];
                std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse) {
        for (int i = 0; i < n; ++i) a[i] /= (float)n;
    }
}

// nearest scale note in semitones (any octave) to a given midi note
int nearestScaleMidi(int midi, int root, int scale) {
    int best = midi;
    int bestD = 1000;
    for (int m = midi - 12; m <= midi + 12; ++m) {
        if (m < 0 || m > 127) continue;
        int pc = ((m - root) % 12 + 12) % 12;
        bool ok = false;
        for (int i = 0; i < kScaleDegreeCount[scale]; ++i)
            if (kScaleIntervals[scale][i] == pc) { ok = true; break; }
        if (!ok) continue;
        int d = std::abs(m - midi);
        if (d < bestD) { bestD = d; best = m; }
    }
    return best;
}

}  // namespace

void activate(Engine& e, double sampleRate) {
    e.sampleRate = sampleRate;
    e.allocated = true;
    const int N = kFftSize;
    for (int ch = 0; ch < 2; ++ch) {
        auto& c = e.channels[ch];
        c.fifo.assign(N, 0.0f);
        c.accum.assign(2 * N, 0.0f);
        c.window.assign(N, 0.0f);
        for (int i = 0; i < N; ++i) {
            // periodic Hann, so four 75 %-overlapped frames sum to exactly 2
            const float hann = 0.5f - 0.5f * std::cos(2.0f * kPi * (float)i / (float)N);
            c.window[i] = std::sqrt(hann);
        }
        c.fifoPos = 0;
        // outPos is the accum slot of the sample being output, which is one
        // full FFT frame behind the input. At input sample 0 that is -kFftSize.
        c.outPos = N;
        c.hopCounter = 0;
        c.samples = 0;
    }
    e.fftBuf.assign(N, std::complex<float>(0, 0));
    e.mask.assign(kBins, std::complex<float>(1, 0));
    e.latency = N; // output is delayed by one full frame
    e.heldNotes.fill(false);
    e.heldCount = 0;
}

void reset(Engine& e) {
    if (!e.allocated) return;
    for (int ch = 0; ch < 2; ++ch) {
        auto& c = e.channels[ch];
        std::fill(c.fifo.begin(), c.fifo.end(), 0.0f);
        std::fill(c.accum.begin(), c.accum.end(), 0.0f);
        c.fifoPos = 0;
        c.outPos = kFftSize;
        c.hopCounter = 0;
        c.samples = 0;
    }
    e.heldNotes.fill(false);
    e.heldCount = 0;
}

int latencySamples(const Engine& e) noexcept { return e.latency; }

void setHeldNote(Engine& e, int note, bool down) noexcept {
    if (note < 0 || note > 127) return;
    e.heldNotes[static_cast<std::size_t>(note)] = down;
}

void processBlock(Engine& e, const Params& p, const float* inL, const float* inR,
                  float* outL, float* outR, std::uint32_t frames,
                  const std::uint8_t* midi, std::uint32_t midiCount) {
    if (!e.allocated) {
        for (std::uint32_t i = 0; i < frames; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
        return;
    }
    // Packed note events (status, note, velocity) for callers that do not use
    // setHeldNote(); the DPF wrapper uses setHeldNote() so it can filter by channel.
    for (std::uint32_t i = 0; i < midiCount; i += 3) {
        if (i + 2 >= midiCount) break;
        const int status = midi[i];
        const int note = midi[i + 1];
        const int vel = midi[i + 2];
        if ((status & 0xF0) == 0x90 && vel > 0) e.heldNotes[note] = true;
        else if ((status & 0xF0) == 0x80 || ((status & 0xF0) == 0x90 && vel == 0)) e.heldNotes[note] = false;
    }
    e.heldCount = 0;
    for (int n = 0; n < 128; ++n) if (e.heldNotes[n]) ++e.heldCount;

    const int root = (int)std::lround(p.root);
    const int scale = (int)std::clamp((int)std::lround(p.scale), 0, kScaleCount - 1);
    const float width = std::max(1.0f, p.width);
    const float floorGain = std::pow(10.0f, p.floor / 20.0f);
    const float tilt = p.tilt;

    // Build mask
    for (int b = 0; b < kBins; ++b) {
        const float freq = (float)b * (float)e.sampleRate / (float)kFftSize;
        if (freq < 1.0f) { e.mask[b] = std::complex<float>(1.0f, 0.0f); continue; }
        const float midiNote = 69.0f + 12.0f * std::log2(freq / 440.0f);
        int nearestMidi = -1;
        int centsDist = 10000;
        if (p.source >= 0.5f && e.heldCount > 0) {
            // nearest held note
            int best = -1; int bestD = 10000;
            for (int n = 0; n < 128; ++n) {
                if (!e.heldNotes[n]) continue;
                const int d = (int)std::lround(std::fabs(midiNote - (float)n) * 100.0f);
                if (d < bestD) { bestD = d; best = n; }
            }
            if (best >= 0) { nearestMidi = best; centsDist = bestD; }
        } else {
            // Scale source, and the fallback when Held MIDI has no notes held
            const int nm = nearestScaleMidi((int)std::lround(midiNote), root, scale);
            nearestMidi = nm;
            centsDist = (int)std::lround(std::fabs(midiNote - (float)nm) * 100.0f);
        }
        // gaussian gain
        float gain = std::exp(-0.5f * (centsDist / width) * (centsDist / width));
        gain = std::max(floorGain, gain);
        // tilt: odd/even bins
        float tiltFactor = 1.0f;
        if (b % 2 == 1) tiltFactor = 1.0f + tilt * 0.3f;
        else tiltFactor = 1.0f - tilt * 0.3f;
        tiltFactor = std::clamp(tiltFactor, 0.1f, 2.0f);
        gain = std::clamp(gain * tiltFactor, floorGain, 1.0f);
        e.mask[b] = std::complex<float>(gain, 0.0f);
    }

    const float mix = std::clamp(p.mix, 0.0f, 1.0f);
    const float levelGain = std::pow(10.0f, p.level / 20.0f);
    const int N = kFftSize;
    const int ring = 2 * N;

    for (std::uint32_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            auto& c = e.channels[ch];
            const float* in = (ch == 0) ? inL : inR;
            float* out = (ch == 0) ? outL : outR;

            // The fifo slot about to be overwritten holds the input from a full
            // frame ago: that is the dry signal, delayed to match the wet path.
            const float dry = c.fifo[static_cast<std::size_t>(c.fifoPos)];
            c.fifo[static_cast<std::size_t>(c.fifoPos)] = in[i];
            c.fifoPos = (c.fifoPos + 1) % N;
            ++c.hopCounter;

            if (c.hopCounter >= kHop) {
                c.hopCounter = 0;
                // Newest frame: fifo holds the last N input samples, oldest at fifoPos.
                for (int n = 0; n < N; ++n) {
                    const int idx = (c.fifoPos + n) % N;
                    e.fftBuf[static_cast<std::size_t>(n)] =
                        std::complex<float>(c.fifo[static_cast<std::size_t>(idx)] * c.window[n], 0.0f);
                }
                fft(e.fftBuf.data(), N, false);
                for (int b = 0; b < kBins; ++b) {
                    e.fftBuf[static_cast<std::size_t>(b)] *= e.mask[static_cast<std::size_t>(b)];
                    if (b > 0 && b < kBins - 1)
                        e.fftBuf[static_cast<std::size_t>(N - b)] =
                            std::conj(e.fftBuf[static_cast<std::size_t>(b)]);
                }
                fft(e.fftBuf.data(), N, true);
                // This frame covers the last N input samples and therefore lands
                // N-1..0 slots *ahead* of the sample being output. The 2N ring
                // keeps the write region clear of the read slot.
                for (int n = 0; n < N; ++n) {
                    const int idx = (c.outPos + 1 + n) % ring;
                    c.accum[static_cast<std::size_t>(idx)] +=
                        e.fftBuf[static_cast<std::size_t>(n)].real() * c.window[n] * kOverlapGain;
                }
            }

            // Every frame that covers this sample has already been added, so
            // the slot can be read and cleared for its next 2N-sample pass.
            const float wet = c.accum[static_cast<std::size_t>(c.outPos)];
            c.accum[static_cast<std::size_t>(c.outPos)] = 0.0f;
            out[i] = ((1.0f - mix) * dry + mix * wet) * levelGain;

            c.outPos = (c.outPos + 1) % ring;
            ++c.samples;
        }
    }
}

}  // namespace downspout::sieve
