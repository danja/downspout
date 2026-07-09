#include "gremlin_processor.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

struct ModeSignature {
    float rms = 0.0f;
    float zeroCrossRate = 0.0f;
    float sideRatio = 0.0f;
    float peak = 0.0f;
    float roughness = 0.0f;
};

bool nearlyEqual(const float a, const float b, const float epsilon = 1.0e-5f)
{
    return std::fabs(a - b) <= epsilon;
}

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool containsMidi(const downspout::gremlin::MidiMessage* events,
                  const std::uint32_t count,
                  const std::uint8_t status,
                  const std::uint8_t data1,
                  const std::uint8_t data2)
{
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if (events[i].size == 3 &&
            events[i].data[0] == status &&
            events[i].data[1] == data1 &&
            events[i].data[2] == data2)
        {
            return true;
        }
    }
    return false;
}

ModeSignature measureRenderedSignature(const std::array<float, 4096>& left, const std::array<float, 4096>& right)
{
    ModeSignature signature {};
    int crossings = 0;
    int samples = 0;
    float energy = 0.0f;
    float side = 0.0f;
    float roughness = 0.0f;
    float previous = 0.0f;
    for (std::size_t i = 1024; i < left.size(); ++i)
    {
        const float mono = 0.5f * (left[i] + right[i]);
        if (samples > 0 && ((previous < 0.0f && mono >= 0.0f) || (previous > 0.0f && mono <= 0.0f)))
            ++crossings;
        if (samples > 0)
            roughness += std::fabs(mono - previous);
        previous = mono;

        energy += mono * mono;
        side += std::fabs(left[i] - right[i]);
        signature.peak = std::max(signature.peak, std::max(std::fabs(left[i]), std::fabs(right[i])));
        ++samples;
    }

    signature.rms = samples > 0 ? std::sqrt(energy / static_cast<float>(samples)) : 0.0f;
    signature.zeroCrossRate = samples > 0 ? static_cast<float>(crossings) / static_cast<float>(samples) : 0.0f;
    signature.sideRatio = energy > 0.0f ? side / (std::sqrt(energy) * static_cast<float>(samples)) : 0.0f;
    signature.roughness = samples > 0 ? roughness / static_cast<float>(samples) : 0.0f;
    return signature;
}

ModeSignature renderModeSignature(const std::size_t mode)
{
    using downspout::gremlin::LiveParamId;
    using downspout::gremlin::MidiMessage;
    using downspout::gremlin::Processor;

    Processor processor;
    processor.init(48000.0);
    processor.setLiveParameter(LiveParamId::mode, static_cast<float>(mode));

    MidiMessage noteOn {};
    noteOn.size = 3;
    noteOn.data[0] = 0x90;
    noteOn.data[1] = 60;
    noteOn.data[2] = 108;

    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    processor.processBlock(left.data(), right.data(), static_cast<std::uint32_t>(left.size()), &noteOn, 1);

    return measureRenderedSignature(left, right);
}

ModeSignature renderSceneSignature(const downspout::gremlin::SceneId scene)
{
    using downspout::gremlin::MidiMessage;
    using downspout::gremlin::Processor;

    Processor processor;
    processor.init(48000.0);
    processor.loadScene(scene);

    MidiMessage noteOn {};
    noteOn.size = 3;
    noteOn.data[0] = 0x90;
    noteOn.data[1] = 60;
    noteOn.data[2] = 112;

    std::array<float, 4096> left {};
    std::array<float, 4096> right {};
    processor.processBlock(left.data(), right.data(), static_cast<std::uint32_t>(left.size()), &noteOn, 1);
    return measureRenderedSignature(left, right);
}

float signatureDistance(const ModeSignature& a, const ModeSignature& b)
{
    const float rmsA = std::max(a.rms, 1.0e-6f);
    const float rmsB = std::max(b.rms, 1.0e-6f);
    return std::fabs(std::log(rmsA / rmsB)) * 0.35f
         + std::fabs(a.zeroCrossRate - b.zeroCrossRate) * 2.2f
         + std::fabs(a.sideRatio - b.sideRatio) * 1.4f
         + std::fabs(a.peak - b.peak) * 0.35f
         + std::fabs(a.roughness - b.roughness) * 18.0f;
}

}  // namespace

