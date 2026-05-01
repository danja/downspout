#include "gremlin_processor.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

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

    float monoOnly[128] {};
    processor.processBlock(monoOnly, nullptr, 128, nullptr, 0);
    float monoPeak = 0.0f;
    for (float sample : monoOnly)
        monoPeak = std::max(monoPeak, std::fabs(sample));

    require(monoPeak > 0.001f, "gremlin mono-output render should still emit audio");

    return 0;
}
