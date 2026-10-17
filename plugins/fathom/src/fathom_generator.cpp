#include "fathom_generator.hpp"

#include "fathom_dsp.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace downspout::fathom {
namespace {

constexpr int kControlInterval = 32;  // samples between control updates

// ── Wavetables ─────────────────────────────────────────────────────────────

constexpr int kFrames = 8;
constexpr int kLevels = 6;
constexpr int kTableSize = 1024;
constexpr std::array<int, kLevels> kLevelHarmonics = {{32, 16, 8, 4, 2, 1}};

[[nodiscard]] bool isPrime(const int n) noexcept
{
    if (n < 2)
        return false;
    for (int d = 2; d * d <= n; ++d)
    {
        if (n % d == 0)
            return false;
    }
    return true;
}

// Harmonic recipes, ordered from pure to complex so Morph scans a useful range.
[[nodiscard]] double frameAmplitude(const int frame, const int h) noexcept
{
    const double x = static_cast<double>(h);
    switch (frame)
    {
    case 0: return h == 1 ? 1.0 : 0.0;                                   // sine
    case 1: return h <= 3 ? 1.0 / (x * x) : 0.0;                         // soft
    case 2: return 1.0 / x;                                              // saw
    case 3: return h % 2 == 1 ? 1.0 / x : 0.0;                           // square
    case 4: return h % 3 == 0 ? 1.0 / x : 0.1 / x;                       // hollow, thirds
    case 5:
    {
        const double d = (x - 5.0) / 1.5;                                // formant near h5
        return (1.0 / x) * (0.2 + 1.8 * std::exp(-d * d));
    }
    case 6: return 1.0 / std::sqrt(x);                                   // bright
    default: return (h == 1 || isPrime(h)) ? 1.0 / x : 0.15 / x;         // prime-weighted
    }
}

class Wavetables {
public:
    Wavetables() : data_(static_cast<std::size_t>(kLevels) * kFrames * (kTableSize + 1), 0.0f)
    {
        for (int level = 0; level < kLevels; ++level)
        {
            for (int frame = 0; frame < kFrames; ++frame)
            {
                float* t = mutableTable(level, frame);
                double peak = 1.0e-9;
                for (int i = 0; i < kTableSize; ++i)
                {
                    double sum = 0.0;
                    for (int h = 1; h <= kLevelHarmonics[static_cast<std::size_t>(level)]; ++h)
                        sum += frameAmplitude(frame, h) * std::sin(kTwoPi * h * i / kTableSize);
                    t[i] = static_cast<float>(sum);
                    peak = std::max(peak, std::fabs(sum));
                }
                for (int i = 0; i < kTableSize; ++i)
                    t[i] = static_cast<float>(t[i] / peak);
                t[kTableSize] = t[0];
            }
        }
    }

    [[nodiscard]] const float* table(const int level, const int frame) const noexcept
    {
        return data_.data() + (static_cast<std::size_t>(level) * kFrames + static_cast<std::size_t>(frame)) *
                                  (kTableSize + 1);
    }

private:
    float* mutableTable(const int level, const int frame) noexcept
    {
        return data_.data() + (static_cast<std::size_t>(level) * kFrames + static_cast<std::size_t>(frame)) *
                                  (kTableSize + 1);
    }

    std::vector<float> data_;
};

const Wavetables& wavetables()
{
    static const Wavetables tables;
    return tables;
}

[[nodiscard]] float readTable(const float* t, double phase) noexcept
{
    phase -= std::floor(phase);
    const double x = phase * kTableSize;
    const int i = static_cast<int>(x);
    const float f = static_cast<float>(x - i);
    return t[i] + (t[i + 1] - t[i]) * f;
}

// Mip level whose harmonic count stays under Nyquist at `hertz`.
[[nodiscard]] int levelFor(const double hertz, const double sampleRate) noexcept
{
    for (int level = 0; level < kLevels; ++level)
    {
        if (kLevelHarmonics[static_cast<std::size_t>(level)] * hertz <= 0.45 * sampleRate)
            return level;
    }
    return kLevels - 1;
}

// ── Helpers ────────────────────────────────────────────────────────────────

// Smooth wavefold: transparent at fuse 0, folding harder as it rises.
[[nodiscard]] float fold(const float x, const float fuse) noexcept
{
    if (fuse < 0.001f)
        return x;
    const float gain = 1.0f + fuse * 5.0f;
    return std::sin(x * gain * static_cast<float>(kPi * 0.5));
}

[[nodiscard]] float lerp(const float a, const float b, const float t) noexcept
{
    return a + (b - a) * t;
}

// Nearest degree of the major scale, in semitones from the root.
[[nodiscard]] float quantiseMajor(const float semitones) noexcept
{
    constexpr std::array<float, 8> degrees = {{0.0f, 2.0f, 4.0f, 5.0f, 7.0f, 9.0f, 11.0f, 12.0f}};
    const float octave = std::floor(semitones / 12.0f);
    const float within = semitones - octave * 12.0f;
    float best = 0.0f;
    float bestDistance = 99.0f;
    for (const float degree : degrees)
    {
        const float distance = std::fabs(within - degree);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = degree;
        }
    }
    return octave * 12.0f + best;
}

}  // namespace

