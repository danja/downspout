#include "sinevox_core_types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace downspout::sinevox {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Target analysis rate. 16 kHz gives an 8 kHz band, enough for five or six
// speech formants, and keeps the LPC order (and the root finder) small.
constexpr double kAnalysisRate = 16000.0;
constexpr double kHopSeconds = 0.005;
constexpr double kWindowSeconds = 0.025;
constexpr double kPreEmphasis = 0.97;
constexpr double kMinFormantHz = 90.0;
constexpr float kMinQ = 0.7f;
constexpr float kMaxQ = 60.0f;
constexpr float kMaxVoxScale = 200.0f;
constexpr double kF0WindowSeconds = 0.04;
constexpr double kF0MinHz = 60.0;
constexpr double kF0MaxHz = 500.0;
constexpr float kF0Voicing = 0.5f;      // normalised autocorrelation needed to call a frame voiced
constexpr float kF0Octave = 0.88f;      // a shorter lag this close to the best peak wins (no octave drops)
constexpr float kIdleAmp = 1.0e-5f;  // -100 dB: below this a released formant is switched off

constexpr std::array<float, kMaxFormants> kNominalHz = {
    500.0f, 1500.0f, 2500.0f, 3500.0f, 4500.0f, 5500.0f, 6500.0f, 7500.0f,
};

template <typename T>
[[nodiscard]] T clampT(const T v, const T lo, const T hi) noexcept
{
    return std::max(lo, std::min(v, hi));
}

[[nodiscard]] float onePole(const float ms, const double sampleRate) noexcept
{
    const double samples = std::max(1.0, static_cast<double>(ms) * 0.001 * sampleRate);
    return static_cast<float>(1.0 - std::exp(-1.0 / samples));
}

[[nodiscard]] float dbToLinear(const float db) noexcept
{
    return std::pow(10.0f, db / 20.0f);
}

[[nodiscard]] float linearToDb(const float lin) noexcept
{
    return 20.0f * std::log10(std::max(lin, 1.0e-6f));
}

// ── Parameter table ────────────────────────────────────────────────────────

[[nodiscard]] std::array<float, kInputParameterCount> toArray(const Parameters& p) noexcept
{
    return {p.mode, p.formants, p.bypass, p.gateDb, p.bwLimit, p.glideMs, p.attackMs,
            p.releaseMs, p.shift, p.qScale, p.tilt, p.levelDb, p.dry, p.carrierNorm,
            p.spread, p.f0Mix, p.noteShift, p.midiOut, p.outChannel, p.ccChannel, p.ccShift,
            p.ccQ, p.ccFormants, p.ccTilt, p.ccMode};
}

[[nodiscard]] Parameters fromArray(const std::array<float, kInputParameterCount>& a) noexcept
{
    Parameters p;
    p.mode = a[0]; p.formants = a[1]; p.bypass = a[2]; p.gateDb = a[3];
    p.bwLimit = a[4]; p.glideMs = a[5]; p.attackMs = a[6]; p.releaseMs = a[7];
    p.shift = a[8]; p.qScale = a[9]; p.tilt = a[10]; p.levelDb = a[11];
    p.dry = a[12]; p.carrierNorm = a[13]; p.spread = a[14]; p.f0Mix = a[15];
    p.noteShift = a[16]; p.midiOut = a[17]; p.outChannel = a[18]; p.ccChannel = a[19];
    p.ccShift = a[20]; p.ccQ = a[21]; p.ccFormants = a[22]; p.ccTilt = a[23]; p.ccMode = a[24];
    return p;
}

// ── Analyser ───────────────────────────────────────────────────────────────

void setLowpass(Biquad& bq, const double fc, const double q, const double sr) noexcept
{
    const double w0 = kTwoPi * fc / sr;
    const double alpha = std::sin(w0) / (2.0 * q);
    const double c = std::cos(w0);
    const double a0 = 1.0 + alpha;
    bq.b0 = static_cast<float>((1.0 - c) * 0.5 / a0);
    bq.b1 = static_cast<float>((1.0 - c) / a0);
    bq.b2 = bq.b0;
    bq.a1 = static_cast<float>(-2.0 * c / a0);
    bq.a2 = static_cast<float>((1.0 - alpha) / a0);
    bq.z1 = bq.z2 = 0.0f;
}

[[nodiscard]] float runBiquad(Biquad& bq, const float x) noexcept
{
    const float y = bq.b0 * x + bq.z1;
    bq.z1 = bq.b1 * x - bq.a1 * y + bq.z2;
    bq.z2 = bq.b2 * x - bq.a2 * y;
    return y;
}

