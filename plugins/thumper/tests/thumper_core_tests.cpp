#include "downspout/test_assert.h"
#include "thumper_core_types.hpp"
#include "thumper_serialization.hpp"
#include <cmath>
#include <cstring>

int main() {
    thumper::ThumperEngine e;
    e.setSampleRate(44100);
    thumper::Params p;
    p.engine = (int)thumper::Engine::VirtualAnalog;
    e.setParams(p);
    e.trigger();
    float L[64], R[64];
    std::memset(L, 0, sizeof(L));
    std::memset(R, 0, sizeof(R));
    e.render(L, R, 64);
    // some output
    float sum = 0;
    for (int i=0;i<64;i++) sum += std::fabs(L[i]);
    assert(sum > 0.0f);
    // serialization roundtrip
    std::string s = thumper::serialize(p);
    thumper::Params p2;
    assert(thumper::deserialize(s, p2));
    assert(p2.engine == p.engine);
    return 0;
}