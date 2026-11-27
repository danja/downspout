// Manual probe, not a ctest: drives the real DPF wrapper the way a host does, with
// four wired input ports, parameter writes and a rendered output. Core tests never
// touch the wrapper, so run this after changing VoxmodPlugin.cpp, its port groups
// or its parameter list.
//
//   cmake --build build --target voxmod-vst3
//   R=$PWD; B=$R/build/plugins/voxmod
//   g++ -std=gnu++20 -O1 -DDISTRHO_PLUGIN_TARGET_VST3 \
//       -I$R/third_party/DPF/distrho/src -I$R/third_party/DPF/distrho -I$R/third_party/DPF \
//       -I$R/plugins/voxmod/include -I$R/plugins/voxmod/src/dpf \
//       plugins/voxmod/tests/wrapper_probe.cpp $R/third_party/DPF/distrho/DistrhoPluginMain.cpp \
//       -o wrapper_probe $B/libvoxmod-dsp.a $B/libdownspout_voxmod_core.a -lpthread -ldl
//   ./wrapper_probe    (prints "ok")
#include "DistrhoPluginInternal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

USE_NAMESPACE_DISTRHO

#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1); } } while (0)

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kFrames = 256;
constexpr double kSampleRate = 48000.0;

[[nodiscard]] bool near(const float a, const float b) noexcept
{
    return std::fabs(a - b) < 0.0005f;
}

[[nodiscard]] std::uint32_t findParam(const PluginExporter& plugin, const char* const symbol)
{
    for (std::uint32_t i = 0; i < plugin.getParameterCount(); ++i) {
        if (plugin.getParameterSymbol(i) == symbol)
            return i;
    }
    std::printf("FAIL: no parameter named %s\n", symbol);
    std::exit(1);
}

[[nodiscard]] double toneMagnitude(const float* data, const std::uint32_t count,
                                   const double hz) noexcept
{
    const double coeff = 2.0 * std::cos(2.0 * kPi * hz / kSampleRate);
    double s1 = 0.0;
    double s2 = 0.0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const double s = static_cast<double>(data[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s;
    }
    const double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(count);
}

[[nodiscard]] float rms(const float* data, const std::uint32_t count) noexcept
{
    double sum = 0.0;
    for (std::uint32_t i = 0; i < count; ++i)
        sum += static_cast<double>(data[i]) * data[i];
    return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

struct Bus {
    std::vector<float> modL = std::vector<float>(kFrames);
    std::vector<float> modR = std::vector<float>(kFrames);
    std::vector<float> carrierL = std::vector<float>(kFrames);
    std::vector<float> carrierR = std::vector<float>(kFrames);
    std::vector<float> outL = std::vector<float>(kFrames);
    std::vector<float> outR = std::vector<float>(kFrames);
};

void fillSine(std::vector<float>& buffer, const double hz, const double amplitude,
              const std::uint32_t offset) noexcept
{
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        const double phase = 2.0 * kPi * hz * (offset + i) / kSampleRate;
        buffer[i] = static_cast<float>(amplitude * std::sin(phase));
    }
}

void render(PluginExporter& plugin, Bus& bus, const std::uint32_t block) noexcept
{
    const float* inputs[] = { bus.modL.data(), bus.modR.data(),
                              bus.carrierL.data(), bus.carrierR.data() };
    float* outputs[] = { bus.outL.data(), bus.outR.data() };
    plugin.run(inputs, outputs, kFrames, nullptr, 0);
}

}  // namespace

