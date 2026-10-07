#include "paunchlad_processor.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(const bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool containsMidi(const downspout::paunchlad::ProcessResult& result,
                  const std::uint8_t status,
                  const std::uint8_t data1,
                  const std::uint8_t data2)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            result.events[i].data[0] == status &&
            result.events[i].data[1] == data1 &&
            result.events[i].data[2] == data2)
        {
            return true;
        }
    }
    return false;
}

} // namespace

// ── Session state ───────────────────────────────────────────────────────────

void testStateRoundTripsTheSettings()
{
    using namespace downspout::paunchlad;

    Processor saved;
    saved.init(48000.0);
    saved.setParameter(kParamDry, 0.31f);
    saved.setParameter(kParamWet, 0.77f);
    saved.setParameter(kParamFeedback, 0.62f);
    saved.setParameter(kParamTone, 0.18f);
    saved.setParameter(kParamSirenLevel, 0.9f);
    saved.setParameter(kParamSpring, 0.05f);
    saved.setParameter(kParamOutput, 0.44f);
    saved.setParameter(kParamLedFeedback, 0.0f);
    saved.setParameter(kParamPadMap, 2.0f);

    const std::string text = saved.serializeParameters();
    require(text.find("version=") != std::string::npos, "state should carry a version line");

    Processor reopened;
    reopened.init(48000.0);
    require(reopened.deserializeParameters(text), "a saved state should load");
    for (const std::uint32_t index : {kParamDry, kParamWet, kParamFeedback, kParamTone, kParamSirenLevel,
                                      kParamSpring, kParamOutput, kParamLedFeedback, kParamPadMap})
    {
        require(std::abs(reopened.getParameter(index) - saved.getParameter(index)) < 1.0e-5f,
                "every setting should restore");
    }
}

void testStateNeverStoresTriggers()
{
    using namespace downspout::paunchlad;

    Processor processor;
    processor.init(48000.0);
    const std::string text = processor.serializeParameters();
    for (const char* name : {"panic", "cell", "status"})
        require(text.find(name) == std::string::npos, "triggers and status must not be persisted");

    // A hand-edited state naming a pad or panic is refused rather than fired.
    Processor target;
    target.init(48000.0);
    require(!target.deserializeParameters("version=1\npanic=1\n"), "a trigger in state must be rejected");
}

void testStateRejectsGarbage()
{
    using namespace downspout::paunchlad;

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamWet, 0.66f);

    require(!processor.deserializeParameters(""), "empty state is rejected");
    require(!processor.deserializeParameters("wet=0.1\n"), "state without a version is rejected");
    require(!processor.deserializeParameters("version=99\nwet=0.1\n"), "an unknown version is rejected");
    require(!processor.deserializeParameters("version=1\nnonsense=1\n"), "an unknown key is rejected");
    require(!processor.deserializeParameters("version=1\nwet=abc\n"), "a non-numeric value is rejected");
    require(std::abs(processor.getParameter(kParamWet) - 0.66f) < 1.0e-5f,
            "a rejected state must leave the patch alone");
}

void testHostActivationKeepsTheSettings()
{
    // Regression: activate() and sampleRateChanged() both reset every setting,
    // so the mix was lost whenever a host started playback or changed rate.
    using namespace downspout::paunchlad;

    Processor processor;
    processor.init(48000.0);
    processor.setParameter(kParamWet, 0.21f);
    processor.setParameter(kParamPadMap, 3.0f);

    processor.activate();
    require(std::abs(processor.getParameter(kParamWet) - 0.21f) < 1.0e-5f, "activate must not reset the mix");
    require(processor.getParameter(kParamPadMap) == 3.0f, "activate must not reset the pad map");

    processor.setSampleRate(96000.0);
    require(std::abs(processor.getParameter(kParamWet) - 0.21f) < 1.0e-5f, "a rate change must not reset the mix");

    // The delay must have been resized for the new rate, so processing at it is safe.
    std::array<float, 256> in {};
    std::array<float, 256> outLeft {};
    std::array<float, 256> outRight {};
    for (int block = 0; block < 400; ++block)
        static_cast<void>(processor.processBlock(in.data(), in.data(), outLeft.data(), outRight.data(), 256, nullptr, 0));
}