void prepareAnalyzer(Analyzer& an, const double sampleRate) noexcept
{
    an = Analyzer {};
    an.sampleRate = sampleRate;
    an.decim = std::max(1, static_cast<int>(std::lround(sampleRate / kAnalysisRate)));
    an.rate = sampleRate / an.decim;
    an.hop = std::max(16, static_cast<int>(std::lround(kHopSeconds * an.rate)));
    an.window = clampT(static_cast<int>(std::lround(kWindowSeconds * an.rate)), 64,
                       kMaxAnalysisWindow);
    if (an.decim > 1) {
        // Fourth-order Butterworth, two cascaded sections.
        setLowpass(an.lowpass[0], 0.4 * an.rate, 0.5412, sampleRate);
        setLowpass(an.lowpass[1], 0.4 * an.rate, 1.3066, sampleRate);
    }
}

struct Candidate {
    float freq;
    float bandwidth;
    float amp;
};

struct Frame {
    std::array<Candidate, kMaxFormants + 1> cands {};
    int count = 0;
    bool silent = true;
};

using Complex = std::complex<double>;

// Durand-Kerner on z^P + a1 z^(P-1) + ... + aP. Returns true on convergence.
bool findRoots(const double* a, const int order, Complex* z, const bool warm) noexcept
{
    auto seed = [&]() {
        const Complex start(0.4, 0.9);
        Complex v(1.0, 0.0);
        for (int i = 0; i < order; ++i) {
            z[i] = v * 0.9;
            v *= start;
        }
    };
    if (!warm)
        seed();

    for (int pass = 0; pass < 2; ++pass) {
        for (int iter = 0; iter < 120; ++iter) {
            double maxDelta = 0.0;
            for (int i = 0; i < order; ++i) {
                Complex num(1.0, 0.0);
                for (int k = 1; k <= order; ++k)
                    num = num * z[i] + a[k];
                Complex den(1.0, 0.0);
                for (int j = 0; j < order; ++j) {
                    if (j != i)
                        den *= (z[i] - z[j]);
                }
                if (std::abs(den) < 1.0e-30)
                    den = Complex(1.0e-30, 0.0);
                const Complex d = num / den;
                z[i] -= d;
                maxDelta = std::max(maxDelta, std::abs(d));
            }
            if (maxDelta < 1.0e-10)
                return true;
        }
        if (pass == 0)
            seed();  // the warm start did not settle: start cold
    }
    return false;
}

// Analyse the most recent window and fill `frame`. Returns false if the roots
// did not converge (the caller keeps the previous tracks).
bool analyseFrame(Analyzer& an, const Parameters& params, Frame& frame) noexcept
{
    frame.count = 0;
    frame.silent = true;

    const int W = an.window;
    const int order = 2 * static_cast<int>(std::lround(params.formants)) + 2;

    // Level of the (band-limited) input over the window.
    double ms = 0.0;
    std::array<double, kMaxAnalysisWindow> xw {};
    double sumW2 = 0.0;
    for (int n = 0; n < W; ++n) {
        const int idx = (an.pos - W + n + kMaxAnalysisWindow) % kMaxAnalysisWindow;
        const double r = an.raw[static_cast<std::size_t>(idx)];
        ms += r * r;
        const double w = 0.5 - 0.5 * std::cos(kTwoPi * (n + 0.5) / W);
        xw[static_cast<std::size_t>(n)] = an.emph[static_cast<std::size_t>(idx)] * w;
        sumW2 += w * w;
    }
    ms /= W;
    an.levelDb = static_cast<float>(10.0 * std::log10(ms + 1.0e-10));
    if (an.levelDb < params.gateDb)
        return true;

    // Autocorrelation with a Gaussian lag window to keep poles off the unit circle.
    std::array<double, kMaxLpcOrder + 1> r {};
    for (int k = 0; k <= order; ++k) {
        double acc = 0.0;
        for (int n = 0; n + k < W; ++n)
            acc += xw[static_cast<std::size_t>(n)] * xw[static_cast<std::size_t>(n + k)];
        const double lag = kTwoPi * 40.0 * k / an.rate;
        r[static_cast<std::size_t>(k)] = acc * std::exp(-0.5 * lag * lag);
    }
    if (r[0] < 1.0e-12)
        return true;
    r[0] *= 1.0 + 1.0e-6;

    // Levinson-Durbin.
    std::array<double, kMaxLpcOrder + 1> a {};
    std::array<double, kMaxLpcOrder + 1> tmp {};
    a[0] = 1.0;
    double err = r[0];
    for (int i = 1; i <= order; ++i) {
        double acc = r[static_cast<std::size_t>(i)];
        for (int j = 1; j < i; ++j)
            acc += a[static_cast<std::size_t>(j)] * r[static_cast<std::size_t>(i - j)];
        const double k = -acc / err;
        tmp = a;
        for (int j = 1; j < i; ++j)
            a[static_cast<std::size_t>(j)] = tmp[static_cast<std::size_t>(j)]
                + k * tmp[static_cast<std::size_t>(i - j)];
        a[static_cast<std::size_t>(i)] = k;
        err *= (1.0 - k * k);
        if (err <= 0.0)
            return true;
    }
    const double sigma2 = err / sumW2;

    const bool warm = an.haveRoots && an.rootOrder == order;
    if (!findRoots(a.data(), order, an.roots.data(), warm)) {
        an.haveRoots = false;
        return false;
    }
    an.haveRoots = true;
    an.rootOrder = order;

    frame.silent = false;
    for (int i = 0; i < order && frame.count < kMaxFormants + 1; ++i) {
        const Complex z = an.roots[static_cast<std::size_t>(i)];
        if (z.imag() <= 1.0e-6)
            continue;
        const double mag = std::abs(z);
        if (mag >= 1.0 || mag < 0.05)
            continue;
        const double omega = std::arg(z);
        const double freq = omega * an.rate / kTwoPi;
        const double bw = -std::log(mag) * an.rate / kPi;
        if (freq < kMinFormantHz || freq > 0.46 * an.rate || bw > params.bwLimit)
            continue;

        // Envelope magnitude at the peak, with the pre-emphasis undone.
        Complex A(1.0, 0.0);
        for (int k = 1; k <= order; ++k)
            A += a[static_cast<std::size_t>(k)] * std::polar(1.0, -k * omega);
        const double env = std::sqrt(sigma2) / std::max(std::abs(A), 1.0e-9);
        const double emph = std::abs(Complex(1.0, 0.0) - kPreEmphasis * std::polar(1.0, -omega));
        const double gamma = kPi * bw / an.rate;
        const double amp = std::sqrt(2.0 * gamma) * env / std::max(emph, 1.0e-3);

        frame.cands[static_cast<std::size_t>(frame.count++)] = {
            static_cast<float>(freq), static_cast<float>(bw), static_cast<float>(amp)};
    }
    return true;
}

