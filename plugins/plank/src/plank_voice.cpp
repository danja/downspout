#include "plank_voice.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::plank {

// ── Envelope ────────────────────────────────────────────────────────────────

void Envelope::gateOn() noexcept
{
    // Re-plucking a sounding string restarts the attack, as on Plinky.
    stage_ = Stage::attack;
}

void Envelope::gateOff() noexcept
{
    if (stage_ != Stage::idle)
        stage_ = Stage::release;
}

void Envelope::advance(const float attackStep,
                       const float decayStep,
                       const float releaseStep,
                       const float sustain) noexcept
{
    switch (stage_)
    {
    case Stage::attack:
        level_ += attackStep;
        if (level_ >= 1.0f)
        {
            level_ = 1.0f;
            stage_ = Stage::decay;
        }
        break;

    case Stage::decay:
        level_ -= decayStep;
        if (level_ <= sustain)
        {
            level_ = sustain;
            stage_ = Stage::sustain;
        }
        break;

    case Stage::sustain:
        level_ = sustain;
        break;

    case Stage::release:
        level_ -= releaseStep;
        if (level_ <= 0.0f)
        {
            level_ = 0.0f;
            stage_ = Stage::idle;
        }
        break;

    case Stage::idle:
        level_ = 0.0f;
        break;
    }
}

// ── Lfo ─────────────────────────────────────────────────────────────────────

std::uint32_t Lfo::random() noexcept
{
    randomState ^= randomState << 13u;
    randomState ^= randomState >> 17u;
    randomState ^= randomState << 5u;
    return randomState;
}

// ── Voice ───────────────────────────────────────────────────────────────────

void Voice::reset() noexcept
{
    oscillator[0] = Oscillator {};
    oscillator[1] = Oscillator {};
    filter.reset();
    amplitude.reset();
    modulation.reset();
    glideSmoother.reset(1.0f);
    noise = 0.0f;
    level = 0.0f;
    pitchSemitones = 0.0f;
    phaseOffset = 0.0f;
    held = false;
    sounding = false;
    degree = 0;
    modal.reset();
    exciter = Exciter {};
}

// ── Modal resonators ────────────────────────────────────────────────────────

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Approximate mode ratios. Beam is a free-free bar (Euler-Bernoulli), marimba a
// bar with its first overtone tuned to two octaves, drumhead the circular
// membrane's Bessel zeros, membrane a square membrane (sqrt(m^2+n^2) normalised
// to the lowest mode), plate a free square plate, and string a stiff string.
constexpr float kBeam[kModeCount] = {1.0f, 2.756f, 5.404f, 8.933f, 13.344f, 18.638f, 24.812f, 31.87f};
constexpr float kMarimba[kModeCount] = {1.0f, 3.99f, 9.92f, 17.9f, 27.5f, 38.5f, 51.0f, 64.0f};
constexpr float kDrumhead[kModeCount] = {1.0f, 1.594f, 2.136f, 2.296f, 2.653f, 2.918f, 3.156f, 3.501f};
constexpr float kMembrane[kModeCount] = {1.0f, 1.581f, 2.0f, 2.236f, 2.550f, 2.915f, 3.0f, 3.162f};
constexpr float kPlate[kModeCount] = {1.0f, 1.60f, 2.32f, 2.93f, 3.74f, 4.29f, 5.05f, 5.90f};

constexpr float kStringStiffness = 0.0004f;

}  // namespace

const float* modeRatios(const EngineId engine) noexcept
{
    static const auto string = [] {
        std::array<float, kModeCount> ratios {};
        for (std::size_t k = 0; k < kModeCount; ++k)
        {
            const float n = static_cast<float>(k + 1u);
            ratios[k] = n * std::sqrt(1.0f + kStringStiffness * n * n);
        }
        return ratios;
    }();

    switch (engine)
    {
    case EngineId::beam: return kBeam;
    case EngineId::marimba: return kMarimba;
    case EngineId::drumhead: return kDrumhead;
    case EngineId::membrane: return kMembrane;
    case EngineId::plate: return kPlate;
    case EngineId::plinky:
    case EngineId::string:
    case EngineId::count:
        break;
    }
    return string.data();
}

void computeModalCoefficients(const EngineId engine,
                              const double fundamentalHz,
                              const double sampleRate,
                              const float damping,
                              const float material,
                              const float strike,
                              ModalCoefficients& out) noexcept
{
    const float* ratios = modeRatios(engine);

    // 50 ms to about 4 s for the fundamental; exponential so the control feels even.
    const double baseT60 = 0.05 * std::pow(80.0, static_cast<double>(damping));
    // Wood/skin: upper modes fall away steeply. Metal/glass: they ring nearly as long.
    const double tilt = 2.2 - 1.9 * static_cast<double>(material);

    for (std::size_t k = 0; k < kModeCount; ++k)
    {
        const double frequency = fundamentalHz * static_cast<double>(ratios[k]);
        if (frequency <= 0.0 || frequency > sampleRate * 0.45)
        {
            out.b1[k] = out.b2[k] = out.gain[k] = 0.0f;
            continue;
        }

        const double t60 = std::max(0.004, baseT60 * std::pow(static_cast<double>(ratios[k]), -tilt));
        const double radius = std::exp(-6.907755 / (t60 * sampleRate));
        const double omega = 2.0 * static_cast<double>(kPi) * frequency / sampleRate;

        out.b1[k] = static_cast<float>(2.0 * radius * std::cos(omega));
        out.b2[k] = static_cast<float>(-radius * radius);

        // sin(omega) normalises the resonator's unit-impulse response to unit
        // amplitude; the strike term nulls modes with a node under the strike.
        const float position = std::fabs(std::sin(kPi * static_cast<float>(k + 1u) * strike));
        out.gain[k] = static_cast<float>(std::sin(omega)) * std::max(0.04f, position);
    }
}

void Exciter::trigger(const ExciterId kind, const float hardness, const double sampleRate) noexcept
{
    position = 0;
    lowpass = 0.0f;

    // Mallet: 6 ms soft down to 0.15 ms hard. Noise: a fixed 12 ms burst.
    const double seconds = kind == ExciterId::mallet ? 0.006 - 0.00585 * static_cast<double>(hardness) : 0.012;
    length = static_cast<std::uint32_t>(std::max(2.0, seconds * sampleRate));
}

float Exciter::next(const ExciterId kind, const float hardness) noexcept
{
    if (length == 0u)
        return 0.0f;

    float out = 0.0f;
    const float phase = static_cast<float>(position) / static_cast<float>(length);

    if (kind == ExciterId::mallet)
    {
        // Half-sine of unit area: peak = pi / (2 * width).
        out = std::sin(kPi * phase) * (kPi * 0.5f / static_cast<float>(length));
    }
    else
    {
        random ^= random << 13u;
        random ^= random >> 17u;
        random ^= random << 5u;
        const float white = static_cast<float>(random & 0xffffu) * (1.0f / 32768.0f) - 1.0f;
        lowpass += (white - lowpass) * (0.04f + 0.9f * hardness);
        out = lowpass * (1.0f - phase) * 0.12f;
    }

    if (++position >= length)
        length = 0;
    return out;
}

}  // namespace downspout::plank
