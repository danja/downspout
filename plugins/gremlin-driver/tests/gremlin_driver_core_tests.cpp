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

    processor.triggerRandomize();
    const auto randomized = processor.processBlock(64, TransportSnapshot {}, nullptr, 0);
    require(randomized.eventCount >= 24, "gremlin-driver randomize should emit direct patch CCs");

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

    return 0;
}