// Fundamental by normalised autocorrelation of the (unemphasised) input over
// ~40 ms. Takes the shortest lag whose peak is close to the best one, so that
// an octave-down peak does not win, and refines it by parabolic interpolation.
void analyseF0(Analyzer& an, const Parameters& params) noexcept
{
    an.f0Hz = 0.0f;
    an.f0Level = 0.0f;
    if (an.levelDb < params.gateDb)
        return;

    const int W = clampT(static_cast<int>(std::lround(kF0WindowSeconds * an.rate)), 64,
                         std::min(an.filled, kMaxAnalysisWindow));
    const int minLag = std::max(2, static_cast<int>(an.rate / kF0MaxHz) - 1);
    const int maxLag = static_cast<int>(an.rate / kF0MinHz) + 1;
    if (maxLag * 2 > W)
        return;

    std::array<float, kMaxAnalysisWindow> x {};
    double mean = 0.0;
    for (int n = 0; n < W; ++n) {
        const int idx = (an.pos - W + n + kMaxAnalysisWindow) % kMaxAnalysisWindow;
        x[static_cast<std::size_t>(n)] = an.raw[static_cast<std::size_t>(idx)];
        mean += x[static_cast<std::size_t>(n)];
    }
    mean /= W;
    // Prefix sums of squares for the two energy terms of each lag.
    std::array<double, kMaxAnalysisWindow + 1> cum {};
    for (int n = 0; n < W; ++n) {
        const double v = x[static_cast<std::size_t>(n)] - mean;
        x[static_cast<std::size_t>(n)] = static_cast<float>(v);
        cum[static_cast<std::size_t>(n + 1)] = cum[static_cast<std::size_t>(n)] + v * v;
    }
    if (cum[static_cast<std::size_t>(W)] < 1.0e-12)
        return;

    std::array<float, kMaxAnalysisWindow> nac {};
    float best = 0.0f;
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag) {
        double num = 0.0;
        for (int n = 0; n + lag < W; ++n)
            num += static_cast<double>(x[static_cast<std::size_t>(n)]) * x[static_cast<std::size_t>(n + lag)];
        const double e1 = cum[static_cast<std::size_t>(W - lag)];
        const double e2 = cum[static_cast<std::size_t>(W)] - cum[static_cast<std::size_t>(lag)];
        const float v = static_cast<float>(num / std::sqrt(std::max(e1 * e2, 1.0e-20)));
        nac[static_cast<std::size_t>(lag)] = v;
        if (lag >= minLag && lag <= maxLag)
            best = std::max(best, v);
    }
    if (best < kF0Voicing)
        return;

    for (int lag = minLag; lag <= maxLag; ++lag) {
        const float v = nac[static_cast<std::size_t>(lag)];
        if (v >= kF0Octave * best && v >= nac[static_cast<std::size_t>(lag - 1)]
            && v >= nac[static_cast<std::size_t>(lag + 1)]) {
            const float a = nac[static_cast<std::size_t>(lag - 1)];
            const float c = nac[static_cast<std::size_t>(lag + 1)];
            const float denom = a - 2.0f * v + c;
            const float frac = std::fabs(denom) > 1.0e-9f ? clampT(0.5f * (a - c) / denom, -0.5f, 0.5f) : 0.0f;
            an.f0Hz = static_cast<float>(an.rate / (static_cast<double>(lag) + frac));
            an.f0Level = std::sqrt(static_cast<float>(cum[static_cast<std::size_t>(W)] / W));
            return;
        }
    }
}

