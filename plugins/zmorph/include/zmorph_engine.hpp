#pragma once

#include "zmorph_params.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace downspout::zmorph {

struct Engine {
    double sampleRate = 48000.0;
    bool allocated = false;
    struct Ch { float x1=0,x2=0,y1=0,y2=0; };
    std::array<Ch,2> channels;
    // coeffs
    float b0=1,b1=0,b2=0,a1=0,a2=0;
};

void activate(Engine& e, double sr);
void reset(Engine& e);
inline int latencySamples(const Engine&) noexcept { return 0; }
void processBlock(Engine& e, const Params& p, const float* inL, const float* inR, float* outL, float* outR, std::uint32_t frames);

}  // namespace downspout::zmorph