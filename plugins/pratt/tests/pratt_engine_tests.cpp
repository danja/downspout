#include "downspout/test_assert.h"
#include "pratt_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

using namespace downspout::pratt;

namespace {

constexpr double kFs = 44100.0;
constexpr double kPi = 3.14159265358979323846;

struct Rendered {
    std::vector<float> l, r;
};

Rendered run(Engine& e, int frames, const std::vector<float>* in = nullptr) {
    Rendered out{std::vector<float>(frames), std::vector<float>(frames)};
    e.process(in ? in->data() : nullptr, in ? in->data() : nullptr, out.l.data(), out.r.data(), frames);
    return out;
}

double peakOf(const std::vector<float>& v, std::size_t from = 0) {
    double p = 0.0;
    for (std::size_t i = from; i < v.size(); ++i) {
        assert(std::isfinite(v[i]));
        p = std::max(p, static_cast<double>(std::abs(v[i])));
    }
    return p;
}

// Frequency of strongest DFT bin in [lo, hi] Hz over a Hann-windowed slice.
double dominantHz(const std::vector<float>& x, std::size_t from, std::size_t len, double lo, double hi) {
    double best = 0.0, bestHz = 0.0;
    for (double hz = lo; hz <= hi; hz += 0.25) {
        double re = 0.0, im = 0.0;
        for (std::size_t i = 0; i < len; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * i / len);
            const double a = 2.0 * kPi * hz * (from + i) / kFs;
            re += w * x[from + i] * std::cos(a);
            im -= w * x[from + i] * std::sin(a);
        }
        if (re * re + im * im > best) { best = re * re + im * im; bestHz = hz; }
    }
    return bestHz;
}

double rms(const std::vector<float>& v, std::size_t from) {
    double s = 0.0;
    for (std::size_t i = from; i < v.size(); ++i) s += static_cast<double>(v[i]) * v[i];
    return std::sqrt(s / static_cast<double>(v.size() - from));
}

void testPresetMapping() {
    // Generated from preset_name() in pratt_midi_synth.py.
    const int expected[128] = {
        0,0,0,0,1,1,3,3,3,3,3,3,3,3,3,3,2,2,2,2,2,2,2,2,3,3,3,3,3,3,3,3,
        4,4,4,4,4,4,4,4,5,5,5,5,5,3,3,10,5,5,9,9,9,9,9,9,6,6,6,6,6,6,6,6,
        7,7,7,7,7,7,7,7,8,8,8,8,8,8,8,8,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,
        9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9};
    for (int p = 0; p < 128; ++p) assert(presetForProgram(p) == expected[p]);
    assert(kPresets[0].base == 5 && kPresets[9].base == 37 && kPresets[7].evenGain == 0.32);
}

void testSynthVoice() {
    Engine e(kFs);
    EngineParams p;
    p.presetOverride = 2;  // organ: sustained
    e.setParams(p);
    const std::uint8_t on[3] = {0x90, 69, 100};
    assert(e.handleMidi(on, 3));
    assert(e.activeVoices() == 1);
    const Rendered a = run(e, 22050);
    assert(peakOf(a.l, 4000) > 0.01 && peakOf(a.l) < 1.0);
    assert(std::abs(dominantHz(a.l, 8000, 8192, 420.0, 460.0) - 440.0) < 1.5);

    // Release decays to silence and frees the voice.
    e.noteOff(0, 69);
    run(e, 44100 * 3);
    assert(e.activeVoices() == 0);
    // Room tail rings out within ~1.8 s; afterwards output is silent.
    const Rendered tail = run(e, 4096);
    assert(peakOf(tail.l) < 1e-6);
}

void testPitchBend() {
    Engine e(kFs);
    EngineParams p;
    p.presetOverride = 2;
    e.setParams(p);
    e.noteOn(0, 69, 100);
    e.pitchBend(0, 16383);  // ~ +2 semitones
    run(e, 22050);          // let the smoother settle
    const Rendered a = run(e, 16384);
    const double expected = 440.0 * std::pow(2.0, 2.0 * 8191.0 / 8192.0 / 12.0);
    assert(std::abs(dominantHz(a.l, 4096, 8192, 480.0, 500.0) - expected) < 2.0);
}

void testSustainPedalAndPolyphony() {
    Engine e(kFs);
    EngineParams p;
    p.presetOverride = 2;
    e.setParams(p);
    e.controlChange(0, 64, 127);
    e.noteOn(0, 60, 90);
    e.noteOn(0, 64, 90);
    e.noteOff(0, 60);
    e.noteOff(0, 64);
    run(e, 44100 * 2);
    assert(e.activeVoices() == 2);  // held by pedal
    e.controlChange(0, 64, 0);
    run(e, 44100 * 3);
    assert(e.activeVoices() == 0);

    // Overlapping same-pitch notes are independent voices.
    e.noteOn(0, 60, 90);
    e.noteOn(0, 60, 90);
    assert(e.activeVoices() == 2);
    e.noteOff(0, 60);
    run(e, 44100 * 3);
    assert(e.activeVoices() == 1);
    e.allSoundOff();
    assert(e.activeVoices() == 0);

    // Voice cap with stealing; output stays finite and bounded.
    for (int i = 0; i < 100; ++i) e.noteOn(i % 4, 30 + i % 60, 100);
    assert(e.activeVoices() <= kMaxVoices);
    const Rendered r = run(e, 4096);
    assert(peakOf(r.l) < 1.0 && peakOf(r.r) < 1.0);
}