int main()
{
    d_nextBufferSize = kFrames;
    d_nextSampleRate = kSampleRate;

    PluginExporter plugin(nullptr, nullptr, nullptr, nullptr);
    plugin.setBufferSize(kFrames, true);
    plugin.setSampleRate(kSampleRate, true);

    CHECK(plugin.getParameterCount() == 35);
    CHECK(plugin.getStateCount() == 25);

    // The host must see two distinct stereo input buses and one stereo output bus.
    CHECK(plugin.getAudioPortCountWithGroupId(true, 0) == 2);
    CHECK(plugin.getAudioPortCountWithGroupId(true, 1) == 2);
    CHECK(plugin.getAudioPort(true, 0).name == "Modulator L");
    CHECK(plugin.getAudioPort(true, 3).name == "Carrier/IR R");
    CHECK(plugin.getAudioPort(false, 0).name == "Out L");
    CHECK(plugin.getAudioPort(false, 1).name == "Out R");

    const std::uint32_t mix = findParam(plugin, "mix");
    const std::uint32_t bypass = findParam(plugin, "bypass");
    const std::uint32_t ringDepth = findParam(plugin, "ring_depth");
    const std::uint32_t attackMs = findParam(plugin, "attack_ms");
    const std::uint32_t carrierSource = findParam(plugin, "carrier_source");
    const std::uint32_t stereoWidth = findParam(plugin, "stereo_width");
    const std::uint32_t convMix = findParam(plugin, "conv_mix");
    const std::uint32_t ringLevel = findParam(plugin, "ring_level");
    const std::uint32_t vocoderLevel = findParam(plugin, "vocoder_level");
    const std::uint32_t convLevel = findParam(plugin, "conv_level");
    const std::uint32_t outCarrierHz = findParam(plugin, "out_carrier_hz");
    const std::uint32_t outModLevel = findParam(plugin, "out_mod_level");
    const std::uint32_t outIrLevel = findParam(plugin, "out_ir_level");

    CHECK(plugin.isParameterOutput(outCarrierHz));
    CHECK(plugin.isParameterOutput(outModLevel));
    CHECK(plugin.isParameterOutput(outIrLevel));
    CHECK(near(plugin.getParameterValue(outModLevel), -60.0f));
    CHECK(near(plugin.getParameterValue(outIrLevel), -60.0f));
    CHECK(!plugin.isParameterOutput(mix));
    CHECK(near(plugin.getParameterValue(mix), 50.0f));
    CHECK(near(plugin.getParameterValue(bypass), 0.0f));
    CHECK(near(plugin.getParameterValue(convMix), 0.0f));
    CHECK(near(plugin.getParameterValue(ringLevel), 100.0f));
    CHECK(near(plugin.getParameterValue(vocoderLevel), 100.0f));
    CHECK(near(plugin.getParameterValue(convLevel), 100.0f));

    // Vocoder: the 1/2 tone must be resynthesised from the envelope of 3/4.
    plugin.setParameterValue(mix, 100.0f);
    plugin.setParameterValue(ringDepth, 0.0f);
    plugin.setParameterValue(carrierSource, 0.0f);
    plugin.setParameterValue(convMix, 0.0f);
    plugin.activate();

    Bus bus;
    for (std::uint32_t block = 0; block < 64; ++block) {
        fillSine(bus.modL, 220.0, 0.4, block * kFrames);
        fillSine(bus.modR, 220.0, 0.4, block * kFrames);
        fillSine(bus.carrierL, 900.0, 0.6, block * kFrames);
        fillSine(bus.carrierR, 900.0, 0.6, block * kFrames);
        render(plugin, bus, block);
    }
    const double sourceTone = toneMagnitude(bus.outL.data(), kFrames, 220.0);
    const double carrierTone = toneMagnitude(bus.outL.data(), kFrames, 900.0);
    CHECK(sourceTone > 0.01);
    CHECK(sourceTone > carrierTone * 3.0);
    CHECK(plugin.getParameterValue(outCarrierHz) > 0.0f);
    CHECK(plugin.getParameterValue(outModLevel) > -20.0f);
    CHECK(plugin.getParameterValue(outIrLevel) > -20.0f);

    // A second bus that a host has not routed must make the vocoder quiet.
    PluginExporter oneBus(nullptr, nullptr, nullptr, nullptr);
    oneBus.setBufferSize(kFrames, true);
    oneBus.setSampleRate(kSampleRate, true);
    oneBus.setParameterValue(mix, 100.0f);
    oneBus.setParameterValue(ringDepth, 0.0f);
    oneBus.setParameterValue(carrierSource, 0.0f);
    oneBus.setParameterValue(convMix, 0.0f);
    oneBus.activate();
    Bus silentCarrier;
    for (std::uint32_t block = 0; block < 64; ++block) {
        fillSine(silentCarrier.modL, 220.0, 0.4, block * kFrames);
        fillSine(silentCarrier.modR, 220.0, 0.4, block * kFrames);
        render(oneBus, silentCarrier, block);
    }
    CHECK(rms(silentCarrier.outL.data(), kFrames) < 1.0e-5f);
    CHECK(oneBus.getParameterValue(outModLevel) > -20.0f);
    CHECK(oneBus.getParameterValue(outIrLevel) <= -59.5f);

    // Internal carrier keeps the vocoder audible when only 1/2 is routed.
    oneBus.setParameterValue(carrierSource, 1.0f);
    for (std::uint32_t block = 0; block < 64; ++block) {
        fillSine(silentCarrier.modL, 220.0, 0.4, block * kFrames);
        fillSine(silentCarrier.modR, 220.0, 0.4, block * kFrames);
        render(oneBus, silentCarrier, block);
    }
    CHECK(rms(silentCarrier.outL.data(), kFrames) > 0.001f);

    // Convolution: both buses are required, and Conv Mix must be opened.
    PluginExporter conv(nullptr, nullptr, nullptr, nullptr);
    conv.setBufferSize(kFrames, true);
    conv.setSampleRate(kSampleRate, true);
    conv.setParameterValue(convMix, 100.0f);
    conv.setParameterValue(convLevel, 100.0f);
    conv.activate();

    Bus withIr;
    for (std::uint32_t block = 0; block < 32; ++block) {
        fillSine(withIr.modL, 440.0, 0.4, block * kFrames);
        fillSine(withIr.modR, 440.0, 0.4, block * kFrames);
        fillSine(withIr.carrierL, 330.0, 0.4, block * kFrames);
        fillSine(withIr.carrierR, 330.0, 0.4, block * kFrames);
        render(conv, withIr, block);
    }
    CHECK(rms(withIr.outL.data(), kFrames) > 0.001f);

    PluginExporter noIr(nullptr, nullptr, nullptr, nullptr);
    noIr.setBufferSize(kFrames, true);
    noIr.setSampleRate(kSampleRate, true);
    noIr.setParameterValue(convMix, 100.0f);
    noIr.setParameterValue(convLevel, 100.0f);
    noIr.activate();
    Bus silentIr;
    for (std::uint32_t block = 0; block < 32; ++block) {
        fillSine(silentIr.modL, 440.0, 0.4, block * kFrames);
        fillSine(silentIr.modR, 440.0, 0.4, block * kFrames);
        render(noIr, silentIr, block);
    }
    CHECK(rms(silentIr.outL.data(), kFrames) < 1.0e-6f);

    // Ring: with the default Carrier = input 3/4, the wrapper must feed that
    // bus into the ring multiplier. Full depth/width makes the output the exact
    // per-channel product; the internal oscillator would not match it.
    PluginExporter ring(nullptr, nullptr, nullptr, nullptr);
    ring.setBufferSize(kFrames, true);
    ring.setSampleRate(kSampleRate, true);
    ring.setParameterValue(mix, 0.0f);
    ring.setParameterValue(bypass, 0.0f);
    ring.setParameterValue(ringDepth, 100.0f);
    ring.setParameterValue(attackMs, 200.0f);
    ring.setParameterValue(carrierSource, 0.0f);
    ring.setParameterValue(stereoWidth, 100.0f);
    ring.setParameterValue(convMix, 0.0f);
    ring.activate();

    Bus externalRing;
    for (std::uint32_t block = 0; block < 4; ++block) {
        fillSine(externalRing.modL, 300.0, 0.4, block * kFrames);
        fillSine(externalRing.modR, 500.0, 0.35, block * kFrames);
        fillSine(externalRing.carrierL, 700.0, 0.08, block * kFrames);
        fillSine(externalRing.carrierR, 900.0, 0.07, block * kFrames);
        render(ring, externalRing, block);
    }
    CHECK(rms(externalRing.outL.data(), kFrames) > 0.01f);
    for (std::uint32_t i = 0; i < kFrames; ++i) {
        CHECK(near(externalRing.outL[i], externalRing.modL[i] * externalRing.carrierL[i]));
        CHECK(near(externalRing.outR[i], externalRing.modR[i] * externalRing.carrierR[i]));
    }

    std::printf("ok\n");
    return 0;
}