int main()
{
    using downspout::gremlin::ActionId;
    using downspout::gremlin::LiveParamId;
    using downspout::gremlin::MidiMessage;
    using downspout::gremlin::Processor;
    using downspout::gremlin::SceneId;

    Processor processor;
    processor.init(48000.0);

    require(nearlyEqual(processor.getLiveParameter(LiveParamId::mode), 0.0f), "gremlin default mode mismatch");

    processor.loadScene(SceneId::rust);
    require(nearlyEqual(processor.getLiveParameter(LiveParamId::mode), 2.0f), "gremlin rust scene mode mismatch");
    require(processor.getStatus().currentScene == static_cast<std::uint32_t>(SceneId::rust), "gremlin rust scene status mismatch");

    processor.triggerAction(ActionId::panic);
    require(!processor.getMomentary(downspout::gremlin::MomentaryId::freeze), "gremlin panic should clear momentaries");

    MidiMessage cc {};
    cc.size = 3;
    cc.data[0] = 0xB0;
    cc.data[1] = downspout::gremlin::kMacroFaderCCs[0];
    cc.data[2] = 127;
    float left[16] {};
    float right[16] {};
    processor.processBlock(left, right, 16, &cc, 1);
    require(nearlyEqual(processor.getMacro(downspout::gremlin::MacroId::source), 1.0f), "gremlin macro CC mapping mismatch");

    std::array<MidiMessage, Processor::kMaxOutputMidiEvents> ledEvents {};
    std::uint32_t ledEventCount = 0;
    processor.processBlock(left, right, 16, nullptr, 0, ledEvents.data(), &ledEventCount, ledEvents.size());
    require(containsMidi(ledEvents.data(), ledEventCount, 0x90, downspout::gremlin::kRecArmNotes[2], 127),
            "gremlin LED feedback should light current mode");

    MidiMessage muteDown {};
    muteDown.size = 3;
    muteDown.data[0] = 0x90;
    muteDown.data[1] = downspout::gremlin::kMuteNotes[0];
    muteDown.data[2] = 127;
    ledEventCount = 0;
    processor.processBlock(left, right, 16, &muteDown, 1, ledEvents.data(), &ledEventCount, ledEvents.size());
    require(containsMidi(ledEvents.data(), ledEventCount, 0x90, downspout::gremlin::kMuteNotes[0], 127),
            "gremlin LED feedback should light held momentary");

    processor.setLiveParameter(LiveParamId::mode, 4.0f);
    ledEventCount = 0;
    processor.processBlock(left, right, 16, nullptr, 0, ledEvents.data(), &ledEventCount, ledEvents.size());
    require(containsMidi(ledEvents.data(), ledEventCount, 0x90, downspout::gremlin::kBankLeftNote, 127),
            "gremlin LED feedback should indicate extended mode on bank LED");

    MidiMessage noteOn {};
    noteOn.size = 3;
    noteOn.data[0] = 0x90;
    noteOn.data[1] = 60;
    noteOn.data[2] = 100;

    float synthLeft[512] {};
    float synthRight[512] {};
    processor.processBlock(synthLeft, synthRight, 512, &noteOn, 1);

    float peak = 0.0f;
    for (int i = 0; i < 512; ++i)
        peak = std::max(peak, std::max(std::fabs(synthLeft[i]), std::fabs(synthRight[i])));

    require(peak > 0.01f, "gremlin should emit audio after note-on");

    for (std::size_t mode = 0; mode < downspout::gremlin::kModeCount; ++mode)
    {
        Processor modeProcessor;
        modeProcessor.init(48000.0);
        modeProcessor.setLiveParameter(LiveParamId::mode, static_cast<float>(mode));
        modeProcessor.processBlock(synthLeft, synthRight, 512, &noteOn, 1);

        float modePeak = 0.0f;
        for (int i = 0; i < 512; ++i)
            modePeak = std::max(modePeak, std::max(std::fabs(synthLeft[i]), std::fabs(synthRight[i])));

        require(modePeak > 0.005f, "gremlin mode should emit audio after note-on");
    }

    std::array<ModeSignature, downspout::gremlin::kModeCount> signatures {};
    for (std::size_t mode = 0; mode < downspout::gremlin::kModeCount; ++mode)
        signatures[mode] = renderModeSignature(mode);

    for (std::size_t mode = 0; mode < downspout::gremlin::kModeCount; ++mode)
    {
        float nearest = 999.0f;
        std::size_t nearestMode = mode;
        for (std::size_t other = 0; other < downspout::gremlin::kModeCount; ++other)
        {
            if (mode == other)
                continue;
            const float distance = signatureDistance(signatures[mode], signatures[other]);
            if (distance < nearest)
            {
                nearest = distance;
                nearestMode = other;
            }
        }
        if (nearest <= 0.035f)
            std::cerr << "nearest signature distance for mode " << mode << " was " << nearest
                      << " against mode " << nearestMode << "\nmode " << mode
                      << " rms=" << signatures[mode].rms
                      << " zc=" << signatures[mode].zeroCrossRate
                      << " side=" << signatures[mode].sideRatio
                      << " peak=" << signatures[mode].peak
                      << " rough=" << signatures[mode].roughness
                      << "\nmode " << nearestMode
                      << " rms=" << signatures[nearestMode].rms
                      << " zc=" << signatures[nearestMode].zeroCrossRate
                      << " side=" << signatures[nearestMode].sideRatio
                      << " peak=" << signatures[nearestMode].peak
                      << " rough=" << signatures[nearestMode].roughness << '\n';
        require(nearest > 0.035f, "gremlin mode signatures should remain separated");
    }

    const ModeSignature musicalScene = renderSceneSignature(SceneId::melt);
    const ModeSignature extremeScene = renderSceneSignature(SceneId::tunnel);
    require(musicalScene.peak > 0.005f, "gremlin musical scene should emit audio");
    require(extremeScene.peak > 0.005f, "gremlin extreme scene should emit audio");
    require(signatureDistance(musicalScene, extremeScene) > 0.08f,
            "gremlin musical and extreme scenes should remain audibly separated");

    float monoOnly[128] {};
    processor.processBlock(monoOnly, nullptr, 128, nullptr, 0);
    float monoPeak = 0.0f;
    for (float sample : monoOnly)
        monoPeak = std::max(monoPeak, std::fabs(sample));

    require(monoPeak > 0.001f, "gremlin mono-output render should still emit audio");

    return 0;
}
