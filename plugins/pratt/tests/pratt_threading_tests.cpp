// The DPF wrapper's warm-up worker calls these Engine entry points from a second
// thread while the audio thread plays notes, retargets the filter and changes
// tuning. This suite hammers exactly that pattern. It passes on its own, but its
// real value is under ThreadSanitizer:
//   g++ -std=c++17 -O1 -g -fsanitize=thread -UNDEBUG -Iinclude -I../../include src/*.cpp tests/pratt_threading_tests.cpp -o pratt_threading_tsan
//   setarch "$(uname -m)" -R ./pratt_threading_tsan   (-R: TSan needs ASLR off on some kernels)
#include "downspout/test_assert.h"
#include "pratt_engine.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace downspout::pratt;

int main() {
    Engine engine(44100.0);
    std::atomic<bool> stop{false};
    std::atomic<int> workerSteps{0};

    // Worker: exactly what Warmer does, with the tuning changing underneath it.
    std::thread worker([&] {
        int round = 0;
        while (!stop.load()) {
            VoiceTuning tuning;
            tuning.xiScale = 0.8 + 0.1 * (round % 5);
            tuning.base = (round % 3 == 0) ? 0 : 5 + round % 4;
            engine.trimTables(tuning);
            for (int pitch = 48; pitch <= 72 && !stop.load(); ++pitch) {
                engine.preload(round % static_cast<int>(kPresetCount), tuning, pitch, pitch);
                workerSteps.fetch_add(1);
            }
            for (int drum = 38; drum <= 52 && !stop.load(); ++drum) engine.preloadDrum(drum);
            Engine::warmRoots(1 + (round * 97) % 8000, 64 + (round * 97) % 8000);
            ++round;
        }
    });

    // "Audio" thread: notes across presets, drums, parameter changes, filter retargets.
    std::vector<float> in(256, 0.1f), outL(256), outR(256);
    for (int block = 0; block < 600; ++block) {
        EngineParams p;
        p.mode = (block % 3 == 0) ? Mode::Both : (block % 3 == 1 ? Mode::Synth : Mode::Filter);
        p.presetOverride = block % 11;
        p.xiScale = 0.8 + 0.1 * (block % 5);
        p.baseOverride = (block % 7 == 0) ? 0 : 5 + block % 4;
        p.filterIndexA = 1 + block % 97;
        p.filterIndexB = 1 + block % 31;
        p.filterCutoffHz = 100.0 + 13.0 * block;
        engine.setParams(p);
        engine.noteOn(block % 16, 40 + block % 40, 60 + block % 60);
        engine.noteOn(9, 36 + block % 12, 100);
        if (block % 5 == 0) engine.allNotesOff();
        engine.process(in.data(), in.data(), outL.data(), outR.data(), 256);
        for (int i = 0; i < 256; ++i) assert(std::isfinite(outL[i]) && std::isfinite(outR[i]));
    }

    stop.store(true);
    worker.join();
    std::printf("worker steps %d\n", workerSteps.load());
    assert(workerSteps.load() > 0);
    std::puts("pratt threading tests passed");
    return 0;
}
