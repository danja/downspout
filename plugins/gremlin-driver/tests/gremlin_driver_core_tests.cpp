#include "gremlin_driver_processor.hpp"

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

int ccValueFor(const downspout::gremlin_driver::ProcessResult& result, const std::uint8_t cc)
{
    for (std::uint32_t i = 0; i < result.eventCount; ++i)
    {
        if (result.events[i].size == 3 &&
            result.events[i].data[0] == 0xB0 &&
            result.events[i].data[1] == cc)
        {
            return result.events[i].data[2];
        }
    }
    return -1;
}

}  // namespace

int main()
{
    using downspout::gremlin_driver::MidiMessage;
    using downspout::gremlin_driver::Processor;
    using downspout::gremlin_driver::TransportSnapshot;

    Processor processor;
    processor.init(48000.0);

    require(processor.getClockMode() == 1, "gremlin-driver default clock mode mismatch");
    require(nearlyEqual(processor.getBpm(), 120.0f), "gremlin-driver default bpm mismatch");
    require(processor.getPassInput(), "gremlin-driver should pass input MIDI by default");
    require(processor.getLane(0).target == 2, "gremlin-driver lane 1 should default to pitch");
    require(processor.getLane(1).target == 3, "gremlin-driver lane 2 should default to breakage");
    require(processor.getLane(2).target == 5, "gremlin-driver lane 3 should default to space");
    require(processor.getLane(3).target == 6, "gremlin-driver lane 4 should default to stutter");
    require(processor.getTrigger(1).action == 5, "gremlin-driver trigger 2 should default to random-all");

    for (std::size_t shape = 0; shape < downspout::gremlin_driver::kShapeCount; ++shape)
    {
        processor.setLaneShape(0, static_cast<int>(shape));
        const auto shaped = processor.processBlock(256, TransportSnapshot {}, nullptr, 0);
        require(processor.getLane(0).shape == static_cast<int>(shape), "gremlin-driver lane shape should be settable");
        require(std::isfinite(shaped.status.laneValues[0]), "gremlin-driver lane shape should produce finite status");
    }

    processor.triggerRandomize();
    const auto randomized = processor.processBlock(64, TransportSnapshot {}, nullptr, 0);
    require(randomized.eventCount >= 24, "gremlin-driver randomize should emit direct patch CCs");
    require(ccValueFor(randomized, 16) >= 18 && ccValueFor(randomized, 16) <= 120,
            "gremlin-driver randomize damage CC should use expanded range");
    require(ccValueFor(randomized, 50) >= 8 && ccValueFor(randomized, 50) <= 126,
            "gremlin-driver randomize fold CC should use expanded range");
    require(ccValueFor(randomized, 26) >= 18 && ccValueFor(randomized, 26) <= 127,
            "gremlin-driver randomize pitch-spread CC should use expanded range");
    require(ccValueFor(randomized, 52) >= 10 && ccValueFor(randomized, 52) <= 108,
            "gremlin-driver randomize glitch-length CC should use expanded range");

    MidiMessage note {};
    note.frame = 7;
    note.size = 3;
    note.data[0] = 0x90;
    note.data[1] = 60;
    note.data[2] = 100;
    const auto passthrough = processor.processBlock(32, TransportSnapshot {}, &note, 1);
    bool foundPassThrough = false;
    for (std::uint32_t i = 0; i < passthrough.eventCount; ++i)
    {
        if (passthrough.events[i].size == 3 &&
            passthrough.events[i].frame == 7 &&
            passthrough.events[i].data[0] == 0x90 &&
            passthrough.events[i].data[1] == 60)
        {
            foundPassThrough = true;
            break;
        }
    }
    require(foundPassThrough, "gremlin-driver should pass through input MIDI");

    processor.setPassInput(false);
    const auto blocked = processor.processBlock(32, TransportSnapshot {}, &note, 1);
    bool foundBlockedInput = false;
    for (std::uint32_t i = 0; i < blocked.eventCount; ++i)
    {
        if (blocked.events[i].size == 3 &&
            blocked.events[i].frame == 7 &&
            blocked.events[i].data[0] == 0x90 &&
            blocked.events[i].data[1] == 60)
        {
            foundBlockedInput = true;
            break;
        }
    }
    require(!foundBlockedInput, "gremlin-driver pass input switch should block incoming MIDI");

    return 0;
}