void testPianoDecaysByItself() {
    Engine e(kFs);
    EngineParams p;
    p.presetOverride = 0;
    e.setParams(p);
    e.noteOn(0, 60, 100);
    const Rendered a = run(e, 44100 * 14);
    assert(peakOf(a.l, 1000) > 0.0);
    assert(e.activeVoices() == 0);  // free decay passes -80 dB without a note-off
}

void testDrums() {
    Engine e(kFs);
    e.setParams(EngineParams{});
    for (int pitch : {36, 38, 42, 46, 49}) {
        e.noteOn(9, pitch, 110);
        const Rendered a = run(e, 44100 * 2);
        const double pk = peakOf(a.l);
        std::printf("drum %d peak %.3f\n", pitch, pk);
        // Python normalises each hit to 0.72 * (vel/127)^1.25 = 0.60. Here that is scaled by
        // master gain 0.5, default channel volume (100/127)^1.25 and centre pan 0.707: ~0.15.
        assert(pk > 0.10 && pk < 0.22);
        assert(e.activeVoices() == 0);
        run(e, 44100 * 2);  // clear room tail
    }
}

void testFilterMode() {
    const int frames = 44100;
    std::vector<float> sine(frames);
    const double hz = 100.0;
    for (int i = 0; i < frames; ++i) sine[i] = static_cast<float>(std::sin(2.0 * kPi * hz * i / kFs));

    EngineParams p;
    p.mode = Mode::Filter;
    p.filterIndexA = 5;
    p.filterIndexB = 7;
    p.filterCutoffHz = 220.0;
    p.filterMix = 1.0;
    Engine e(kFs);
    e.setParams(p);
    const Rendered a = run(e, frames, &sine);
    const double gain = rms(a.l, 20000) / (1.0 / std::sqrt(2.0));
    const double expected = std::abs(response(35, hz / 220.0));
    std::printf("filter gain %.4f expected %.4f\n", gain, expected);
    assert(std::abs(gain - expected) < 0.02);

    // H_m H_n == H_mn: 5*7 and 35*1 give identical output.
    EngineParams q = p;
    q.filterIndexA = 35;
    q.filterIndexB = 1;
    Engine e2(kFs);
    e2.setParams(q);
    const Rendered b = run(e2, frames, &sine);
    for (int i = 0; i < frames; ++i) assert(std::abs(a.l[i] - b.l[i]) < 1e-6f);

    // Dry passthrough at mix 0.
    EngineParams d = p;
    d.filterMix = 0.0;
    Engine e3(kFs);
    e3.setParams(d);
    const Rendered c = run(e3, frames, &sine);
    for (int i = 0; i < frames; ++i) assert(std::abs(c.l[i] - sine[i]) < 1e-6f);

    // Switching index and cutoff mid-stream crossfades without blow-up or jumps.
    Engine e4(kFs);
    e4.setParams(p);
    Rendered all{std::vector<float>(), std::vector<float>()};
    std::vector<float> chunk(1024);
    double prev = 0.0, maxJump = 0.0;
    for (int blk = 0; blk < 40; ++blk) {
        if (blk == 10) { q.filterCutoffHz = 900.0; q.filterIndexA = 97; q.filterIndexB = 3; e4.setParams(q); }
        if (blk == 25) { q.filterIndexA = 1; q.filterIndexB = 1; e4.setParams(q); }
        std::vector<float> in(sine.begin() + (blk % 20) * 1024, sine.begin() + (blk % 20) * 1024 + 1024);
        std::vector<float> ol(1024), orr(1024);
        e4.process(in.data(), in.data(), ol.data(), orr.data(), 1024);
        for (float v : ol) {
            assert(std::isfinite(v) && std::abs(v) < 4.0f);
            maxJump = std::max(maxJump, std::abs(static_cast<double>(v) - prev));
            prev = v;
        }
    }
    std::printf("max sample step across switches %.4f\n", maxJump);
    assert(maxJump < 0.2);
}

void testBothMode() {
    Engine e(kFs);
    EngineParams p;
    p.mode = Mode::Both;
    p.presetOverride = 2;
    p.filterIndexA = 3;
    p.filterIndexB = 1;
    p.filterCutoffHz = 300.0;
    e.setParams(p);
    e.noteOn(0, 69, 100);
    const Rendered a = run(e, 22050);
    assert(peakOf(a.l, 4000) > 0.005);
}

}  // namespace

int main() {
    Engine::warmCaches();
    testPresetMapping();
    testSynthVoice();
    testPitchBend();
    testSustainPedalAndPolyphony();
    testPianoDecaysByItself();
    testDrums();
    testFilterMode();
    testBothMode();
    std::puts("pratt engine tests passed");
    return 0;
}