// Assigns candidates to persistent slots so a formant keeps its identity, and
// therefore its oscillator and filter, from frame to frame.
void trackFormants(EngineState& st, const Parameters& params, Frame& frame) noexcept
{
    const int n = static_cast<int>(std::lround(params.formants));
    std::array<bool, kMaxFormants> claimed {};

    if (!frame.silent) {
        std::array<int, kMaxFormants + 1> order {};
        for (int i = 0; i < frame.count; ++i)
            order[static_cast<std::size_t>(i)] = i;
        std::sort(order.begin(), order.begin() + frame.count, [&](const int x, const int y) {
            return frame.cands[static_cast<std::size_t>(x)].amp
                 > frame.cands[static_cast<std::size_t>(y)].amp;
        });

        for (int oi = 0; oi < frame.count; ++oi) {
            const Candidate& c = frame.cands[static_cast<std::size_t>(order[static_cast<std::size_t>(oi)])];
            int best = -1;
            float bestDist = 1.0e9f;
            for (int s = 0; s < n; ++s) {
                if (claimed[static_cast<std::size_t>(s)])
                    continue;
                const Formant& f = st.formants[static_cast<std::size_t>(s)];
                const bool dead = f.tgtAmp <= 0.0f;
                const float ref = dead ? kNominalHz[static_cast<std::size_t>(s)] : f.tgtFreq;
                const float dist = std::fabs(std::log2(c.freq / ref));
                if ((dist < 0.75f || dead) && dist < bestDist) {
                    bestDist = dist;
                    best = s;
                }
            }
            if (best < 0)
                continue;
            Formant& f = st.formants[static_cast<std::size_t>(best)];
            claimed[static_cast<std::size_t>(best)] = true;
            // A slot that was inaudible has no pitch to glide from.
            if (f.smAmp < 1.0e-4f) {
                f.smLogFreq = std::log2(c.freq);
                f.smQ = clampT(c.freq / std::max(c.bandwidth, 1.0f), kMinQ, kMaxQ);
            }
            f.tgtFreq = c.freq;
            f.tgtQ = clampT(c.freq / std::max(c.bandwidth, 1.0f), kMinQ, kMaxQ);
            f.tgtAmp = c.amp;
        }
    }

    for (int s = 0; s < kMaxFormants; ++s) {
        if (!claimed[static_cast<std::size_t>(s)])
            st.formants[static_cast<std::size_t>(s)].tgtAmp = 0.0f;
    }
}

void pushAnalysis(EngineState& st, const Parameters& params, const float mono) noexcept
{
    Analyzer& an = st.analyzer;
    float y = mono;
    if (an.decim > 1) {
        y = runBiquad(an.lowpass[0], y);
        y = runBiquad(an.lowpass[1], y);
    }
    if (++an.decimCount < an.decim)
        return;
    an.decimCount = 0;

    const auto at = static_cast<std::size_t>(an.pos);
    an.raw[at] = y;
    an.emph[at] = y - static_cast<float>(kPreEmphasis) * an.prevRaw;
    an.prevRaw = y;
    an.pos = (an.pos + 1) % kMaxAnalysisWindow;
    an.filled = std::min(an.filled + 1, kMaxAnalysisWindow);

    if (++an.sinceHop < an.hop || an.filled < an.window)
        return;
    an.sinceHop = 0;

    Frame frame;
    if (analyseFrame(an, params, frame))
        trackFormants(st, params, frame);
    analyseF0(an, params);
}

// ── Synthesis ──────────────────────────────────────────────────────────────

void flushDenormals(Formant& f) noexcept
{
    for (int c = 0; c < 2; ++c) {
        if (std::fabs(f.ic1[c]) < 1.0e-20f) f.ic1[c] = 0.0f;
        if (std::fabs(f.ic2[c]) < 1.0e-20f) f.ic2[c] = 0.0f;
    }
}

}  // namespace