Generator::Generator()
{
    (void)sineTable();  // build the shared tables off the audio thread
    (void)wavetables();
    deriveShape();
    restart();
}

void Generator::setSampleRate(const double sampleRate) noexcept
{
    sampleRate_ = std::max(sampleRate, 1.0);
    smoothing_ = static_cast<float>(1.0 - std::exp(-static_cast<double>(kControlInterval) / (0.02 * sampleRate_)));
}

void Generator::setControls(const GeneratorControls& controls) noexcept
{
    target_ = controls;
    target_.algorithm = std::min(std::max(controls.algorithm, 0), 4);
}

void Generator::snap() noexcept
{
    now_ = target_;
    deriveShape();
    snapPending_ = false;
}

void Generator::restart() noexcept
{
    for (std::size_t k = 0; k < phase_.size(); ++k)
        phase_[k] = wrapPhase(static_cast<double>(k) * 0.6180339887);
    lastModulator_ = 0.0f;
    lastOut_.fill(0.0f);
}

void Generator::tick() noexcept
{
    now_.algorithm = target_.algorithm;
    now_.warp += (target_.warp - now_.warp) * smoothing_;
    now_.span += (target_.span - now_.span) * smoothing_;
    now_.morph += (target_.morph - now_.morph) * smoothing_;
    now_.seed += (target_.seed - now_.seed) * smoothing_;
    now_.fuse += (target_.fuse - now_.fuse) * smoothing_;
    deriveShape();
}

// Per-oscillator ratios and levels from span, morph and the seed noise.
void Generator::deriveShape() noexcept
{
    for (std::size_t k = 0; k < offsets_.size(); ++k)
        offsets_[k] = valueNoise(now_.seed * 10.0f, static_cast<std::uint32_t>(k));

    switch (now_.algorithm)
    {
    case 0:
        // Second oscillator's tuning: nothing at span 0, up to a fifth-and-a-bit above.
        ratio_[0] = 1.0f;
        ratio_[1] = std::exp2(now_.span * now_.span * 7.0f / 12.0f);
        break;

    case 1:
        // Modulator ratio: 0.5 to 4.
        ratio_[0] = 1.0f;
        ratio_[1] = std::exp2(now_.span * 3.0f - 1.0f);
        break;

    case 2:
    {
        // Harmonics 1-7 drifting apart with span, plus a near-unison partner of the
        // fundamental whose beating is the drone.
        const float exponent = 0.3f + 3.0f * (1.0f - now_.morph);
        float energy = 0.0f;
        for (std::size_t k = 0; k < 7; ++k)
        {
            const float h = static_cast<float>(k + 1);
            ratio_[k] = k == 0 ? 1.0f : h * (1.0f + now_.span * 0.03f * offsets_[k]);
            weight_[k] = std::pow(h, -exponent);
        }
        ratio_[7] = 1.0f + now_.span * 0.01f * (0.5f + 0.5f * offsets_[7]);
        weight_[7] = weight_[0] * 0.8f;
        for (std::size_t k = 0; k < 8; ++k)
            energy += weight_[k] * weight_[k];
        norm_ = 0.75f / std::sqrt(energy);
        break;
    }

    case 3:
        // Four oscillators fanned out in cents around the pitch.
        for (std::size_t i = 0; i < 4; ++i)
        {
            const float cents = now_.span * 40.0f * (static_cast<float>(i) - 1.5f) * (1.0f + 0.5f * offsets_[i]);
            ratio_[i] = std::exp2(cents / 1200.0f);
        }
        break;

    default:
    {
        // Four oscillators at diatonic offsets from the pitch; Morph scans a window
        // across them.
        const float position = now_.morph * 3.0f;
        float total = 0.0f;
        for (std::size_t i = 0; i < 4; ++i)
        {
            ratio_[i] = std::exp2(quantiseMajor(offsets_[i] * now_.span * 14.0f) / 12.0f);
            const float d = static_cast<float>(i) - position;
            weight_[i] = 0.08f + std::exp(-d * d / 0.9f);
            total += weight_[i];
        }
        norm_ = 1.0f / total;
        break;
    }
    }
}

double Generator::advance(const std::size_t oscillator, const double hertz) noexcept
{
    const double step = std::min(hertz, 0.45 * sampleRate_) / sampleRate_;
    double p = phase_[oscillator] + step;
    if (p >= 1.0)
        p -= 1.0;
    phase_[oscillator] = p;
    return step;
}

