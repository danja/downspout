// Manual probe, not a ctest: drives the real DPF wrapper (NamPlugin) through PluginExporter the way
// a host does (state restore, activate/deactivate, sample-rate change, clearing and bad files). The core
// tests never touch the wrapper, so run this after changing NamPlugin.cpp or its lifecycle.
//
//   cmake --build build --target nam-vst3 downspout_nam_core_tests    (the tests leave /tmp/downspout_nam_tests/nam_test_delta.wav)
//   R=$PWD; B=$R/build/plugins/nam
//   g++ -std=c++20 -O1 -DDGL_USE_FILE_BROWSER -DDISTRHO_PLUGIN_TARGET_VST3 \
//       -I$R/third_party/DPF/distrho/src -I$R/third_party/DPF/distrho -I$R/third_party/DPF -I$R/plugins/nam/src/dpf \
//       plugins/nam/tests/wrapper_probe.cpp $R/third_party/DPF/distrho/DistrhoPluginMain.cpp -o wrapper_probe \
//       -Wl,--whole-archive $B/libnam-dsp.a $B/libdownspout_nam_vendor.a -Wl,--no-whole-archive \
//       $B/libdownspout_nam_core.a -lpthread -ldl
//   ./wrapper_probe plugins/nam/tests/data/wavenet.nam ${TMPDIR:-/tmp}/downspout_nam_tests/nam_test_delta.wav     (prints "ok")
#include "DistrhoPluginInternal.hpp"
#include <cmath>
#include <cstdio>
#include <vector>
#include <cstdlib>
USE_NAMESPACE_DISTRHO
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1);} } while(0)

int main(int argc, char** argv) {
    const char* model = argv[1];
    const char* ir = argv[2];
    printf("start\n");
    d_nextBufferSize = 256; d_nextSampleRate = 48000.0;
    PluginExporter p(nullptr, nullptr, nullptr, nullptr);
    p.setBufferSize(256, true); p.setSampleRate(48000.0, true);
    printf("params %u states %u\n", p.getParameterCount(), p.getStateCount());
    p.setState("model_file", model);
    p.setState("ir_file", ir);
    uint32_t statusModel=0,statusIr=0;
    for (uint32_t i=0;i<p.getParameterCount();++i){ String s=p.getParameterSymbol(i); if (s=="status_model") statusModel=i; if (s=="status_ir") statusIr=i; }
    printf("model status %g ir status %g\n", p.getParameterValue(statusModel), p.getParameterValue(statusIr));
    CHECK(p.getParameterValue(statusModel)==1.0f && p.getParameterValue(statusIr)==1.0f);
    printf("state model=%s ir=%s\n", p.getStateValue("model_file").buffer(), p.getStateValue("ir_file").buffer());
    p.activate();
    printf("latency %u\n", p.getLatency());
    // 1 s of audio through the exporter
    std::vector<float> inA(256), inB(256), outA(256), outB(256);
    const float* ins[2] = {inA.data(), inB.data()};
    float* outs[2] = {outA.data(), outB.data()};
    double energy = 0;
    for (int b = 0; b < 190; ++b) {
        for (int i = 0; i < 256; ++i) { float v = 0.3f*std::sin(2*3.14159265*110.0*(b*256+i)/48000.0); inA[i]=inB[i]=v; }
        p.run(ins, outs, 256);
        if (b > 100) for (int i=0;i<256;++i) energy += outA[i]*outA[i];
    }
    printf("energy %g\n", energy);
    CHECK(energy > 1e-3);
    uint32_t outLevel=0; for (uint32_t i=0;i<p.getParameterCount();++i){ if (String(p.getParameterSymbol(i))=="out_level") outLevel=i; }
    printf("out_level %g\n", p.getParameterValue(outLevel));
    CHECK(p.getParameterValue(outLevel) > -60.0f);

    // reactivation keeps the patch
    p.deactivate(); p.activate();
    double e2=0;
    for (int b = 0; b < 100; ++b) { for (int i=0;i<256;++i){float v=0.3f*std::sin(2*3.14159265*110.0*(b*256+i)/48000.0); inA[i]=inB[i]=v;} p.run(ins, outs, 256); if (b>50) for (int i=0;i<256;++i) e2+=outA[i]*outA[i]; }
    printf("after reactivate energy %g\n", e2);
    CHECK(e2 > 1e-3);
    CHECK(p.getParameterValue(statusModel)==1.0f);

    // sample rate change rebuilds for the new rate
    p.deactivate(); p.setSampleRate(44100.0, true); p.activate();
    printf("after 44.1k: status %g latency %u\n", p.getParameterValue(statusModel), p.getLatency());
    CHECK(p.getParameterValue(statusModel)==1.0f);
    for (int b = 0; b < 100; ++b) p.run(ins, outs, 256);
    // clearing the model
    p.setState("model_file", "");
    printf("cleared status %g latency %u\n", p.getParameterValue(statusModel), p.getLatency());
    CHECK(p.getParameterValue(statusModel)==0.0f);
    p.setState("model_file", "/nonexistent.nam");
    CHECK(p.getParameterValue(statusModel)==2.0f);
    printf("ok\n");
    return 0;
}