int main()
{
    testStateRoundTripsTheSettings();
    testStateNeverStoresTriggers();
    testStateRejectsGarbage();
    testHostActivationKeepsTheSettings();
    using downspout::paunchlad::MidiMessage;
    using downspout::paunchlad::Processor;
    using downspout::paunchlad::gridToNote;
    using downspout::paunchlad::kLedPurple;
    using downspout::paunchlad::kParamLedFeedback;
    using downspout::paunchlad::kParamPadMap;
    using downspout::paunchlad::kParamSirenLevel;
    using downspout::paunchlad::kParamStatusCellStart;

    Processor processor;
    processor.init(48000.0);

    std::array<float, 512> inLeft {};
    std::array<float, 512> inRight {};
    std::array<float, 512> outLeft {};
    std::array<float, 512> outRight {};
    inLeft[0] = 1.0f;
    inRight[0] = 1.0f;

    MidiMessage alarm {};
    alarm.size = 3;
    alarm.data[0] = 0x90;
    alarm.data[1] = gridToNote(6, 6);
    alarm.data[2] = 127;

    auto result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &alarm, 1);
    require(processor.getParameter(kParamStatusCellStart + downspout::paunchlad::cellIndex(6, 6)) == 1.0f,
            "paunchlad status cell should reflect triggered visible pad 2,7 alarm");
    require(containsMidi(result, 0x90, gridToNote(6, 6), kLedPurple),
            "paunchlad LED feedback should follow Luma-style programmer mode by default");

    float alarmPeak = 0.0f;
    for (std::size_t i = 0; i < outLeft.size(); ++i)
        alarmPeak = std::max(alarmPeak, std::max(std::fabs(outLeft[i]), std::fabs(outRight[i])));
    require(alarmPeak > 0.005f, "paunchlad visible pad 2,7 should produce audible alarm output");

    processor.setParameter(kParamLedFeedback, 0.0f);
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &alarm, 1);
    require(result.eventCount == 0, "paunchlad should stop emitting LED MIDI when LED feedback is disabled");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamPadMap, 2.0f);
    MidiMessage rotatedAlarm {};
    rotatedAlarm.size = 3;
    rotatedAlarm.data[0] = 0x90;
    rotatedAlarm.data[1] = gridToNote(1, 1);
    rotatedAlarm.data[2] = 127;
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &rotatedAlarm, 1);
    require(processor.getParameter(kParamStatusCellStart + downspout::paunchlad::cellIndex(6, 6)) == 1.0f,
            "paunchlad pad map should rotate hardware input onto the UI grid");
    require(containsMidi(result, 0x90, gridToNote(1, 1), kLedPurple),
            "paunchlad pad map should rotate LED feedback back onto the hardware pad");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamSirenLevel, 0.0f);
    inLeft.fill(0.0f);
    inRight.fill(0.0f);
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &alarm, 1);
    float mutedPeak = 0.0f;
    for (std::size_t i = 0; i < outLeft.size(); ++i)
        mutedPeak = std::max(mutedPeak, std::max(std::fabs(outLeft[i]), std::fabs(outRight[i])));
    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamSirenLevel, 1.0f);
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &alarm, 1);
    float loudPeak = 0.0f;
    for (std::size_t i = 0; i < outLeft.size(); ++i)
        loudPeak = std::max(loudPeak, std::max(std::fabs(outLeft[i]), std::fabs(outRight[i])));
    require(loudPeak > mutedPeak + 0.005f, "paunchlad siren level parameter should change generated alarm output");

    processor.init(48000.0);  // back to defaults
    MidiMessage snare {};
    snare.size = 3;
    snare.data[0] = 0x90;
    snare.data[1] = gridToNote(4, 2);
    snare.data[2] = 127;
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &snare, 1);
    require(processor.getParameter(kParamStatusCellStart + downspout::paunchlad::cellIndex(4, 2)) == 1.0f,
            "paunchlad visible pad 4,3 should trigger the snare echo row");
    float snarePeak = 0.0f;
    for (std::size_t i = 0; i < outLeft.size(); ++i)
        snarePeak = std::max(snarePeak, std::max(std::fabs(outLeft[i]), std::fabs(outRight[i])));
    require(snarePeak > 0.005f, "paunchlad visible pad 4,3 should produce audible snare echo output");

    processor.init(48000.0);  // back to defaults
    processor.setParameter(kParamLedFeedback, 0.0f);
    inLeft[0] = 1.0f;
    inRight[0] = 1.0f;
    MidiMessage custom {};
    custom.size = 3;
    custom.data[0] = 0x90;
    custom.data[1] = 36 + 2 * 8 + 3;
    custom.data[2] = 127;
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &custom, 1);
    require(processor.getParameter(kParamStatusCellStart + downspout::paunchlad::cellIndex(2, 3)) == 1.0f,
            "paunchlad should map linear chromatic Launchpad-style notes when LEDs are disabled");

    processor.init(48000.0);  // back to defaults
    std::array<float, 512> dryLeft {};
    std::array<float, 512> dryRight {};
    processor.processBlock(inLeft.data(), inRight.data(), dryLeft.data(), dryRight.data(), 512, nullptr, 0);
    processor.init(48000.0);  // back to defaults
    MidiMessage drop {};
    drop.size = 3;
    drop.data[0] = 0x90;
    drop.data[1] = gridToNote(2, 0);
    drop.data[2] = 127;
    processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &drop, 1);
    require(std::fabs(outLeft[0]) < std::fabs(dryLeft[0]) * 0.5f,
            "paunchlad drop row should visibly affect incoming audio level");

    MidiMessage echo {};
    echo.size = 3;
    echo.data[0] = 0x90;
    echo.data[1] = gridToNote(0, 7);
    echo.data[2] = 127;
    result = processor.processBlock(inLeft.data(), inRight.data(), outLeft.data(), outRight.data(), 512, &echo, 1);
    require(processor.getStatus().activity > 0.0f, "paunchlad echo throw should update activity");

    return 0;
}
