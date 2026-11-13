#include "zmorph_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::zmorph {
namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void activate(Engine& e, double sr) {
    e.sampleRate = sr;
    e.allocated = true;
    reset(e);
}

void reset(Engine& e) {
    for (auto& c : e.channels) { c.x1=c.x2=c.y1=c.y2=0.0f; }
}

void processBlock(Engine& e, const Params& p, const float* inL, const float* inR, float* outL, float* outR, std::uint32_t frames) {
    if (!e.allocated) {
        for (std::uint32_t i=0;i<frames;++i){outL[i]=inL[i];outR[i]=inR[i];}
        return;
    }
    const float cutoff = p.noteTracking ? p.noteCutoff : p.cutoff;
    const float fc = std::clamp(cutoff, 20.0f, (float)e.sampleRate*0.49f);
    const float Q = std::max(0.15f, 0.5f + 2.0f * p.resonance);
    const float morph = std::clamp(p.morph, 0.0f, 1.0f);
    const float w0 = 2.0f * kPi * fc / (float)e.sampleRate;
    const float cosw = std::cos(w0);
    const float sinw = std::sin(w0);
    const float alpha = sinw / (2.0f * Q);

    // LP coeffs
    float lp_b0 = (1.0f - cosw) / 2.0f;
    float lp_b1 = 1.0f - cosw;
    float lp_b2 = (1.0f - cosw) / 2.0f;
    // HP coeffs
    float hp_b0 = (1.0f + cosw) / 2.0f;
    float hp_b1 = -(1.0f + cosw);
    float hp_b2 = (1.0f + cosw) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cosw;
    float a2 = 1.0f - alpha;

    // morph
    e.b0 = (lp_b0*(1-morph) + hp_b0*morph) / a0;
    e.b1 = (lp_b1*(1-morph) + hp_b1*morph) / a0;
    e.b2 = (lp_b2*(1-morph) + hp_b2*morph) / a0;
    float na1 = a1 / a0;
    float na2 = a2 / a0;
    e.a1 = na1;
    e.a2 = na2;

    const float mix = p.mix;
    const float gain = std::pow(10.0f, p.level/20.0f);
    for (std::uint32_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            auto& c = e.channels[ch];
            const float* in = (ch==0)?inL:inR;
            float* out = (ch==0)?outL:outR;
            float x = in[i];
            float y = e.b0*x + e.b1*c.x1 + e.b2*c.x2 - e.a1*c.y1 - e.a2*c.y2;
            c.x2 = c.x1; c.x1 = x;
            c.y2 = c.y1; c.y1 = y;
            float wet = y;
            out[i] = gain * ((1.0f-mix)*x + mix*wet);
        }
    }
}

}  // namespace downspout::zmorph