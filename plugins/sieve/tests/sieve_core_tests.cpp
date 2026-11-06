#include "sieve_engine.hpp"
#include "sieve_params.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace downspout::sieve;

int main() {
    Engine e;
    activate(e, 48000.0);
    assert(e.allocated);
    // silence in -> silence out
    std::vector<float> inL(1024, 0.0f), inR(1024, 0.0f), outL(1024), outR(1024);
    Params p;
    processBlock(e, p, inL.data(), inR.data(), outL.data(), outR.data(), 1024, nullptr, 0);
    for (int i = 0; i < 1024; ++i) {
        assert(std::isfinite(outL[i]) && std::isfinite(outR[i]));
    }
    // reset doesn't crash
    reset(e);
    std::cout << "sieve core tests passed\n";
    return 0;
}