// ── Public API ─────────────────────────────────────────────────────────────

Parameters clampParameters(const Parameters& p) noexcept
{
    auto a = toArray(p);
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        const ParamSpec& spec = kParameterSpecs[i];
        float v = a[i];
        if (!std::isfinite(v))
            v = spec.defaultValue;
        v = clampT(v, spec.minimum, spec.maximum);
        if (spec.integer)
            v = std::round(v);
        a[i] = v;
    }
    return fromArray(a);
}

void activate(EngineState& state) noexcept
{
    const double sr = state.sampleRate;
    state = EngineState {};
    state.sampleRate = sr;
    for (int i = 0; i < kMaxFormants; ++i) {
        Formant& f = state.formants[static_cast<std::size_t>(i)];
        f.smLogFreq = std::log2(kNominalHz[static_cast<std::size_t>(i)]);
        f.tgtFreq = kNominalHz[static_cast<std::size_t>(i)];
        state.soundingNote[static_cast<std::size_t>(i)] = -1;
        state.sentFreqCc[static_cast<std::size_t>(i)] = -1;
        state.sentAmpCc[static_cast<std::size_t>(i)] = -1;
    }
    if (sr > 0.0)
        prepareAnalyzer(state.analyzer, sr);
}

float effectiveFrequency(const float hz, const float shiftSemitones, const double sampleRate) noexcept
{
    const float shifted = hz * std::exp2(shiftSemitones / 12.0f);
    return clampT(shifted, 20.0f, static_cast<float>(0.45 * sampleRate));
}

bool handleMidi(EngineState& state, Parameters& p, const std::uint8_t* data,
                const std::uint32_t size) noexcept
{
    if (data == nullptr || size < 2)
        return false;
    const std::uint8_t status = data[0] & 0xf0u;
    if (static_cast<int>(data[0] & 0x0fu) + 1 != static_cast<int>(p.ccChannel))
        return false;

    if (status == 0x90 || status == 0x80) {
        const std::uint8_t note = data[1] & 0x7fu;
        const bool on = status == 0x90 && size >= 3 && data[2] > 0;
        // Held notes form a stack; the most recent one wins.
        for (int i = 0; i < state.heldCount; ++i) {
            if (state.heldNotes[static_cast<std::size_t>(i)] == note) {
                for (int j = i; j + 1 < state.heldCount; ++j)
                    state.heldNotes[static_cast<std::size_t>(j)] = state.heldNotes[static_cast<std::size_t>(j + 1)];
                --state.heldCount;
                break;
            }
        }
        if (on) {
            if (state.heldCount == static_cast<int>(state.heldNotes.size())) {
                for (int j = 0; j + 1 < state.heldCount; ++j)
                    state.heldNotes[static_cast<std::size_t>(j)] = state.heldNotes[static_cast<std::size_t>(j + 1)];
                --state.heldCount;
            }
            state.heldNotes[static_cast<std::size_t>(state.heldCount++)] = note;
        }
        return false;
    }

    if (status != 0xb0 || size < 3)
        return false;

    const int cc = data[1] & 0x7f;
    const float t = static_cast<float>(data[2] & 0x7f) / 127.0f;
    bool changed = false;
    auto hit = [&](const float assigned) { return static_cast<int>(assigned) > 0
                                                  && static_cast<int>(assigned) == cc; };

    if (hit(p.ccShift)) {
        p.shift = -12.0f + t * 24.0f;
        changed = true;
    }
    if (hit(p.ccQ)) {
        p.qScale = 0.25f * std::pow(16.0f, t);  // log across 0.25 - 4
        changed = true;
    }
    if (hit(p.ccFormants)) {
        p.formants = std::round(1.0f + t * 7.0f);
        changed = true;
    }
    if (hit(p.ccTilt)) {
        p.tilt = -12.0f + t * 24.0f;
        changed = true;
    }
    if (hit(p.ccMode)) {
        p.mode = std::min(2.0f, std::floor(t * 3.0f));
        changed = true;
    }
    if (changed)
        p = clampParameters(p);
    return changed;
}