float Generator::process(const double hertz) noexcept
{
    if (snapPending_)
        snap();
    if (++tick_ >= kControlInterval)
    {
        tick_ = 0;
        tick();
    }

    switch (now_.algorithm)
    {
    case 0: return analog(hertz);
    case 1: return shaper(hertz);
    case 2: return sines(hertz);
    case 3: return table(hertz);
    default: return cluster(hertz);
    }
}

// Two band-limited oscillators, a saw and a pulse. Warp is exponential
// cross-modulation of the saw by the pulse, Span detunes the pulse, Morph fades
// both towards sines, Fuse widens the pulse.
float Generator::analog(const double hertz) noexcept
{
    const auto& sine = sineTable();
    const double f2 = hertz * ratio_[1];
    const double f1 = hertz * std::exp2(static_cast<double>(now_.warp * 2.0f * lastModulator_));
    const double dt1 = advance(0, f1);
    const double dt2 = advance(1, f2);

    const double width = 0.5 + 0.4 * static_cast<double>(now_.fuse);
    const float o1 = lerp(blepSaw(phase_[0], dt1), sine(phase_[0]), now_.morph);
    const float o2 = lerp(blepPulse(phase_[1], dt2, width), sine(phase_[1]), now_.morph);
    lastModulator_ = o2;
    return 0.5f * (o1 + o2);
}

// A sine carrier phase-modulated by a second sine (Warp), faded towards ring
// modulation by it (Morph), then folded (Fuse). Span sets the modulator ratio.
float Generator::shaper(const double hertz) noexcept
{
    const auto& sine = sineTable();
    advance(0, hertz);
    advance(1, hertz * ratio_[1]);
    const float m = sine(phase_[1]);
    const float c = sine(wrapPhase(phase_[0] + static_cast<double>(now_.warp * 0.6f * m)));
    const float shaped = lerp(c, c * m, now_.morph);
    return fold(shaped, now_.fuse);
}

// Eight sines. Warp reduces the bit depth, Span detunes the partials from exact
// harmonics, Morph tilts the spectrum, Fuse saturates.
float Generator::sines(const double hertz) noexcept
{
    const auto& sine = sineTable();
    float sum = 0.0f;
    for (std::size_t k = 0; k < 8; ++k)
    {
        const double f = hertz * ratio_[k];
        if (f > 0.45 * sampleRate_)
            continue;
        advance(k, f);
        sum += weight_[k] * sine(phase_[k]);
    }
    float out = sum * norm_;

    if (now_.warp > 0.001f)
    {
        const float levels = std::exp2(16.0f - now_.warp * 13.0f);  // 65536 down to 8 steps
        out = std::round(out * levels * 0.5f) / (levels * 0.5f);
    }
    if (now_.fuse > 0.001f)
    {
        const float gain = 1.0f + now_.fuse * 6.0f;
        out = std::tanh(out * gain) / std::tanh(gain);
    }
    return out;
}

// Four wavetable oscillators. Morph scans the eight frames, Seed offsets each
// oscillator's frame, Span fans their tuning, Warp phase-modulates each by its
// neighbour, Fuse folds.
float Generator::table(const double hertz) noexcept
{
    const auto& tables = wavetables();
    float sum = 0.0f;
    std::array<float, 4> out {};
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double f = hertz * ratio_[i];
        advance(i, f);
        const int level = levelFor(f, sampleRate_);

        const float position = std::min(std::max(now_.morph * 7.0f + offsets_[i] * 2.0f, 0.0f), 7.0f);
        const int frame = std::min(static_cast<int>(position), 6);
        const float blend = position - static_cast<float>(frame);

        const double phase = phase_[i] + static_cast<double>(now_.warp * 0.6f * lastOut_[(i + 3) & 3]);
        const float a = readTable(tables.table(level, frame), phase);
        const float b = readTable(tables.table(level, frame + 1), phase);
        out[i] = lerp(a, b, blend);
        sum += out[i];
    }
    lastOut_ = out;
    return fold(sum * 0.4f, now_.fuse);
}

// Four oscillators at diatonic steps around the pitch. Span widens the steps,
// Seed picks them, Morph scans a window across the four, Warp adds ring
// modulation between neighbours, Fuse fades each from sine to saw.
float Generator::cluster(const double hertz) noexcept
{
    const auto& sine = sineTable();
    std::array<float, 4> o {};
    float sum = 0.0f;
    for (std::size_t i = 0; i < 4; ++i)
    {
        const double f = hertz * ratio_[i];
        const double dt = advance(i, f);
        o[i] = lerp(sine(phase_[i]), blepSaw(phase_[i], dt), now_.fuse);
        sum += weight_[i] * o[i];
    }
    const float base = sum * norm_;
    if (now_.warp < 0.001f)
        return base;

    const float ring = (o[0] * o[1] + o[1] * o[2] + o[2] * o[3]) * (1.0f / 3.0f);
    return lerp(base, ring * 1.6f, now_.warp * 0.6f);
}

}  // namespace downspout::fathom
