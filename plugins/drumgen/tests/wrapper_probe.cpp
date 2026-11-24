// Manual probe, not a ctest: drives the real DPF wrapper the way a host does, injecting
// Conductor CCs into run() and reading the appended output parameters. Core tests never
// touch the wrapper, so run this after changing DrumgenPlugin.cpp or its parameter list.
//
//   cmake --build build --target drumgen-vst3
//   R=$PWD; B=$R/build/plugins/drumgen
//   g++ -std=gnu++20 -O1 -DDISTRHO_PLUGIN_TARGET_VST3 \
//       -I$R/third_party/DPF/distrho/src -I$R/third_party/DPF/distrho -I$R/third_party/DPF \
//       -I$R/plugins/drumgen/include -I$R/plugins/drumgen/src/dpf \
//       plugins/drumgen/tests/wrapper_probe.cpp $R/third_party/DPF/distrho/DistrhoPluginMain.cpp \
//       -o wrapper_probe $B/libdrumgen-dsp.a $B/libdownspout_drumgen_core.a -lpthread -ldl
//   ./wrapper_probe    (prints "ok")
#include "DistrhoPluginInternal.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

USE_NAMESPACE_DISTRHO

#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

namespace {

[[nodiscard]] bool near(const float a, const float b) noexcept
{
    return std::fabs(a - b) < 0.0005f;
}

[[nodiscard]] uint32_t findParam(const PluginExporter& plugin, const char* const symbol)
{
    for (uint32_t i = 0; i < plugin.getParameterCount(); ++i) {
        if (plugin.getParameterSymbol(i) == symbol)
            return i;
    }
    std::printf("FAIL: no parameter named %s\n", symbol);
    std::exit(1);
}

[[nodiscard]] MidiEvent ccEvent(const uint8_t cc, const uint8_t value) noexcept
{
    MidiEvent event {};
    event.frame = 0;
    event.size = 3;
    event.data[0] = 0xb0;  // Conductor Ch 1
    event.data[1] = cc;
    event.data[2] = value;
    return event;
}

[[nodiscard]] bool discardMidi(void*, const MidiEvent&) noexcept
{
    return true;
}

}  // namespace

int main()
{
    d_nextBufferSize = 256;
    d_nextSampleRate = 48000.0;

    PluginExporter plugin(nullptr, discardMidi, nullptr, nullptr);
    plugin.setBufferSize(256, true);
    plugin.setSampleRate(48000.0, true);

    const uint32_t density = findParam(plugin, "density");
    const uint32_t variation = findParam(plugin, "variation");
    const uint32_t vary = findParam(plugin, "vary");
    const uint32_t conductorCh = findParam(plugin, "conductor_ch");
    const uint32_t statusDensity = findParam(plugin, "status_density");
    const uint32_t statusVariation = findParam(plugin, "status_variation");
    const uint32_t statusVary = findParam(plugin, "status_vary");

    CHECK(plugin.getParameterCount() == 28);
    CHECK(density == 6 && variation == 7 && vary == 19);
    CHECK(statusDensity == 25 && statusVariation == 26 && statusVary == 27);
    CHECK(plugin.isParameterOutput(statusDensity));
    CHECK(plugin.isParameterOutput(statusVariation));
    CHECK(plugin.isParameterOutput(statusVary));
    CHECK(plugin.isParameterInteger(statusVary));
    CHECK(!plugin.isParameterOutput(density));

    // The mirrors start at, and then follow, the ordinary control values.
    CHECK(near(plugin.getParameterValue(statusDensity), 0.58f));
    CHECK(near(plugin.getParameterValue(statusVariation), 0.35f));
    CHECK(near(plugin.getParameterValue(statusVary), 0.0f));
    plugin.setParameterValue(density, 0.20f);
    plugin.setParameterValue(variation, 0.40f);
    plugin.setParameterValue(vary, 25.0f);
    CHECK(near(plugin.getParameterValue(statusDensity), 0.20f));
    CHECK(near(plugin.getParameterValue(statusVariation), 0.40f));
    CHECK(near(plugin.getParameterValue(statusVary), 25.0f));

    // Conductor CC 21-23 overwrite the effective values. Output parameters
    // carry those values to the UI; writing a status parameter must not undo them.
    plugin.setParameterValue(conductorCh, 1.0f);
    float silence[256] {};
    const float* inputs[2] = {silence, silence};
    float* outputs[2] = {silence, silence};
    const MidiEvent events[3] = {ccEvent(21, 127), ccEvent(22, 64), ccEvent(23, 32)};
    plugin.run(inputs, outputs, 256, events, 3);
    CHECK(near(plugin.getParameterValue(statusDensity), 1.0f));
    CHECK(near(plugin.getParameterValue(statusVariation), 64.0f / 127.0f));
    CHECK(near(plugin.getParameterValue(statusVary), 32.0f / 127.0f * 100.0f));
    CHECK(near(plugin.getParameterValue(density), 1.0f));
    plugin.setParameterValue(statusDensity, 0.0f);
    CHECK(near(plugin.getParameterValue(statusDensity), 1.0f));

    // With the Conductor channel off, later CCs leave the live values alone.
    plugin.setParameterValue(conductorCh, 0.0f);
    const MidiEvent ignored[1] = {ccEvent(21, 0)};
    plugin.run(inputs, outputs, 256, ignored, 1);
    CHECK(near(plugin.getParameterValue(statusDensity), 1.0f));

    std::printf("ok\n");
    return 0;
}