void processBlock(EngineState& state,
                  const Parameters& params,
                  const std::uint32_t frames,
                  const double sampleRate,
                  const float* const* inputs,
                  float* const* outputs) noexcept
{
    if (state.sampleRate != sampleRate || state.analyzer.sampleRate != sampleRate) {
        state.sampleRate = sampleRate;
        prepareAnalyzer(state.analyzer, sampleRate);
    }

    const int n = static_cast<int>(std::lround(params.formants));
    const int mode = static_cast<int>(std::lround(params.mode));
    const bool useVocoder = mode == static_cast<int>(ModeId::vocoder) || mode == static_cast<int>(ModeId::both);
    const bool useSine = mode == static_cast<int>(ModeId::sineWave) || mode == static_cast<int>(ModeId::both);

    const float aFreq = onePole(params.glideMs, sampleRate);
    const float aAttack = onePole(params.attackMs, sampleRate);
    const float aRelease = onePole(params.releaseMs, sampleRate);
    const float aShift = onePole(10.0f, sampleRate);
    const float aCarRise = onePole(5.0f, sampleRate);
    const float aCarFall = onePole(400.0f, sampleRate);

    const float level = dbToLinear(params.levelDb);
    const float f0Gain = params.f0Mix * 0.01f;
    const float dry = params.dry * 0.01f;
    const float norm = params.carrierNorm * 0.01f;
    const bool bypass = params.bypass >= 0.5f;

    const float noteOffset = (params.noteShift >= 0.5f && state.heldCount > 0)
        ? static_cast<float>(state.heldNotes[static_cast<std::size_t>(state.heldCount - 1)]) - 60.0f
        : 0.0f;
    const float shiftTarget = clampT(params.shift + noteOffset, -36.0f, 36.0f);

    const float* speechL = inputs[0];
    const float* speechR = inputs[1];
    const float* carL = inputs[2];
    const float* carR = inputs[3];

    for (std::uint32_t s = 0; s < frames; ++s) {
        const float inL = speechL[s];
        const float inR = speechR[s];
        const float cL = carL != nullptr ? carL[s] : 0.0f;
        const float cR = carR != nullptr ? carR[s] : 0.0f;

        pushAnalysis(state, params, 0.5f * (inL + inR));

        state.shiftSmooth += aShift * (shiftTarget - state.shiftSmooth);

        const float carPower = 0.5f * (cL * cL + cR * cR);
        state.carrierEnv += (carPower > state.carrierEnv ? aCarRise : aCarFall)
                          * (carPower - state.carrierEnv);

        // Sub-block coefficient refresh. The filters are state-variable, so
        // stepping their coefficients every few samples is click-free.
        if (state.coefCount-- <= 0) {
            state.coefCount = kCoefInterval - 1;
            const float carRms = std::max(std::sqrt(state.carrierEnv), 1.0e-4f);
            const float normDiv = std::pow(carRms, norm);
            for (int i = 0; i < kMaxFormants; ++i) {
                Formant& f = state.formants[static_cast<std::size_t>(i)];
                if (f.smAmp <= kIdleAmp && f.tgtAmp <= 0.0f)
                    continue;
                const float hz = effectiveFrequency(std::exp2(f.smLogFreq), state.shiftSmooth, sampleRate);
                const float q = clampT(f.smQ * params.qScale, 0.5f, 100.0f);
                const float tilt = std::pow(10.0f, params.tilt * std::log2(hz / 1000.0f) / 20.0f);

                const float g = std::tan(static_cast<float>(kPi) * hz / static_cast<float>(sampleRate));
                f.k = 1.0f / q;
                f.a1 = 1.0f / (1.0f + g * (g + f.k));
                f.a2 = g * f.a1;
                f.a3 = g * f.a2;

                const float bw = hz / q;
                const float bandGain = std::sqrt(2.0f * static_cast<float>(kPi) * bw
                                                 / static_cast<float>(sampleRate));
                f.voxScale = std::min(kMaxVoxScale, tilt / (normDiv * std::max(bandGain, 1.0e-4f)));
                f.sineScale = tilt;
                f.phaseInc = static_cast<double>(hz) / sampleRate;

                const float pan = n > 1 ? params.spread * 0.01f * (-1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(n - 1))
                                        : 0.0f;
                const float angle = (pan + 1.0f) * static_cast<float>(kPi) * 0.25f;
                f.panL = std::cos(angle);
                f.panR = std::sin(angle);
            }
        }

        float voxL = 0.0f, voxR = 0.0f, sineL = 0.0f, sineR = 0.0f;
        for (int i = 0; i < kMaxFormants; ++i) {
            Formant& f = state.formants[static_cast<std::size_t>(i)];
            if (f.smAmp <= kIdleAmp && f.tgtAmp <= 0.0f) {
                f.smAmp = 0.0f;
                f.ic1[0] = f.ic1[1] = f.ic2[0] = f.ic2[1] = 0.0f;
                continue;
            }

            f.smLogFreq += aFreq * (std::log2(std::max(f.tgtFreq, 20.0f)) - f.smLogFreq);
            f.smQ += aFreq * (f.tgtQ - f.smQ);
            f.smAmp += (f.tgtAmp > f.smAmp ? aAttack : aRelease) * (f.tgtAmp - f.smAmp);

            if (useVocoder) {
                const float amp = f.smAmp * f.voxScale;
                float v3 = cL - f.ic2[0];
                float v1 = f.a1 * f.ic1[0] + f.a2 * v3;
                float v2 = f.ic2[0] + f.a2 * f.ic1[0] + f.a3 * v3;
                f.ic1[0] = 2.0f * v1 - f.ic1[0];
                f.ic2[0] = 2.0f * v2 - f.ic2[0];
                voxL += f.k * v1 * amp;

                v3 = cR - f.ic2[1];
                v1 = f.a1 * f.ic1[1] + f.a2 * v3;
                v2 = f.ic2[1] + f.a2 * f.ic1[1] + f.a3 * v3;
                f.ic1[1] = 2.0f * v1 - f.ic1[1];
                f.ic2[1] = 2.0f * v2 - f.ic2[1];
                voxR += f.k * v1 * amp;
            }
            if (useSine) {
                f.phase += f.phaseInc;
                f.phase -= std::floor(f.phase);
                const float sample = static_cast<float>(std::sin(kTwoPi * f.phase))
                                   * f.smAmp * f.sineScale;
                sineL += sample * f.panL;
                sineR += sample * f.panR;
            }
        }

        // Fundamental: a sine at the tracked pitch, level following the voiced input.
        float f0Sample = 0.0f;
        {
            const Analyzer& an = state.analyzer;
            const float f0Target = an.f0Hz > 0.0f ? an.f0Level : 0.0f;
            if (an.f0Hz > 0.0f) {
                const float lg = std::log2(an.f0Hz);
                if (state.f0SmAmp < 1.0e-4f)
                    state.f0SmLogFreq = lg;
                else
                    state.f0SmLogFreq += aFreq * (lg - state.f0SmLogFreq);
            }
            state.f0SmAmp += (f0Target > state.f0SmAmp ? aAttack : aRelease) * (f0Target - state.f0SmAmp);
            if (f0Target <= 0.0f && state.f0SmAmp <= kIdleAmp)
                state.f0SmAmp = 0.0f;
            if (state.f0SmAmp > 0.0f && f0Gain > 0.0f) {
                state.f0Phase += std::exp2(static_cast<double>(state.f0SmLogFreq)) / sampleRate;
                state.f0Phase -= std::floor(state.f0Phase);
                f0Sample = static_cast<float>(std::sin(kTwoPi * state.f0Phase)) * state.f0SmAmp * f0Gain;
            }
        }

        if (bypass) {
            outputs[0][s] = inL;
            outputs[1][s] = inR;
        } else {
            outputs[0][s] = dry * inL + level * (voxL + sineL + f0Sample);
            outputs[1][s] = dry * inR + level * (voxR + sineR + f0Sample);
        }
    }

    for (Formant& f : state.formants)
        flushDenormals(f);

    // Publish what the tracker hears, pre-shift, so the panel shows the speaker
    // rather than the transposition.
    state.outLevelDb = std::max(state.analyzer.levelDb, -90.0f);
    state.outF0 = state.f0SmAmp > 1.0e-3f ? std::exp2(state.f0SmLogFreq) : 0.0f;
    state.outActive = 0;
    for (int i = 0; i < kMaxFormants; ++i) {
        const Formant& f = state.formants[static_cast<std::size_t>(i)];
        const auto idx = static_cast<std::size_t>(i);
        const float db = std::max(linearToDb(f.smAmp), -90.0f);
        state.outFreq[idx] = f.smAmp > 1.0e-4f ? std::exp2(f.smLogFreq) : 0.0f;
        state.outAmpDb[idx] = db;
        state.outQ[idx] = f.smAmp > 1.0e-4f ? f.smQ : 0.0f;
        if (db > kOutAmpMinDb)
            ++state.outActive;
    }
}

