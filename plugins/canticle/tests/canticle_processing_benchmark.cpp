#include "canticle_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace {

using Clock = std::chrono::steady_clock;
using downspout::canticle::CanticleEngine;
using downspout::canticle::ParamId;
using downspout::canticle::kModelNames;

constexpr int kFramesPerBlock = 1024;
constexpr int kWarmupBlocks = 16;
constexpr int kMeasuredBlocks = 256;
constexpr float kSampleRate = 48000.0f;

void startVoices(CanticleEngine& engine, const int voiceCount)
{
    for (int voice = 0; voice < voiceCount; ++voice)
        engine.noteOn(43 + voice * 3, static_cast<std::uint8_t>(96 + (voice % 4) * 7));
}

void configureStress(CanticleEngine& engine, const int model)
{
    engine.setParameter(ParamId::model, static_cast<float>(model));
    engine.setParameter(ParamId::articulation, 3.0f);
    engine.setParameter(ParamId::ensemble, 3.0f);
    engine.setParameter(ParamId::movement, 1.0f);
    engine.setParameter(ParamId::metal, 1.0f);
    engine.setParameter(ParamId::drive, 0.8f);
}

double renderBlock(CanticleEngine& engine, volatile float& checksum)
{
    const auto started = Clock::now();
    for (int frame = 0; frame < kFramesPerBlock; ++frame)
    {
        const auto output = engine.processStereo();
        checksum = checksum + output.left * 0.37f + output.right * 0.63f;
    }
    return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
}

void benchmark(const int model, const int voiceCount, volatile float& checksum)
{
    CanticleEngine engine {kSampleRate};
    configureStress(engine, model);
    startVoices(engine, voiceCount);

    for (int block = 0; block < kWarmupBlocks; ++block)
        (void)renderBlock(engine, checksum);

    double totalMs = 0.0;
    double maximumMs = 0.0;
    for (int block = 0; block < kMeasuredBlocks; ++block)
    {
        if (block > 0 && block % 64 == 0)
            startVoices(engine, voiceCount);
        const double elapsedMs = renderBlock(engine, checksum);
        totalMs += elapsedMs;
        maximumMs = std::max(maximumMs, elapsedMs);
    }

    std::cout << std::left << std::setw(7) << kModelNames[static_cast<std::size_t>(model)]
              << std::right << std::setw(7) << voiceCount
              << std::setw(12) << std::fixed << std::setprecision(3)
              << totalMs / kMeasuredBlocks
              << std::setw(12) << maximumMs << '\n';
}

void benchmarkGlassPair(const int motionVoices,
                        const int chordVoices,
                        volatile float& checksum)
{
    CanticleEngine motion {kSampleRate};
    CanticleEngine chords {kSampleRate};
    configureStress(motion, 4);
    configureStress(chords, 4);
    startVoices(motion, motionVoices);
    startVoices(chords, chordVoices);

    auto renderPair = [&]() {
        const auto started = Clock::now();
        for (int frame = 0; frame < kFramesPerBlock; ++frame)
        {
            const auto motionOut = motion.processStereo();
            const auto chordOut = chords.processStereo();
            checksum = checksum + motionOut.left * 0.17f + motionOut.right * 0.23f
                + chordOut.left * 0.27f + chordOut.right * 0.33f;
        }
        return std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    };

    for (int block = 0; block < kWarmupBlocks; ++block)
        (void)renderPair();

    double totalMs = 0.0;
    double maximumMs = 0.0;
    for (int block = 0; block < kMeasuredBlocks; ++block)
    {
        if (block > 0 && block % 64 == 0)
        {
            startVoices(motion, motionVoices);
            startVoices(chords, chordVoices);
        }
        const double elapsedMs = renderPair();
        totalMs += elapsedMs;
        maximumMs = std::max(maximumMs, elapsedMs);
    }

    std::cout << "Glass pair " << motionVoices << '+' << chordVoices
              << " voices: average " << std::fixed << std::setprecision(3)
              << totalMs / kMeasuredBlocks << " ms, maximum "
              << maximumMs << " ms\n";
}

} // namespace

int main()
{
    std::cout << "Canticle 48 kHz / 1024-frame processing benchmark\n"
              << std::left << std::setw(7) << "model"
              << std::right << std::setw(7) << "voices"
              << std::setw(12) << "average ms"
              << std::setw(12) << "maximum ms" << '\n';

    volatile float checksum = 0.0f;
    for (int model = 0; model < 5; ++model)
        for (const int voices : {1, 6, 12})
            benchmark(model, voices, checksum);
    benchmarkGlassPair(1, 6, checksum);
    benchmarkGlassPair(6, 12, checksum);

    if (!std::isfinite(checksum))
        return 1;
    return 0;
}
