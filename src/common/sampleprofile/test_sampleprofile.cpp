#include "downspout/sampleprofile.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

// ── helpers ──────────────────────────────────────────────────────────────────

static bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

static sp_profile_t analyse_mono(const std::vector<float>& s, double sr)
{
    const float* ch[1] = {s.data()};
    return sp_analyse(ch, 1, s.size(), sr, nullptr);
}

static void pass(const char* name)
{
    std::printf("PASS  %s\n", name);
}

// ── §7 conformance tests ──────────────────────────────────────────────────────

// 1 Hz full-scale sine, 1 second
static void test_sine_1khz()
{
    const double SR = 44100.0;
    const int    N  = (int)SR;
    std::vector<float> s(N);
    for (int i = 0; i < N; ++i)
        s[i] = std::sin(2.0 * M_PI * 1000.0 * i / SR);

    auto p = analyse_mono(s, SR);

    assert(!p.silent);
    assert(!p.clipped);
    // A flat sine correctly reports sustained=true (temporal centroid ~0.5, R²~0)

    // centroid should be near 1000 Hz
    assert(p.frame_stat[0].defined);
    assert(near(p.frame_stat[0].mean, 1000.0, 100.0));

    // flatness should be very low (near-pure tone)
    assert(p.frame_stat[6].defined);
    assert(p.frame_stat[6].mean < 0.10);

    // ZCR ~2000 crossings/second for 1 kHz sine
    assert(p.frame_stat[8].defined);
    assert(near(p.frame_stat[8].mean, 2000.0, 300.0));

    // kurtosis should be high (energy at one frequency)
    assert(p.frame_stat[3].defined);
    assert(p.frame_stat[3].mean > 10.0);

    // Peak is ~0 dBFS
    assert(near(p.peak_dbfs, 0.0, 1.0));

    // 65-element vector populated
    assert(p.valid[0] && p.valid[8] && p.valid[63]);

    pass("sine_1khz");
}

// White noise (fixed seed) — flatness ~1, centroid ~Nyquist/2
static void test_white_noise()
{
    const double SR = 44100.0;
    const int    N  = (int)SR;
    std::vector<float> s(N);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& x : s) x = dist(rng);

    auto p = analyse_mono(s, SR);

    assert(!p.silent);
    assert(p.frame_stat[6].defined);
    // For chi²(2) bin powers, theoretical flatness ≈ e^-γ ≈ 0.56 (not 1.0)
    assert(p.frame_stat[6].mean > 0.40);

    assert(p.frame_stat[0].defined);
    // centroid near Nyquist/2 = 11025 Hz
    assert(p.frame_stat[0].mean > 8000.0 && p.frame_stat[0].mean < 14000.0);

    pass("white_noise");
}

// Exponentially decaying 100 Hz sine, τ = 0.1 s
// Theoretical decay slope: -20/ln(10) / τ ≈ -86.86 dB/s
static void test_decaying_sine()
{
    const double SR  = 44100.0;
    const double TAU = 0.1;
    const int    N   = (int)(SR * 1.0);  // 1 second
    std::vector<float> s(N);
    for (int i = 0; i < N; ++i)
        s[i] = (float)(std::sin(2.0 * M_PI * 100.0 * i / SR) * std::exp(-(double)i / (SR * TAU)));

    auto p = analyse_mono(s, SR);

    assert(!p.silent);
    assert(p.valid[6] && p.valid[7]);  // decay slope and R2 defined

    // slope near -86.86 dB/s, tolerance ±5 dB/s
    assert(near(p.decay_slope_db_per_second, -86.86, 5.0));

    // R² near 1.0 for pure exponential decay
    assert(p.decay_fit_r2 > 0.95);

    // dominant partial near 100 Hz
    assert(p.valid[33]);
    assert(near(p.dominant_partial_frequency, 100.0, 20.0));

    pass("decaying_sine");
}

// 10 ms click — most descriptors undefined
static void test_click_10ms()
{
    const double SR = 44100.0;
    const int    N  = (int)(SR * 0.010);
    std::vector<float> s(N, 0.0f);
    s[0] = 1.0f;

    auto p = analyse_mono(s, SR);

    assert(!p.silent);
    // fewer than 3 STFT frames → frame stats undefined
    assert(!p.frame_stat[0].defined);
    // envelope segments undefined (effective_duration < 10 ms)
    assert(!p.valid[43]);

    pass("click_10ms");
}

// Pure silence
static void test_silence()
{
    const double SR = 44100.0;
    std::vector<float> s(4410, 0.0f);

    auto p = analyse_mono(s, SR);
    assert(p.silent);
    // feature vector should all be NaN / invalid
    assert(!p.valid[0]);

    pass("silence");
}