// ── MIDI output ────────────────────────────────────────────────────────────

void collectMidiOut(EngineState& st, const Parameters& params,
                    const std::uint32_t frames, MidiOutBlock& out) noexcept
{
    out.count = 0;
    auto push = [&](const std::uint8_t status, const int channel, const int d1, const int d2) {
        if (out.count >= kMaxMidiOut)
            return;
        MidiOutEvent& ev = out.events[static_cast<std::size_t>(out.count++)];
        ev.frame = 0;
        ev.data[0] = static_cast<std::uint8_t>(status | ((channel - 1) & 0x0f));
        ev.data[1] = static_cast<std::uint8_t>(d1 & 0x7f);
        ev.data[2] = static_cast<std::uint8_t>(d2 & 0x7f);
    };

    const int mode = static_cast<int>(std::lround(params.midiOut));
    const int channel = static_cast<int>(std::lround(params.outChannel));
    const int n = static_cast<int>(std::lround(params.formants));

    // Leaving notes mode, or moving to another channel, must not strand notes.
    if ((st.lastMidiOut == static_cast<int>(MidiOutId::notes) && mode != st.lastMidiOut)
        || (mode == static_cast<int>(MidiOutId::notes) && st.lastOutChannel != channel)) {
        const int oldChannel = st.lastOutChannel > 0 ? st.lastOutChannel : channel;
        for (int i = 0; i < kMaxFormants; ++i) {
            int& note = st.soundingNote[static_cast<std::size_t>(i)];
            if (note >= 0) {
                push(0x80, oldChannel, note, 0);
                note = -1;
            }
        }
    }
    if (mode != st.lastMidiOut) {
        // Re-send every controller after a mode change.
        st.sentFreqCc.fill(-1);
        st.sentAmpCc.fill(-1);
    }
    st.lastMidiOut = mode;
    st.lastOutChannel = channel;

    st.samplesSinceMidi += frames;
    if (mode == static_cast<int>(MidiOutId::off))
        return;
    if (static_cast<double>(st.samplesSinceMidi) < 0.01 * st.sampleRate)
        return;
    const std::uint32_t elapsed = st.samplesSinceMidi;
    st.samplesSinceMidi = 0;

    const double logRange = std::log(kOutFreqMaxHz / kOutFreqMinHz);

    for (int i = 0; i < kMaxFormants; ++i) {
        const auto idx = static_cast<std::size_t>(i);
        const float db = st.outAmpDb[idx];
        const bool sounding = i < n && db > kNoteOnDb;
        const float hz = effectiveFrequency(st.outFreq[idx], st.shiftSmooth, st.sampleRate);

        if (mode == static_cast<int>(MidiOutId::controllers)) {
            const int ampCc = (i < n && db > kOutAmpMinDb)
                ? clampT(static_cast<int>(std::lround(127.0f * (db - kOutAmpMinDb) / -kOutAmpMinDb)), 0, 127)
                : 0;
            if (ampCc != st.sentAmpCc[idx]) {
                push(0xb0, channel, kOutAmpCcBase + i, ampCc);
                st.sentAmpCc[idx] = ampCc;
            }
            if (ampCc > 0 && st.outFreq[idx] > 0.0f) {
                const int freqCc = clampT(static_cast<int>(std::lround(
                    127.0 * std::log(std::max(hz, kOutFreqMinHz) / kOutFreqMinHz) / logRange)), 0, 127);
                if (freqCc != st.sentFreqCc[idx]) {
                    push(0xb0, channel, kOutFreqCcBase + i, freqCc);
                    st.sentFreqCc[idx] = freqCc;
                }
            }
        } else {
            int& note = st.soundingNote[idx];
            std::uint32_t& hold = st.noteHold[idx];
            hold -= std::min(hold, elapsed);
            if (!sounding) {
                if (note >= 0) {
                    push(0x80, channel, note, 0);
                    note = -1;
                }
                continue;
            }
            const int want = clampT(static_cast<int>(std::lround(
                69.0 + 12.0 * std::log2(std::max(hz, 8.0f) / 440.0))), 0, 127);
            if (want == note)
                continue;
            // A formant wobbling across a semitone boundary should not machine-gun.
            if (note >= 0 && hold > 0)
                continue;
            if (note >= 0)
                push(0x80, channel, note, 0);
            const int vel = clampT(static_cast<int>(std::lround(1.0f + 126.0f * (db - kNoteOnDb) / -kNoteOnDb)), 1, 127);
            push(0x90, channel, want, vel);
            note = want;
            hold = static_cast<std::uint32_t>(0.05 * st.sampleRate);
        }
    }
}

// ── Serialization ──────────────────────────────────────────────────────────

std::string serializeParameters(const Parameters& raw)
{
    const auto a = toArray(clampParameters(raw));
    std::string text = "version=1\n";
    for (std::size_t i = 0; i < kInputParameterCount; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s=%.6g\n", kParameterSpecs[i].symbol,
                      static_cast<double>(a[i]));
        text += buf;
    }
    return text;
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    auto a = toArray(Parameters {});
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        const std::string_view line(text.data() + start, end - start);
        start = end + 1;
        if (line.empty())
            continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos)
            return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        if (key == "version")
            continue;
        const std::string value(line.substr(sep + 1));
        char* endp = nullptr;
        const float v = std::strtof(value.c_str(), &endp);
        if (endp == value.c_str())
            return std::nullopt;
        for (std::size_t i = 0; i < kInputParameterCount; ++i) {
            if (key == kParameterSpecs[i].symbol)
                a[i] = v;
        }
    }
    return clampParameters(fromArray(a));
}

}  // namespace downspout::sinevox
