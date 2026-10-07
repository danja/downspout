#include "pratt_engine.hpp"
#include <cstdio>
#include <cstdlib>
using namespace downspout::pratt;
int main(int argc, char** argv) {
    int preset = atoi(argv[1]), pitch = atoi(argv[2]), vel = atoi(argv[3]); double hold = atof(argv[4]); const char* out = argv[5];
    Engine e(44100.0);
    EngineParams p; p.presetOverride = preset; p.room = 0.0; p.masterGain = 0.01; e.setParams(p);
    e.noteOn(0, pitch, vel);
    int total = 44100 * 2; std::vector<float> l(total), r(total);
    int held = int(hold * 44100);
    e.process(nullptr, nullptr, l.data(), r.data(), held);
    e.noteOff(0, pitch);
    e.process(nullptr, nullptr, l.data() + held, r.data() + held, total - held);
    FILE* f = fopen(out, "wb"); fwrite(l.data(), 4, total, f); fclose(f);
}
