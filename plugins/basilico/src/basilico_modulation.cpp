#include "basilico_modulation.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::basilico {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

float clampUnit(const float value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

double absoluteBeat(const TransportSnapshot& transport)
{
    return transport.bar * std::max(1.0, transport.beatsPerBar) + transport.barBeat;
}

bool transportUsable(const TransportSnapshot& transport)
{
    return transport.valid && transport.playing && transport.bpm > 1.0 && transport.beatsPerBar > 0.0;
}

} // namespace

float basilicoWobbleDivisionBeats(const int division, const double beatsPerBar)
{
    const float barBeats = static_cast<float>(std::max(1.0, beatsPerBar));
    switch (std::clamp(division, 0, 7))
    {
    case 0:
        return barBeats;
    case 1:
        return std::max(0.5f, barBeats * 0.5f);
    case 2:
        return 1.0f;
    case 3:
        return 0.5f;
    case 4:
        return 1.0f / 3.0f;
    case 5:
        return 0.25f;
    case 6:
        return 1.0f / 6.0f;
    default:
        return 0.125f;
    }
}

float basilicoWobbleShapeValue(const int shape, float phase)
{
    phase -= std::floor(phase);
    switch (std::clamp(shape, 0, 4))
    {
    case 1:
        return phase < 0.5f ? -1.0f + phase * 4.0f : 3.0f - phase * 4.0f;
    case 2:
        return 1.0f - phase * 2.0f;
    case 3:
        return phase * 2.0f - 1.0f;
    case 4:
        return phase < 0.5f ? 1.0f : -1.0f;
    default:
        return std::sin(phase * kTwoPi);
    }
}

void WobbleModulator::reset()
{
    beatPosition_ = 0.0;
    beatsPerBar_ = 4.0;
    bpm_ = 120.0;
    transportUsable_ = false;
    freePhase_ = 0.0f;
    smoothedBipolar_ = 0.0f;
}

void WobbleModulator::setTransport(const TransportSnapshot& transport)
{
    if (transport.bpm > 1.0)
        bpm_ = transport.bpm;
    if (transport.beatsPerBar > 0.0)
        beatsPerBar_ = transport.beatsPerBar;

    transportUsable_ = transportUsable(transport);
    if (transportUsable_)
        beatPosition_ = absoluteBeat(transport);
}

WobbleFrame WobbleModulator::process(const WobbleConfig& config, const float sampleRate)
{
    const float safeSampleRate = std::max(1000.0f, sampleRate);
    float phase = freePhase_;

    if (config.sync)
    {
        const float beatsPerCycle = basilicoWobbleDivisionBeats(config.division, beatsPerBar_);
        if (transportUsable_)
            phase = static_cast<float>(std::fmod(beatPosition_ / beatsPerCycle, 1.0));

        const double safeBpm = std::clamp(bpm_, 20.0, 320.0);
        const double beatsPerSample = safeBpm / 60.0 / static_cast<double>(safeSampleRate);
        beatPosition_ += beatsPerSample;

        if (!transportUsable_)
        {
            freePhase_ += static_cast<float>(beatsPerSample / static_cast<double>(beatsPerCycle));
            if (freePhase_ >= 1.0f)
                freePhase_ -= std::floor(freePhase_);
        }
    }
    else
    {
        freePhase_ += std::clamp(config.freeRateHz, 0.05f, 20.0f) / safeSampleRate;
        if (freePhase_ >= 1.0f)
            freePhase_ -= std::floor(freePhase_);
    }

    const float target = basilicoWobbleShapeValue(config.shape, phase);
    const float smoothingMs = std::clamp(config.shape, 0, 4) == 4 ? 4.5f : 1.25f;
    const float coefficient = 1.0f - std::exp(-1.0f / (safeSampleRate * smoothingMs * 0.001f));
    smoothedBipolar_ += (target - smoothedBipolar_) * coefficient;

    const float bipolar = std::clamp(std::isfinite(smoothedBipolar_) ? smoothedBipolar_ : 0.0f, -1.0f, 1.0f);
    return {bipolar, clampUnit(0.5f + bipolar * 0.5f), config.sync && transportUsable_};
}

} // namespace downspout::basilico
