#include "zmorph_engine.hpp"
#include "zmorph_params.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace downspout::zmorph;

int main() {
    Engine e;
    activate(e, 48000.0);
    std::vector<float> inL(256, 0.0f), inR(256, 0.0f), outL(256), outR(256);
    Params p;
    // impulse
    inL[0] = 1.0f; inR[0] = 1.0f;
    processBlock(e, p, inL.data(), inR.data(), outL.data(), outR.data(), 256);
    for (int i = 0; i < 256; ++i) {
        assert(std::isfinite(outL[i]) && std::isfinite(outR[i]));
    }
    reset(e);
    std::cout << "zmorph core tests passed\n";
    return 0;
}