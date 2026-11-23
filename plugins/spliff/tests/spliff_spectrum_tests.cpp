#include "spliff_spectrum.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

static int gPassed = 0;
static int gFailed = 0;

static void check(const char* name, bool condition)
{
    if (condition) {
        ++gPassed;
    } else {
        std::printf("FAIL: %s\n", name);
        ++gFailed;
    }
}

using namespace downspout::spliff;

static std::vector<float> sine(const double hz, const double seconds, const float level = 0.5f,
                               const double sampleRate = 48000.0)
{
    const std::size_t n = static_cast<std::size_t>(seconds * sampleRate);
    std::vector<float> out(n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<float>(level * std::sin(2.0 * 3.141592653589793 * hz * i / sampleRate));
    return out;
}

static SpectrumState analyse(const std::vector<float>& sig, const float sr = 48000.0f,
                             const std::uint32_t block = 128)
{
    SpectrumState st;
    resetSpectrum(st);
    for (std::size_t pos = 0; pos < sig.size(); pos += block) {
        const std::uint32_t n = static_cast<std::uint32_t>(std::min<std::size_t>(block, sig.size() - pos));
        analyseBlock(st, sr, n, sig.data() + pos, sig.data() + pos);
    }
    return st;
}

static int bandOf(const double hz)
{
    for (int k = 0; k < kSpectrumBins; ++k)
        if (hz >= spectrumBandEdgeHz(k) && hz < spectrumBandEdgeHz(k + 1)) return k;
    return -1;
}

static void testSpectrumBands()
{
    check("the bands run from 20 Hz to 20 kHz",
          std::fabs(spectrumBandEdgeHz(0) - 20.0) < 1e-9
          && std::fabs(spectrumBandEdgeHz(kSpectrumBins) - 20000.0) < 1e-6);

    const SpectrumState silent = analyse(std::vector<float>(48000, 0.0f));
    bool floor = true;
    for (const float v : silent.levelDb) floor = floor && v == kSpectrumFloorDb;
    check("silence rests on the floor", floor);

    for (const double hz : {55.0, 220.0, 1000.0, 4400.0, 12000.0}) {
        const SpectrumState st = analyse(sine(hz, 0.5));
        const int band = bandOf(hz);
        float best = -200.0f;
        int bestBand = -1;
        for (int k = 0; k < kSpectrumBins; ++k)
            if (st.levelDb[static_cast<std::size_t>(k)] > best) {
                best = st.levelDb[static_cast<std::size_t>(k)];
                bestBand = k;
            }
        char name[96];
        std::snprintf(name, sizeof(name), "%.0f Hz peaks in its own band (+-1)", hz);
        check(name, std::abs(bestBand - band) <= 1);
        std::snprintf(name, sizeof(name), "%.0f Hz at -6 dBFS reads about -6 dB (got %.1f)", hz, static_cast<double>(best));
        check(name, std::fabs(static_cast<double>(best) + 6.02) < 3.0);
        const int far = (band + kSpectrumBins / 2) % kSpectrumBins;  // 4+ octaves away
        std::snprintf(name, sizeof(name), "%.0f Hz leaves a band 4 octaves away more than 40 dB down", hz);
        check(name, st.levelDb[static_cast<std::size_t>(far)] < best - 40.0f);
    }
}

static void testSpectrumBlocksAndFall()
{
    const auto tone = sine(777.0, 0.4);
    const SpectrumState a = analyse(tone, 48000.0f, 37);
    const SpectrumState b = analyse(tone, 48000.0f, 4096);
    check("block size does not change the spectrum", a.levelDb == b.levelDb);

    std::vector<float> sig = sine(1000.0, 0.5);
    const std::size_t toneEnd = sig.size();
    sig.resize(toneEnd + 12000, 0.0f);  // 0.25 s of silence
    const SpectrumState tail = analyse(sig);
    const int band = bandOf(1000.0);
    const float held = analyse(sine(1000.0, 0.5)).levelDb[static_cast<std::size_t>(band)];
    check("a stopped tone fades no faster than the fall rate",
          tail.levelDb[static_cast<std::size_t>(band)] > held - kSpectrumFallDbPerSecond * 0.25f - 4.0f);
    check("a stopped tone has begun to fall",
          tail.levelDb[static_cast<std::size_t>(band)] < held - 3.0f);
    sig.resize(toneEnd + 48000 * 4, 0.0f);
    check("a stopped tone reaches the floor",
          analyse(sig).levelDb[static_cast<std::size_t>(band)] == kSpectrumFloorDb);
}

static void testSpectrumRoutingAndRange()
{
    const SpectrumState low = analyse(sine(1000.0, 0.5, 0.5, 22050.0), 22050.0f);
    check("bands above Nyquist stay on the floor",
          low.levelDb[static_cast<std::size_t>(kSpectrumBins - 1)] == kSpectrumFloorDb
          && low.levelDb[static_cast<std::size_t>(bandOf(1000.0))] > -12.0f);

    const auto l = sine(1000.0, 0.5);
    std::vector<float> r(l.size());
    for (std::size_t i = 0; i < l.size(); ++i) r[i] = -l[i];
    SpectrumState st;
    resetSpectrum(st);
    analyseBlock(st, 48000.0f, static_cast<std::uint32_t>(l.size()), l.data(), r.data());
    check("out-of-phase channels cancel in the mono sum",
          st.levelDb[static_cast<std::size_t>(bandOf(1000.0))] == kSpectrumFloorDb);

    std::vector<float> noise(480000);
    unsigned rnd = 1u;
    for (auto& x : noise) {
        rnd = rnd * 1664525u + 1013904223u;
        x = (static_cast<float>(rnd >> 8) / 8388608.0f - 1.0f) * 0.5f;
    }
    const SpectrumState analysed = analyse(noise);
    bool finite = true;
    for (const float v : analysed.levelDb)
        finite = finite && std::isfinite(v) && v >= kSpectrumFloorDb && v <= 6.0f;
    check("noise gives finite levels inside the range", finite);
}

int main()
{
    testSpectrumBands();
    testSpectrumBlocksAndFall();
    testSpectrumRoutingAndRange();

    std::printf("\n%d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
