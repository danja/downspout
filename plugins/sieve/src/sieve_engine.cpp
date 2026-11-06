#include "sieve_engine.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

namespace downspout::sieve {
namespace {

constexpr float kPi = 3.14159265358979323846f;

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

bool inScale(int pc, int scale, int root) {
    pc = ((pc - root) % 12 + 12) % 12;
    for (int i = 0; i < kScaleDegreeCount[scale]; ++i)
        if (kScaleIntervals[scale][i] == pc) return true;
    return false;
}

// nearest scale note in semitones (any octave) to a given midi note
int nearestScaleMidi(int midi, int root, int scale) {
    // search nearest midi whose pc is in scale
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
        c.accum.assign(N, 0.0f);
        c.window.assign(N, 0.0f);
        for (int i = 0; i < N; ++i) {
            float w = 0.5f - 0.5f * std::cos(2.0f * kPi * i / (N - 1));
            c.window[i] = std::sqrt(w); // sqrt-hann
        }
        c.fifoPos = 0;
        c.hopCounter = 0;
        c.frameCounter = 0;
    }
    e.fftBuf.assign(N, std::complex<float>(0, 0));
    e.mask.assign(kBins, std::complex<float>(1, 0));
    e.latency = N; // report N samples
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
        c.hopCounter = 0;
        c.frameCounter = 0;
    }
    e.heldNotes.fill(false);
    e.heldCount = 0;
}

int latencySamples(const Engine& e) noexcept { return e.latency; }

void processBlock(Engine& e, const Params& p, const float* inL, const float* inR,
                  float* outL, float* outR, std::uint32_t frames,
                  const std::uint8_t* midi, std::uint32_t midiCount) {
    if (!e.allocated) {
        for (std::uint32_t i = 0; i < frames; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
        return;
    }
    // MIDI held notes
    for (std::uint32_t i = 0; i < midiCount; i += 3) {
        if (i + 2 >= midiCount) break;
        int status = midi[i];
        int note = midi[i+1];
        int vel = midi[i+2];
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
        float freq = (float)b * (float)e.sampleRate / (float)kFftSize;
        if (freq < 1.0f) { e.mask[b] = std::complex<float>(1.0f, 0.0f); continue; }
        float midiNote = 69.0f + 12.0f * std::log2(freq / 440.0f);
        int nearestMidi = -1;
        int centsDist = 10000;
        if (p.source >= 0.5f && e.heldCount > 0) {
            // nearest held note
            int best = -1; int bestD = 10000;
            for (int n = 0; n < 128; ++n) {
                if (!e.heldNotes[n]) continue;
                int d = (int)std::lround(std::fabs(midiNote - (float)n) * 100.0f);
                if (d < bestD) { bestD = d; best = n; }
            }
            if (best >= 0) { nearestMidi = best; centsDist = bestD; }
        } else {
            int nm = nearestScaleMidi((int)std::lround(midiNote), root, scale);
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

    // Process in hops
    for (std::uint32_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            auto& c = e.channels[ch];
            const float* in = (ch == 0) ? inL : inR;
            float* out = (ch == 0) ? outL : outR;
            c.fifo[c.fifoPos] = in[i];
            c.fifoPos = (c.fifoPos + 1) % kFftSize;
            c.hopCounter++;
            if (c.hopCounter >= kHop) {
                c.hopCounter = 0;
                // Build FFT input (reorder fifo)
                for (int n = 0; n < kFftSize; ++n) {
                    int idx = (c.fifoPos + n) % kFftSize;
                    float w = c.window[n];
                    e.fftBuf[n] = std::complex<float>(c.fifo[idx] * w, 0.0f);
                }
                fft(e.fftBuf.data(), kFftSize, false);
                // Apply mask
                for (int b = 0; b < kBins; ++b) {
                    e.fftBuf[b] *= e.mask[b];
                    if (b > 0 && b < kBins - 1) {
                        e.fftBuf[kFftSize - b] = std::conj(e.fftBuf[b]);
                    }
                }
                fft(e.fftBuf.data(), kFftSize, true);
                // Overlap-add into accum
                for (int n = 0; n < kFftSize; ++n) {
                    int idx = (c.fifoPos + n - kFftSize + kFftSize) % kFftSize; // align? use simple
                    // accum circular at frameCounter
                }
                // Simpler: write to accum ring at current position
                int base = (int)(c.frameCounter % kFftSize);
                for (int n = 0; n < kFftSize; ++n) {
                    int idx = (base + n) % kFftSize;
                    c.accum[idx] += e.fftBuf[n].real() * c.window[n];
                }
                c.frameCounter += kHop;
            }
            // Output
            int outIdx = (int)((c.frameCounter - kFftSize + kFftSize) % kFftSize);
            // read accum
            float val = c.accum[outIdx];
            c.accum[outIdx] = 0.0f;
            // mix
            float dry = in[i];
            float wet = val;
            out[i] = (1.0f - p.mix) * dry + p.mix * wet;
        }
    }
    // level
    for (std::uint32_t i = 0; i < frames; ++i) {
        outL[i] *= std::pow(10.0f, p.level / 20.0f);
        outR[i] *= std::pow(10.0f, p.level / 20.0f);
    }
}

}  // namespace downspout::sieve
