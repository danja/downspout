#pragma once

#include "bubbles_core_types.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::bubbles {

// ---------------------------------------------------------------------------
// LFO division table (beats per cycle, matches Basilico's WobbleModulator)
// ---------------------------------------------------------------------------

inline float lfoBeatsPerCycle(int division, double beatsPerBar)
{
    const float bar = static_cast<float>(std::max(1.0, beatsPerBar));
    switch (std::clamp(division, 0, 7)) {
    case 0: return bar;
    case 1: return std::max(0.5f, bar * 0.5f);
    case 2: return 1.0f;
    case 3: return 0.5f;
    case 4: return 1.0f / 3.0f;
    case 5: return 0.25f;
    case 6: return 1.0f / 6.0f;
    default: return 0.125f;
    }
}

// Bipolar shape value [-1, 1] from phase [0, 1)
inline float lfoShapeValue(int shape, float phase)
{
    phase -= std::floor(phase);
    switch (std::clamp(shape, 0, 4)) {
    case 1: return phase < 0.5f ? -1.0f + phase * 4.0f : 3.0f - phase * 4.0f;
    case 2: return 1.0f - phase * 2.0f;
    case 3: return phase * 2.0f - 1.0f;
    case 4: return phase < 0.5f ? 1.0f : -1.0f;
    default: return std::sin(phase * 6.28318530718f);
    }
}

// Process LFO for nSamples. Advances lfo state. Returns unipolar [0, 1].
// Call once per control-rate block (nSamples = kControlUpdatePeriod).
inline float processLfo(LfoState&                state,
                        const TransportSnapshot& transport,
                        int   shape,
                        float rateHz,
                        bool  sync,
                        int   division,
                        float sampleRate,
                        int   nSamples)
{
    const float sr = std::max(1000.0f, sampleRate);
    float phase = state.freePhase;

    const bool useSync = sync && transport.valid && transport.playing && transport.bpm > 1.0;
    if (useSync) {
        const float beatsPerCycle = lfoBeatsPerCycle(division, transport.beatsPerBar);
        const double absBeat = transport.bar * transport.beatsPerBar + transport.barBeat;
        phase = static_cast<float>(std::fmod(absBeat / beatsPerCycle, 1.0));
        const double bps = transport.bpm / 60.0;
        state.freePhase += static_cast<float>(bps * nSamples / beatsPerCycle / sr);
        if (state.freePhase >= 1.0f)
            state.freePhase -= std::floor(state.freePhase);
    } else {
        state.freePhase += std::clamp(rateHz, 0.05f, 20.0f) * static_cast<float>(nSamples) / sr;
        if (state.freePhase >= 1.0f)
            state.freePhase -= std::floor(state.freePhase);
    }

    const float raw = lfoShapeValue(shape, phase);
    // nSamples of one-pole smoothing in one step (removes square-wave edges)
    const float smoothMs = (shape == 4) ? 4.5f : 1.25f;
    const float coeff = 1.0f - std::exp(-static_cast<float>(nSamples) / (sr * smoothMs * 0.001f));
    state.smoothed += (raw - state.smoothed) * coeff;

    const float clamped = std::clamp(std::isfinite(state.smoothed) ? state.smoothed : 0.0f,
                                     -1.0f, 1.0f);
    return 0.5f + clamped * 0.5f;
}

// Apply LFO unipolar output to a target parameter (additive, clamped to [0, 1])
inline void applyLfoToParams(Parameters& p, float uni, float depth, int target)
{
    const float delta = (uni - 0.5f) * 2.0f * depth;
    switch (target) {
    case 1: p.flow       = std::clamp(p.flow       + delta, 0.0f, 1.0f); break;
    case 2: p.density    = std::clamp(p.density    + delta, 0.0f, 1.0f); break;
    case 3: p.brightness = std::clamp(p.brightness + delta, 0.0f, 1.0f); break;
    case 4: p.size       = std::clamp(p.size       + delta, 0.0f, 1.0f); break;
    case 5: p.heat       = std::clamp(p.heat       + delta, 0.0f, 1.0f); break;
    case 6: p.randomness = std::clamp(p.randomness + delta, 0.0f, 1.0f); break;
    default: break;
    }
}

}  // namespace downspout::bubbles
