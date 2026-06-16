#include "drumkit_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <utility>

namespace {

float renderEnergy(downspout::drumkit::Engine& engine, const int frames)
{
    float energy = 0.0f;
    for (int i = 0; i < frames; ++i)
    {
        const auto frame = engine.processStereo();
        energy += std::fabs(frame.left) + std::fabs(frame.right);
    }
    return energy;
}

void noteOn(downspout::drumkit::Engine& engine, const std::uint8_t note, const std::uint8_t velocity = 110)
{
    const std::uint8_t data[] = {0x99, note, velocity};
    engine.handleMidi(data, 3);
}

void allNotesOff(downspout::drumkit::Engine& engine)
{
    const std::uint8_t data[] = {0xb0, 123, 0};
    engine.handleMidi(data, 3);
}

void kickRendersAudio()
{
    downspout::drumkit::Engine engine {48000.0f};
    noteOn(engine, 36);
    assert(renderEnergy(engine, 512) > 0.01f);
}

void mutedKickDoesNotTrigger()
{
    downspout::drumkit::Engine engine {48000.0f};
    engine.setInstrumentMuted(downspout::drumkit::InstrumentId::Kick, true);
    noteOn(engine, 36);
    assert(renderEnergy(engine, 512) < 0.0001f);

    engine.setInstrumentMuted(downspout::drumkit::InstrumentId::Kick, false);
    noteOn(engine, 36);
    assert(renderEnergy(engine, 512) > 0.01f);
}

void kickTransientAddsAttackEnergy()
{
    downspout::drumkit::Engine dry {48000.0f};
    dry.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
    dry.setParameter(downspout::drumkit::kParamKickTransient, 0.0f);
    noteOn(dry, 36);
    const float dryAttack = renderEnergy(dry, 96);

    downspout::drumkit::Engine transient {48000.0f};
    transient.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
    transient.setParameter(downspout::drumkit::kParamKickTransient, 1.0f);
    noteOn(transient, 36);
    const float transientAttack = renderEnergy(transient, 96);

    assert(transientAttack > dryAttack * 1.2f);
}

void closedHatChokesOpenHatEvenWhenClosedHatMuted()
{
    downspout::drumkit::Engine engine {48000.0f};
    engine.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
    engine.setInstrumentMuted(downspout::drumkit::InstrumentId::ClosedHH, true);

    noteOn(engine, 46);
    const float openEnergy = renderEnergy(engine, 96);
    noteOn(engine, 42);
    const float postChokeEnergy = renderEnergy(engine, 512);

    assert(openEnergy > 0.001f);
    assert(postChokeEnergy < openEnergy * 5.0f);
}

void allNotesOffStopsVoices()
{
    downspout::drumkit::Engine engine {48000.0f};
    engine.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
    noteOn(engine, 51);
    assert(renderEnergy(engine, 64) > 0.001f);
    allNotesOff(engine);
    assert(renderEnergy(engine, 256) < 0.0001f);
}

void metalDefaultsToDryTone()
{
    constexpr std::array<std::pair<std::uint8_t, std::uint32_t>, 9> targets {{
        {39, downspout::drumkit::kParamClapMetal},
        {40, downspout::drumkit::kParamSnareMetal},
        {41, downspout::drumkit::kParamCrashMetal},
        {42, downspout::drumkit::kParamClosedHHMetal},
        {45, downspout::drumkit::kParamTom1Metal},
        {46, downspout::drumkit::kParamOpenHHMetal},
        {50, downspout::drumkit::kParamTom2Metal},
        {52, downspout::drumkit::kParamCowbellMetal},
        {53, downspout::drumkit::kParamClaveMetal},
    }};

    for (const auto& target : targets)
    {
        downspout::drumkit::Engine defaultMetal {48000.0f};
        defaultMetal.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
        noteOn(defaultMetal, target.first);
        const float defaultEnergy = renderEnergy(defaultMetal, 768);

        downspout::drumkit::Engine explicitZero {48000.0f};
        explicitZero.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
        explicitZero.setParameter(target.second, 0.0f);
        noteOn(explicitZero, target.first);
        const float zeroEnergy = renderEnergy(explicitZero, 768);

        assert(std::fabs(defaultEnergy - zeroEnergy) < 0.000001f);
    }
}

void metalChangesTargetVoices()
{
    constexpr std::array<std::pair<std::uint8_t, std::uint32_t>, 9> targets {{
        {39, downspout::drumkit::kParamClapMetal},
        {40, downspout::drumkit::kParamSnareMetal},
        {41, downspout::drumkit::kParamCrashMetal},
        {42, downspout::drumkit::kParamClosedHHMetal},
        {45, downspout::drumkit::kParamTom1Metal},
        {46, downspout::drumkit::kParamOpenHHMetal},
        {50, downspout::drumkit::kParamTom2Metal},
        {52, downspout::drumkit::kParamCowbellMetal},
        {53, downspout::drumkit::kParamClaveMetal},
    }};

    for (const auto& target : targets)
    {
        downspout::drumkit::Engine dry {48000.0f};
        dry.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
        dry.setParameter(target.second, 0.0f);
        noteOn(dry, target.first);

        downspout::drumkit::Engine metal {48000.0f};
        metal.setParameter(downspout::drumkit::kParamMasterReverb, 0.0f);
        metal.setParameter(target.second, 1.0f);
        noteOn(metal, target.first);

        float difference = 0.0f;
        for (int i = 0; i < 768; ++i)
        {
            const auto dryFrame = dry.processStereo();
            const auto metalFrame = metal.processStereo();
            difference += std::fabs(dryFrame.left - metalFrame.left);
            difference += std::fabs(dryFrame.right - metalFrame.right);
        }
        assert(difference > 0.05f);
    }
}

} // namespace

int main()
{
    kickRendersAudio();
    mutedKickDoesNotTrigger();
    kickTransientAddsAttackEnergy();
    closedHatChokesOpenHatEvenWhenClosedHatMuted();
    allNotesOffStopsVoices();
    metalDefaultsToDryTone();
    metalChangesTargetVoices();

    std::cout << "drumkit core tests passed\n";
    return 0;
}
