#include "grist_core.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace downspout::grist {

namespace {

constexpr double kPi = 3.14159265358979323846;

constexpr const char* kDivisionNames[kDivisionCount] = {"1/64", "1/32T", "1/32", "1/16T", "1/16", "1/16D", "1/8T",
                                                         "1/8",  "1/8D",  "1/4T", "1/4",   "1/4D",  "1/2T", "1/2",
                                                         "1/2D", "1/1",   "2/1",  "4/1",   "8/1"};
constexpr double kDivisionQuarters[kDivisionCount] = {
    1.0 / 16.0, 1.0 / 12.0, 1.0 / 8.0, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5, 0.75, 2.0 / 3.0,
    1.0,        1.5,        4.0 / 3.0, 2.0,       3.0,  4.0,   8.0,       16.0, 32.0};

constexpr const char* kScaleNames[kScaleCount] = {"Free", "Semitones", "Octaves", "Fifths + oct", "Major", "Minor", "Pentatonic"};
constexpr const char* kDirectionNames[3] = {"Up", "Down", "Up & down"};
constexpr const char* kFilterNames[3] = {"Lowpass", "Highpass", "Bandpass"};

// Pitch classes each scale allows (bit n = n semitones above the input's pitch).
constexpr std::uint16_t mask(std::initializer_list<int> pcs)
{
    std::uint16_t m = 0;
    for (const int pc : pcs) m = static_cast<std::uint16_t>(m | (1u << pc));
    return m;
}
constexpr std::uint16_t kScaleMasks[kScaleCount] = {
    0xfff,                           // Free (unused: drawn continuously)
    0xfff,                           // Semitones
    mask({0}),                       // Octaves
    mask({0, 7}),                    // Fifths + octaves
    mask({0, 2, 4, 5, 7, 9, 11}),    // Major
    mask({0, 2, 3, 5, 7, 8, 10}),    // Minor
    mask({0, 2, 4, 7, 9}),           // Pentatonic
};

// Both curves average 0.5 over the grain.
struct Curves {
    static constexpr int kN = 512;
    std::array<float, kN + 1> hann {};
    std::array<float, kN + 1> perc {};
    Curves()
    {
        constexpr double peakAt = 0.08;
        double sum = 0.0;
        std::array<double, kN + 1> raw {};
        for (int i = 0; i <= kN; ++i) {
            const double p = static_cast<double>(i) / kN;
            hann[static_cast<std::size_t>(i)] = static_cast<float>(0.5 * (1.0 - std::cos(2.0 * kPi * p)));
            raw[static_cast<std::size_t>(i)] = p / peakAt * std::exp(1.0 - p / peakAt);
            if (i > 0 && i < kN) sum += raw[static_cast<std::size_t>(i)];
        }
        const double mean = (sum + 0.5 * (raw[0] + raw[kN])) / kN;
        for (int i = 0; i <= kN; ++i) perc[static_cast<std::size_t>(i)] = static_cast<float>(raw[static_cast<std::size_t>(i)] * 0.5 / mean);
    }
};

const Curves& curves()
{
    static const Curves c;
    return c;
}

float pv(const std::array<float, kParameterCount>& p, const std::uint32_t i)
{
    return downspout::generative::clampParam(p[i], kParameterSpecs[i]);
}

int iv(const std::array<float, kParameterCount>& p, const std::uint32_t i)
{
    return static_cast<int>(std::lround(pv(p, i)));
}

// Four-point Hermite read of a ring buffer at a fractional position (in absolute samples).
float readHermite(const std::vector<float>& buf, const double pos) noexcept
{
    const auto n = static_cast<std::int64_t>(buf.size());
    const double floored = std::floor(pos);
    const auto i = static_cast<std::int64_t>(floored);
    const auto f = static_cast<float>(pos - floored);
    const auto at = [&](const std::int64_t k) {
        std::int64_t m = k % n;
        if (m < 0) m += n;
        return buf[static_cast<std::size_t>(m)];
    };
    const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

float flush(const float v) noexcept { return std::fabs(v) < 1e-15f ? 0.0f : v; }

struct FilterCoefficients {
    float a1 = 0, a2 = 0, a3 = 0, k = 1;
};

FilterCoefficients coefficients(const double hz, const float width, const double sampleRate) noexcept
{
    FilterCoefficients c;
    const double fc = std::min(hz, 0.45 * sampleRate);
    const double g = std::tan(kPi * fc / sampleRate);
    const double q = 0.5 + 11.5 * (1.0 - static_cast<double>(width)) * (1.0 - static_cast<double>(width));
    c.k = static_cast<float>(1.0 / q);
    c.a1 = static_cast<float>(1.0 / (1.0 + g * (g + 1.0 / q)));
    c.a2 = static_cast<float>(g * c.a1);
    c.a3 = static_cast<float>(g * c.a2);
    return c;
}

float runFilter(Svf& s, const FilterCoefficients& c, const int mode, const float x) noexcept
{
    const float v3 = x - s.s2;
    const float v1 = c.a1 * s.s1 + c.a2 * v3;
    const float v2 = s.s2 + c.a2 * s.s1 + c.a3 * v3;
    s.s1 = flush(2.0f * v1 - s.s1);
    s.s2 = flush(2.0f * v2 - s.s2);
    if (mode == kHighpass) return x - c.k * v1 - v2;
    if (mode == kBandpass) return c.k * v1;
    return v2;
}

}  // namespace

float shapeAt(const float shape, const float p) noexcept
{
    const auto& c = curves();
    const float x = std::clamp(p, 0.0f, 1.0f) * static_cast<float>(Curves::kN);
    const int i = std::min(static_cast<int>(x), Curves::kN - 1);
    const float f = x - static_cast<float>(i);
    const auto lerp = [&](const std::array<float, Curves::kN + 1>& t) {
        return t[static_cast<std::size_t>(i)] + f * (t[static_cast<std::size_t>(i) + 1] - t[static_cast<std::size_t>(i)]);
    };
    const float s = std::clamp(shape, 0.0f, 1.0f);
    return (1.0f - s) * lerp(c.hann) + s * lerp(c.perc);
}

double divisionQuarters(const int division) noexcept { return kDivisionQuarters[std::clamp(division, 0, kDivisionCount - 1)]; }
const char* divisionName(const int division) noexcept { return kDivisionNames[std::clamp(division, 0, kDivisionCount - 1)]; }
const char* scaleName(const int scale) noexcept { return kScaleNames[std::clamp(scale, 0, kScaleCount - 1)]; }
const char* directionName(const int direction) noexcept { return kDirectionNames[std::clamp(direction, 0, 2)]; }
const char* filterModeName(const int mode) noexcept { return kFilterNames[std::clamp(mode, 0, 2)]; }

double windowMs(const std::array<float, kParameterCount>& p, const double bpm) noexcept
{
    double ms = pv(p, kWindow);
    if (iv(p, kWindowSync) != 0) ms = divisionQuarters(iv(p, kWindowDivision)) * 60000.0 / std::clamp(bpm, 1.0, 999.0);
    return std::clamp(ms, 1.0, static_cast<double>(kMaxWindowMs));
}

double delayMs(const std::array<float, kParameterCount>& p, const double bpm) noexcept
{
    double ms = pv(p, kDelay);
    if (iv(p, kDelaySync) != 0) ms = divisionQuarters(iv(p, kDelayDivision)) * 60000.0 / std::clamp(bpm, 1.0, 999.0);
    return std::clamp(ms, 1.0, static_cast<double>(kMaxDelayMs));
}

double filterHz(const float amount) noexcept { return 20.0 * std::pow(750.0, static_cast<double>(std::clamp(amount, 0.0f, 1.0f))); }

double latencyMs(const double window, const double blur, const double semitones) noexcept
{
    const double ratio = std::pow(2.0, semitones / 12.0);
    return blur * window * 0.5 + std::max(0.0, ratio - 1.0) * window;
}

bool scaleAllows(const int scale, const int semitones) noexcept
{
    const int pc = ((semitones % 12) + 12) % 12;
    return (kScaleMasks[std::clamp(scale, 0, kScaleCount - 1)] >> pc & 1u) != 0;
}

float drawOffset(Rng& rng, const int centre, const float range, const int direction, const int scale) noexcept
{
    if (range <= 0.0f) return 0.0f;
    const float lo = direction == kUp ? 0.0f : -range;
    const float hi = direction == kDown ? 0.0f : range;
    if (scale <= 0) return lo + (hi - lo) * rng.next();

    // Every allowed whole-semitone offset in range, plus "stay on PITCH".
    std::array<int, 64> candidates {};
    int count = 0;
    candidates[static_cast<std::size_t>(count++)] = 0;
    for (int o = static_cast<int>(std::ceil(lo)); o <= static_cast<int>(std::floor(hi)); ++o)
        if (o != 0 && scaleAllows(scale, centre + o) && count < static_cast<int>(candidates.size()))
            candidates[static_cast<std::size_t>(count++)] = o;
    const int pick = std::min(count - 1, static_cast<int>(rng.next() * static_cast<float>(count)));
    return static_cast<float>(candidates[static_cast<std::size_t>(pick)]);
}

std::vector<CloudGrain> cloudPreview(const float pitch, const float range, const int direction, const float variance, const int scale,
                                     const int count, const std::uint32_t seed)
{
    Rng rng;
    rng.seed(seed);
    std::vector<CloudGrain> out;
    const int centre = static_cast<int>(std::lround(pitch));
    for (int i = 0; i < count; ++i) {
        CloudGrain g;
        g.size = 1.0f - 0.75f * variance * rng.next();
        g.offset = drawOffset(rng, centre, range, direction, scale);
        out.push_back(g);
    }
    return out;
}

void prepare(State& s, const double sampleRate)
{
    s.sampleRate = sampleRate > 0.0 ? sampleRate : 48000.0;
    const auto history = static_cast<std::size_t>(std::ceil(kBufferSeconds * s.sampleRate)) + 16;
    const auto line = static_cast<std::size_t>(std::ceil((kMaxDelayMs / 1000.0 + 0.05) * s.sampleRate)) + 16;
    s.bufferL.assign(history, 0.0f);
    s.bufferR.assign(history, 0.0f);
    s.delayL.assign(line, 0.0f);
    s.delayR.assign(line, 0.0f);
    reset(s);
}

void reset(State& s) noexcept
{
    std::fill(s.bufferL.begin(), s.bufferL.end(), 0.0f);
    std::fill(s.bufferR.begin(), s.bufferR.end(), 0.0f);
    std::fill(s.delayL.begin(), s.delayL.end(), 0.0f);
    std::fill(s.delayR.begin(), s.delayR.end(), 0.0f);
    s.written = 0;
    s.delayWritten = 0;
    for (auto& g : s.grains) g = {};
    s.untilNextGrain = 0.0;
    s.seededWith = -1;
    s.filterL = {};
    s.filterR = {};
    s.mixSmooth = -1.0f;
    s.shapeValue = -1.0f;
    s.statusGrains = 0;
}

void process(State& s, const std::array<float, kParameterCount>& p, const std::uint32_t frames, const double sampleRate,
             const double bpm, const float* inL, const float* inR, float* outL, float* outR) noexcept
{
    if (s.bufferL.empty() || std::fabs(s.sampleRate - sampleRate) > 0.5) prepare(s, sampleRate);

    const int seed = iv(p, kSeed);
    if (seed != s.seededWith) {
        s.rng.seed(static_cast<std::uint32_t>(seed));
        s.seededWith = seed;
    }

    const float shape = pv(p, kShape);
    if (shape != s.shapeValue) {
        for (int i = 0; i <= Curves::kN; ++i) s.shapeTable[static_cast<std::size_t>(i)] = shapeAt(shape, static_cast<float>(i) / Curves::kN);
        s.shapeValue = shape;
    }

    const double sr = s.sampleRate;
    const double window = windowMs(p, bpm);
    const auto windowSamples = std::max<std::int64_t>(2, std::llround(window * sr / 1000.0));
    const int density = iv(p, kDensity);
    const double interval = static_cast<double>(windowSamples) / density;
    const float grainGain = 2.0f / static_cast<float>(density);
    const float semis = pv(p, kPitch);
    const int centre = static_cast<int>(std::lround(semis));
    const float blur = pv(p, kBlur);
    const float reverse = pv(p, kReverse);
    const float spread = pv(p, kSpread);
    const float range = pv(p, kRange);
    const int direction = iv(p, kDirection);
    const int scale = iv(p, kScale);
    const float variance = pv(p, kVariance);
    const float feedback = pv(p, kFeedback);
    const bool filterOn = iv(p, kFilterOn) != 0;
    const int filterMode = iv(p, kFilterMode);
    const FilterCoefficients filter = coefficients(filterHz(pv(p, kFilterFreq)), pv(p, kFilterWidth), sr);
    const double delaySamples = std::clamp(delayMs(p, bpm) * sr / 1000.0, 2.0, static_cast<double>(s.delayL.size()) - 8.0);
    const float mixTarget = pv(p, kMix);
    if (s.mixSmooth < 0.0f) s.mixSmooth = mixTarget;
    const auto history = static_cast<double>(s.bufferL.size());
    const auto lineSize = static_cast<std::int64_t>(s.delayL.size());

    for (std::uint32_t n = 0; n < frames; ++n) {
        const float dryL = inL[n];
        const float dryR = inR[n];

        // Feedback: the granular output of delaySamples ago, filtered, goes back into the history.
        float fbL = 0.0f, fbR = 0.0f;
        if (feedback > 0.0f) {
            const double readAt = static_cast<double>(s.delayWritten) - delaySamples;
            const double fl = std::floor(readAt);
            const auto frac = static_cast<float>(readAt - fl);
            const auto idx = static_cast<std::int64_t>(fl);
            const auto wrap = [&](const std::int64_t k) { return static_cast<std::size_t>(((k % lineSize) + lineSize) % lineSize); };
            fbL = s.delayL[wrap(idx)] * (1.0f - frac) + s.delayL[wrap(idx + 1)] * frac;
            fbR = s.delayR[wrap(idx)] * (1.0f - frac) + s.delayR[wrap(idx + 1)] * frac;
            if (filterOn) {
                fbL = runFilter(s.filterL, filter, filterMode, fbL);
                fbR = runFilter(s.filterR, filter, filterMode, fbR);
            }
            fbL = std::tanh(fbL * feedback);
            fbR = std::tanh(fbR * feedback);
        }

        const auto slot = static_cast<std::size_t>(s.written % s.bufferL.size());
        s.bufferL[slot] = flush(dryL + fbL);
        s.bufferR[slot] = flush(dryR + fbR);
        const auto w = static_cast<double>(s.written);

        // One new grain every window / density samples.
        if (s.untilNextGrain <= 0.0) {
            s.untilNextGrain += interval;
            for (auto& g : s.grains) {
                if (g.active) continue;
                const int length = std::max(2, static_cast<int>(static_cast<double>(windowSamples) * (1.0 - 0.75 * variance * s.rng.next())));
                const float offset = drawOffset(s.rng, centre, range, direction, scale);
                const bool reversed = s.rng.next() < reverse;
                const double blurSamples = static_cast<double>(blur) * static_cast<double>(windowSamples) * s.rng.next();
                const float pan = spread * (2.0f * s.rng.next() - 1.0f);
                const double ratio = std::pow(2.0, static_cast<double>(semis + offset) / 12.0);
                // How far behind "now" the grain starts. A grain that reads faster than real time
                // needs a head start, or it would catch the write position and read the future.
                double gap = blurSamples + 2.0;
                if (!reversed) gap += std::max(0.0, ratio - 1.0) * static_cast<double>(length);
                gap = std::min(gap, history - ratio * static_cast<double>(length) - 8.0);
                gap = std::max(gap, 2.0);
                const double angle = (static_cast<double>(pan) + 1.0) * kPi / 4.0;
                g.active = true;
                g.pos = w - gap;
                g.step = reversed ? -ratio : ratio;
                g.age = 0;
                g.length = length;
                g.gainL = static_cast<float>(std::sqrt(2.0) * std::cos(angle));
                g.gainR = static_cast<float>(std::sqrt(2.0) * std::sin(angle));
                break;
            }
        }
        s.untilNextGrain -= 1.0;

        float wetL = 0.0f, wetR = 0.0f;
        int active = 0;
        for (auto& g : s.grains) {
            if (!g.active) continue;
            ++active;
            const float x = static_cast<float>(g.age) / static_cast<float>(g.length) * Curves::kN;
            const int i = std::min(static_cast<int>(x), Curves::kN - 1);
            const float f = x - static_cast<float>(i);
            const float envelope = s.shapeTable[static_cast<std::size_t>(i)] +
                                   f * (s.shapeTable[static_cast<std::size_t>(i) + 1] - s.shapeTable[static_cast<std::size_t>(i)]);
            const float gain = envelope * grainGain;
            wetL += readHermite(s.bufferL, g.pos) * gain * g.gainL;
            wetR += readHermite(s.bufferR, g.pos) * gain * g.gainR;
            g.pos += g.step;
            if (++g.age >= g.length) g.active = false;
        }
        s.statusGrains = active;

        // Always recorded, so raising Feedback never replays an old pass.
        const auto line = static_cast<std::size_t>(s.delayWritten % static_cast<std::uint64_t>(lineSize));
        s.delayL[line] = flush(wetL);
        s.delayR[line] = flush(wetR);
        ++s.delayWritten;
        ++s.written;

        s.mixSmooth += (mixTarget - s.mixSmooth) * 0.002f;
        outL[n] = dryL * (1.0f - s.mixSmooth) + wetL * s.mixSmooth;
        outR[n] = dryR * (1.0f - s.mixSmooth) + wetR * s.mixSmooth;
    }

    s.statusWindow = static_cast<float>(window);
    s.statusDelay = static_cast<float>(delayMs(p, bpm));
    s.statusLatency = static_cast<float>(latencyMs(window, blur, semis));
}

}  // namespace downspout::grist
