#include "gremlin_processor.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>

namespace {

// Coarse half-octave log-magnitude fingerprint used to assert that the modes and
// scenes stay audibly distinct. Comparing band shape rather than hand-weighted
// scalars (rms/zero-crossings/roughness) keeps the check tied to timbre and
// independent of the shipping parameter defaults, which are a product decision
// and change more often than the synthesis paths do.
constexpr std::size_t kBandCount = 16;
constexpr float kLowestBandHz = 60.0f;
constexpr std::size_t kAnalysisStart = 1024;
constexpr float kMagnitudeFloor = 1.0e-9f;

// Minimum spectral distance required between any two modes, and between the
// musical and extreme scenes. Measured worst-case separation at the shipping
// defaults is ~0.117 for modes and ~0.150 for the scene pair, so this leaves
// roughly a 2x margin rather than sitting inside the metric's own scatter.
constexpr float kModeSeparation = 0.06f;
constexpr float kSceneSeparation = 0.06f;

struct Signature {
    std::array<float, kBandCount> bands {};
    float peak = 0.0f;
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

// Goertzel magnitude for one bin over the analysis window.
double bandMagnitude(const std::array<float, 4096>& left,
                     const std::array<float, 4096>& right,
                     const double frequencyHz,
                     const double sampleRate)
{
    const std::size_t count = left.size() - kAnalysisStart;
    const double omega = 2.0 * 3.14159265358979323846 * frequencyHz / sampleRate;
    const double coeff = 2.0 * std::cos(omega);

    double q1 = 0.0;
    double q2 = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const double sample = 0.5 * (static_cast<double>(left[kAnalysisStart + i])
                                   + static_cast<double>(right[kAnalysisStart + i]));
        const double q0 = coeff * q1 - q2 + sample;
        q2 = q1;
        q1 = q0;
    }

    const double power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    return std::sqrt(std::max(0.0, power)) / static_cast<double>(count);
}

Signature measureRenderedSignature(const std::array<float, 4096>& left,
                                   const std::array<float, 4096>& right,
                                   const double sampleRate = 48000.0)
{
    Signature signature {};

    for (std::size_t i = kAnalysisStart; i < left.size(); ++i)
        signature.peak = std::max(signature.peak, std::max(std::fabs(left[i]), std::fabs(right[i])));

    for (std::size_t band = 0; band < kBandCount; ++band)
    {
        const double frequency = static_cast<double>(kLowestBandHz)
                               * std::pow(2.0, static_cast<double>(band) * 0.5);
        const double magnitude = frequency < sampleRate * 0.45
                               ? bandMagnitude(left, right, frequency, sampleRate)
                               : 0.0;
        signature.bands[band] = static_cast<float>(std::log10(magnitude + kMagnitudeFloor));
    }

    return signature;
}

// Mean-removed cosine distance: 0 means identical band shape, larger means more
// timbral difference. Removing the mean discards overall level so the check is
// about spectrum rather than loudness.
float spectralDistance(const Signature& a, const Signature& b)
{
    double meanA = 0.0;
    double meanB = 0.0;
    for (std::size_t i = 0; i < kBandCount; ++i)
    {
        meanA += a.bands[i];
        meanB += b.bands[i];
    }
    meanA /= static_cast<double>(kBandCount);
    meanB /= static_cast<double>(kBandCount);

    double dot = 0.0;
    double normA = 0.0;
    double normB = 0.0;
    for (std::size_t i = 0; i < kBandCount; ++i)
    {
        const double x = static_cast<double>(a.bands[i]) - meanA;
        const double y = static_cast<double>(b.bands[i]) - meanB;
        dot += x * y;
        normA += x * x;
        normB += y * y;
    }

    if (normA <= 0.0 || normB <= 0.0)
        return 0.0f;

    return static_cast<float>(1.0 - dot / std::sqrt(normA * normB));
}

void printSignature(const char* label, const std::size_t index, const Signature& signature)
{
    std::cerr << label << ' ' << index << " peak=" << signature.peak << " bands=[";
    for (std::size_t i = 0; i < kBandCount; ++i)
        std::cerr << signature.bands[i] << (i + 1 < kBandCount ? ", " : "");
    std::cerr << "]\n";
}

Signature renderModeSignature(const std::size_t mode)
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

Signature renderSceneSignature(const downspout::gremlin::SceneId scene)
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

    std::array<Signature, downspout::gremlin::kModeCount> signatures {};
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
            const float distance = spectralDistance(signatures[mode], signatures[other]);
            if (distance < nearest)
            {
                nearest = distance;
                nearestMode = other;
            }
        }
        if (nearest <= kModeSeparation)
        {
            std::cerr << "nearest spectral distance for mode " << mode << " was " << nearest
                      << " against mode " << nearestMode << " (threshold " << kModeSeparation << ")\n";
            printSignature("mode", mode, signatures[mode]);
            printSignature("mode", nearestMode, signatures[nearestMode]);
        }
        require(nearest > kModeSeparation, "gremlin mode signatures should remain separated");
    }

    const Signature musicalScene = renderSceneSignature(SceneId::melt);
    const Signature extremeScene = renderSceneSignature(SceneId::tunnel);
    require(musicalScene.peak > 0.005f, "gremlin musical scene should emit audio");
    require(extremeScene.peak > 0.005f, "gremlin extreme scene should emit audio");

    const float sceneDistance = spectralDistance(musicalScene, extremeScene);
    if (sceneDistance <= kSceneSeparation)
    {
        std::cerr << "musical/extreme scene spectral distance was " << sceneDistance
                  << " (threshold " << kSceneSeparation << ")\n";
        printSignature("scene", 0, musicalScene);
        printSignature("scene", 1, extremeScene);
    }
    require(sceneDistance > kSceneSeparation,
            "gremlin musical and extreme scenes should remain audibly separated");

    float monoOnly[128] {};
    processor.processBlock(monoOnly, nullptr, 128, nullptr, 0);
    float monoPeak = 0.0f;
    for (float sample : monoOnly)
        monoPeak = std::max(monoPeak, std::fabs(sample));

    require(monoPeak > 0.001f, "gremlin mono-output render should still emit audio");

    return 0;
}