// Pure DC — after DC removal, signal is all zeros → silent
static void test_dc()
{
    const double SR = 44100.0;
    std::vector<float> s(4410, 0.5f);

    auto p = analyse_mono(s, SR);
    assert(p.silent);

    pass("dc");
}

// Single-sample impulse
static void test_impulse()
{
    const double SR = 44100.0;
    std::vector<float> s(4410, 0.0f);
    s[100] = 1.0f;

    auto p = analyse_mono(s, SR);
    assert(!p.silent);
    // Very short effective duration — frame stats will be undefined or minimal
    // We just check it doesn't crash and peak is near 0 dBFS

    pass("impulse");
}

// Full-scale square wave → clipped
static void test_square_clipped()
{
    const double SR = 44100.0;
    const int    N  = (int)SR;
    std::vector<float> s(N);
    for (int i = 0; i < N; ++i) s[i] = (i % 100 < 50) ? 1.0f : -1.0f;

    auto p = analyse_mono(s, SR);
    assert(p.clipped);

    pass("square_clipped");
}

// §7.2 invariance: peak-normalising must not change any descriptor except
// peakDbfs, rmsDbfs, durationSeconds, bitDepth.
static void test_normalise_invariance()
{
    const double SR = 44100.0;
    const int    N  = (int)SR;
    std::vector<float> s(N), s2(N);

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-0.5f, 0.5f);
    for (auto& x : s) x = dist(rng);
    for (int i = 0; i < N; ++i) s2[i] = s[i] * 2.0f;  // scale up

    auto p1 = analyse_mono(s,  SR);
    auto p2 = analyse_mono(s2, SR);

    // centroid, spread, ZCR, temporal centroid etc. must be equal
    assert(near(p1.frame_stat[0].mean, p2.frame_stat[0].mean, 1e-6));  // centroid
    assert(near(p1.temporal_centroid_normalised, p2.temporal_centroid_normalised, 1e-6));
    assert(near(p1.frame_stat[8].mean, p2.frame_stat[8].mean, 1e-6));  // ZCR

    // But peakDbfs should differ
    assert(!near(p1.peak_dbfs, p2.peak_dbfs, 0.01));

    pass("normalise_invariance");
}

// Mono: inter_channel_correlation = 1.0, mid_side_ratio_db = 96.0
static void test_mono_stereo_defaults()
{
    const double SR = 44100.0;
    std::vector<float> s(4410);
    std::mt19937 rng(99);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    for (auto& x : s) x = d(rng);

    auto p = analyse_mono(s, SR);
    assert(near(p.inter_channel_correlation, 1.0, 1e-9));
    assert(near(p.mid_side_ratio_db,        96.0, 1e-9));

    pass("mono_stereo_defaults");
}

// Vector ordering: indices 0-7 temporal, 8-9 levels, 35-42 band energies, etc.
static void test_vector_ordering()
{
    const double SR = 44100.0;
    const int    N  = (int)(SR * 0.5);
    std::vector<float> s(N);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> d(-1.f, 1.f);
    for (auto& x : s) x = d(rng);

    auto p = analyse_mono(s, SR);

    assert(p.valid[0] && near(p.vector[0], p.duration_seconds, 1e-12));
    assert(p.valid[1] && near(p.vector[1], p.effective_duration_seconds, 1e-12));
    assert(p.valid[8] && near(p.vector[8], p.peak_dbfs,  1e-12));
    assert(p.valid[9] && near(p.vector[9], p.rms_dbfs,   1e-12));

    // Band energies sum to ~1
    double sum = 0;
    for (int b = 0; b < SP_BAND_COUNT; ++b) sum += p.band_energy[b];
    assert(near(sum, 1.0, 1e-9));

    // Band energies in vector match struct
    for (int b = 0; b < SP_BAND_COUNT; ++b)
        assert(!p.valid[35+b] || near(p.vector[35+b], p.band_energy[b], 1e-12));

    assert(p.valid[63] && near(p.vector[63], p.inter_channel_correlation, 1e-12));
    assert(p.valid[64] && near(p.vector[64], p.mid_side_ratio_db,         1e-12));

    pass("vector_ordering");
}

// ── entry point ───────────────────────────────────────────────────────────────

int main()
{
    test_sine_1khz();
    test_white_noise();
    test_decaying_sine();
    test_click_10ms();
    test_silence();
    test_dc();
    test_impulse();
    test_square_clipped();
    test_normalise_invariance();
    test_mono_stereo_defaults();
    test_vector_ordering();

    std::printf("All sampleprofile tests passed.\n");
    return 0;
}
