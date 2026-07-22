#include "ambo_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace downspout::ambo {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr double kMaxDelaySeconds = 8.0;
constexpr double kParameterSmoothingSeconds = 0.012;
constexpr double kChainFadeSeconds = 0.006;

[[nodiscard]] float clampf(float value, float minValue, float maxValue)
{
    if (value < minValue)
        return minValue;
    if (value > maxValue)
        return maxValue;
    return value;
}

[[nodiscard]] int chainIndex(float value)
{
    return static_cast<int>(std::lround(clampf(value, 0.0f, static_cast<float>(kChainCount - 1))));
}

[[nodiscard]] float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}

void smoothTowards(float& current, float target, float coefficient)
{
    current += (target - current) * coefficient;
}

[[nodiscard]] float readChannel(const std::vector<float>& buffer,
                                std::uint32_t frames,
                                std::uint32_t frame,
                                std::uint32_t channel)
{
    if (frames == 0 || buffer.empty())
        return 0.0f;
    return buffer[(frame % frames) * kMaxChannels + channel];
}

[[nodiscard]] float readDelayLinear(const std::vector<float>& buffer,
                                    std::uint32_t frames,
                                    std::uint32_t writeHead,
                                    double delayFrames,
                                    std::uint32_t channel)
{
    if (frames == 0 || buffer.empty())
        return 0.0f;

    delayFrames = std::max(0.0, std::min(delayFrames, static_cast<double>(frames - 1u)));
    double readPosition = static_cast<double>(writeHead) - delayFrames;
    while (readPosition < 0.0)
        readPosition += static_cast<double>(frames);
    while (readPosition >= static_cast<double>(frames))
        readPosition -= static_cast<double>(frames);

    const auto frame0 = static_cast<std::uint32_t>(std::floor(readPosition));
    const auto frame1 = (frame0 + 1u) % frames;
    const float frac = static_cast<float>(readPosition - static_cast<double>(frame0));
    const float a = readChannel(buffer, frames, frame0, channel);
    const float b = readChannel(buffer, frames, frame1, channel);
    return a + (b - a) * frac;
}

void writeChannel(std::vector<float>& buffer,
                  std::uint32_t frames,
                  std::uint32_t frame,
                  std::uint32_t channel,
                  float value)
{
    if (frames == 0 || buffer.empty())
        return;
    buffer[(frame % frames) * kMaxChannels + channel] = value;
}

[[nodiscard]] float softClip(float value)
{
    return std::tanh(value);
}

[[nodiscard]] float foldback(float value)
{
    constexpr float threshold = 1.2f;
    if (value > threshold)
        return threshold - (value - threshold) * 0.38f;
    if (value < -threshold)
        return -threshold - (value + threshold) * 0.38f;
    return value;
}

[[nodiscard]] std::array<ModuleId, kModuleCount> chainModules(int chain)
{
    switch (chain) {
    case 1:
        return {ModuleId::Tape, ModuleId::Time, ModuleId::Shimmer, ModuleId::Spectral, ModuleId::Delay, ModuleId::Drive};
    case 2:
        return {ModuleId::Spectral, ModuleId::Shimmer, ModuleId::Time, ModuleId::Delay, ModuleId::Tape, ModuleId::Drive};
    case 3:
        return {ModuleId::Drive, ModuleId::Time, ModuleId::Spectral, ModuleId::Delay, ModuleId::Shimmer, ModuleId::Tape};
    default:
        return {ModuleId::Time, ModuleId::Spectral, ModuleId::Tape, ModuleId::Shimmer, ModuleId::Delay, ModuleId::Drive};
    }
}

[[nodiscard]] std::array<float, kMaxChannels> processTime(EngineState& state,
                                                          const std::array<float, kMaxChannels>& in,
                                                          float amount,
                                                          double sampleRate)
{
    if (amount <= 0.0001f || state.bufferFrames == 0)
        return in;

    const float slow = std::sin(state.timePhase);
    const float fast = std::sin(state.timePhase * 2.731f + 1.17f);
    const double baseDelay = (0.045 + amount * 1.22) * sampleRate;
    const double grainSpread = (0.018 + amount * 0.28) * sampleRate;
    const double drift = (slow * 0.55 + fast * 0.20) * grainSpread;
    const float smear = 0.30f + amount * 0.62f;
    const float reverseBlend = amount * amount * 0.18f;

    std::array<float, kMaxChannels> out {};
    for (std::uint32_t channel = 0; channel < kMaxChannels; ++channel) {
        const float tapA = readDelayLinear(state.timeBuffer, state.bufferFrames, state.writeHead, baseDelay + drift, channel);
        const float tapB = readDelayLinear(state.timeBuffer,
                                           state.bufferFrames,
                                           state.writeHead,
                                           baseDelay * 0.54 + grainSpread * (1.0 - slow * 0.25),
                                           channel);
        const float tapC = readDelayLinear(state.timeBuffer,
                                           state.bufferFrames,
                                           state.writeHead,
                                           baseDelay * 1.47 + grainSpread * (0.25 + fast * 0.35),
                                           channel);
        const float reverseTap = readDelayLinear(state.timeBuffer,
                                                 state.bufferFrames,
                                                 state.writeHead,
                                                 baseDelay - drift * 0.7 + grainSpread * 1.8,
                                                 1u - channel);
        const float wet = tapA * (0.52f - amount * 0.10f)
            + tapB * (0.30f + amount * 0.08f)
            + tapC * (0.18f + amount * 0.10f)
            - reverseTap * reverseBlend;
        out[channel] = in[channel] * (1.0f - smear) + wet * smear;
    }
    return out;
}

[[nodiscard]] std::array<float, kMaxChannels> processSpectral(EngineState& state,
                                                              const std::array<float, kMaxChannels>& in,
                                                              float amount)
{
    if (amount <= 0.0001f)
        return in;

    std::array<float, kMaxChannels> out {};
    const float lowCoeff = 0.006f + amount * 0.028f;
    const float midCoeff = 0.020f + amount * 0.070f;
    const float freezeCoeff = 0.00035f + (1.0f - amount) * 0.011f;
    const float airCoeff = 0.0012f + (1.0f - amount) * 0.020f;
    const float motion = std::sin(state.spectralPhase) * (0.08f + amount * 0.20f);
    for (std::uint32_t channel = 0; channel < kMaxChannels; ++channel) {
        state.spectralLow[channel] += (in[channel] - state.spectralLow[channel]) * lowCoeff;
        state.spectralMid[channel] += (state.spectralLow[channel] - state.spectralMid[channel]) * midCoeff;
        const float low = state.spectralLow[channel];
        const float mid = state.spectralLow[channel] - state.spectralMid[channel];
        const float high = in[channel] - low;
        state.spectralAir[channel] += (std::fabs(high) - state.spectralAir[channel]) * airCoeff;

        const float foldedHigh = softClip((high + state.spectralAir[channel] * (channel == 0 ? motion : -motion)) * (1.0f + amount * 2.0f));
        const float targetFreeze = low * (0.55f + amount * 0.15f) + mid * (0.35f + amount * 0.25f) + foldedHigh * (0.18f + amount * 0.32f);
        state.spectralFreeze[channel] += (targetFreeze - state.spectralFreeze[channel]) * freezeCoeff;
        const float cross = state.spectralFreeze[1u - channel] * amount * 0.16f;
        const float wet = state.spectralFreeze[channel] * (0.82f + amount * 0.24f)
            + cross
            + high * (0.13f - amount * 0.05f)
            - mid * amount * 0.12f;
        out[channel] = in[channel] * (1.0f - amount) + wet * amount;
    }
    return out;
}

[[nodiscard]] std::array<float, kMaxChannels> processTape(EngineState& state,
                                                          const std::array<float, kMaxChannels>& in,
                                                          float amount)
{
    if (amount <= 0.0001f)
        return in;

    std::array<float, kMaxChannels> out {};
    const float wow = std::sin(state.tapePhase * 0.37f) * amount * 0.018f;
    const float flutter = std::sin(state.tapePhase * 4.71f + 0.6f) * amount * 0.010f;
    const float level = 1.0f + wow + flutter;
    const float wear = 0.025f + amount * 0.130f;
    const float headBump = 0.004f + amount * 0.025f;
    const float drive = 1.0f + amount * 3.4f;
    const double slapDelay = (0.008 + amount * 0.026 + static_cast<double>(wow + flutter) * 0.006) * 48000.0;

    for (std::uint32_t channel = 0; channel < kMaxChannels; ++channel) {
        const float slap = state.bufferFrames > 0
            ? readDelayLinear(state.timeBuffer,
                              state.bufferFrames,
                              state.writeHead,
                              slapDelay * (state.sampleRate > 0.0 ? state.sampleRate / 48000.0 : 1.0),
                              channel)
            : 0.0f;
        const float biased = in[channel] + slap * amount * 0.12f + (channel == 0 ? 0.006f : -0.006f) * amount;
        const float saturated = softClip(biased * drive) / softClip(drive);
        state.tapeLow[channel] += (saturated - state.tapeLow[channel]) * wear;
        const float high = saturated - state.tapeLow[channel];
        state.tapeHigh[channel] += (high - state.tapeHigh[channel]) * (0.12f + amount * 0.08f);
        const float wet = (state.tapeLow[channel] + state.tapeHigh[channel] * (0.42f - amount * 0.22f)) * level
            + state.tapeLow[channel] * headBump;
        out[channel] = in[channel] * (1.0f - amount) + wet * amount;
    }
    return out;
}

[[nodiscard]] std::array<float, kMaxChannels> processShimmer(EngineState& state,
                                                             const std::array<float, kMaxChannels>& in,
                                                             float amount,
                                                             double sampleRate)
{
    if (amount <= 0.0001f || state.bufferFrames == 0)
        return in;

    const double mod = std::sin(state.shimmerPhase) * (0.006 + amount * 0.026) * sampleRate;
    const double leftDelay = (0.101 + amount * 0.31) * sampleRate + mod;
    const double rightDelay = (0.149 + amount * 0.37) * sampleRate - mod * 0.73;
    const double bloomDelay = (0.239 + amount * 0.51) * sampleRate + mod * 0.41;
    const double octaveTap = (0.041 + amount * 0.105) * sampleRate;
    const float regen = 0.36f + amount * 0.46f;
    const float send = 0.16f + amount * 0.70f;

    const float tankL = readDelayLinear(state.shimmerBuffer, state.bufferFrames, state.shimmerWriteHead, leftDelay, 0);
    const float tankR = readDelayLinear(state.shimmerBuffer, state.bufferFrames, state.shimmerWriteHead, rightDelay, 1);
    const float bloomL = readDelayLinear(state.shimmerBuffer, state.bufferFrames, state.shimmerWriteHead, bloomDelay, 0);
    const float bloomR = readDelayLinear(state.shimmerBuffer, state.bufferFrames, state.shimmerWriteHead, bloomDelay * 0.83, 1);
    const float pitchL = readDelayLinear(state.shimmerBuffer, state.bufferFrames, state.shimmerWriteHead, octaveTap, 0);
    const float pitchR = readDelayLinear(state.shimmerBuffer, state.bufferFrames, state.shimmerWriteHead, octaveTap + leftDelay * 0.22, 1);
    const float diffuseL = readDelayLinear(state.diffusionBuffer, state.bufferFrames, state.diffusionWriteHead, leftDelay * 0.43, 0);
    const float diffuseR = readDelayLinear(state.diffusionBuffer, state.bufferFrames, state.diffusionWriteHead, rightDelay * 0.39, 1);

    const float brightL = softClip((tankL * 0.46f + bloomR * 0.26f + pitchR * 0.28f + diffuseL * 0.22f)
                                  * (1.0f + amount * 0.70f));
    const float brightR = softClip((tankR * 0.46f + bloomL * 0.26f + pitchL * 0.28f + diffuseR * 0.22f)
                                  * (1.0f + amount * 0.70f));
    const float diffuseWriteL = in[0] * send + (brightR - diffuseR * 0.35f) * regen;
    const float diffuseWriteR = in[1] * send + (brightL - diffuseL * 0.35f) * regen;

    writeChannel(state.diffusionBuffer, state.bufferFrames, state.diffusionWriteHead, 0, diffuseWriteL);
    writeChannel(state.diffusionBuffer, state.bufferFrames, state.diffusionWriteHead, 1, diffuseWriteR);
    writeChannel(state.shimmerBuffer,
                 state.bufferFrames,
                 state.shimmerWriteHead,
                 0,
                 in[0] * send + (brightR + diffuseWriteL * 0.24f) * regen);
    writeChannel(state.shimmerBuffer,
                 state.bufferFrames,
                 state.shimmerWriteHead,
                 1,
                 in[1] * send + (brightL + diffuseWriteR * 0.24f) * regen);

    return {
        in[0] * (1.0f - amount * 0.58f) + (brightL + diffuseL * 0.34f) * amount * 0.86f,
        in[1] * (1.0f - amount * 0.58f) + (brightR + diffuseR * 0.34f) * amount * 0.86f,
    };
}

[[nodiscard]] std::array<float, kMaxChannels> processDelay(EngineState& state,
                                                           const std::array<float, kMaxChannels>& in,
                                                           float amount,
                                                           float feedback,
                                                           double sampleRate)
{
    if (amount <= 0.0001f || state.bufferFrames == 0)
        return in;

    const double sway = std::sin(state.timePhase * 0.61f + 0.31f) * (0.004 + amount * 0.018) * sampleRate;
    const double delayA = (0.145 + amount * 1.08) * sampleRate + sway;
    const double delayB = (0.223 + amount * 1.41) * sampleRate - sway * 0.59;
    const double delayC = (0.377 + amount * 0.73) * sampleRate + sway * 0.27;
    const float regen = clampf(0.16f + feedback * 0.58f + amount * 0.18f, 0.0f, 0.90f);
    const float send = 0.18f + amount * 0.74f;
    const float smear = amount * 0.22f;

    const float leftA = readDelayLinear(state.delayBuffer, state.bufferFrames, state.delayWriteHead, delayA, 0);
    const float rightA = readDelayLinear(state.delayBuffer, state.bufferFrames, state.delayWriteHead, delayA * 1.13, 1);
    const float leftB = readDelayLinear(state.delayBuffer, state.bufferFrames, state.delayWriteHead, delayB, 0);
    const float rightB = readDelayLinear(state.delayBuffer, state.bufferFrames, state.delayWriteHead, delayB * 0.87, 1);
    const float leftC = readDelayLinear(state.diffusionBuffer, state.bufferFrames, state.diffusionWriteHead, delayC, 0);
    const float rightC = readDelayLinear(state.diffusionBuffer, state.bufferFrames, state.diffusionWriteHead, delayC * 1.19, 1);

    const float delayedL = leftA * 0.56f + rightB * 0.30f + leftC * 0.20f;
    const float delayedR = rightA * 0.56f + leftB * 0.30f + rightC * 0.20f;
    const float filteredL = softClip(delayedL + (leftB - delayedL) * smear);
    const float filteredR = softClip(delayedR + (rightB - delayedR) * smear);

    writeChannel(state.delayBuffer, state.bufferFrames, state.delayWriteHead, 0, in[0] * send + filteredR * regen);
    writeChannel(state.delayBuffer, state.bufferFrames, state.delayWriteHead, 1, in[1] * send + filteredL * regen);

    return {
        in[0] * (1.0f - amount * 0.44f) + filteredL * amount,
        in[1] * (1.0f - amount * 0.44f) + filteredR * amount,
    };
}

[[nodiscard]] std::array<float, kMaxChannels> processDrive(EngineState& state,
                                                           const std::array<float, kMaxChannels>& in,
                                                           float amount)
{
    if (amount <= 0.0001f)
        return in;

    std::array<float, kMaxChannels> out {};
    const float drive = 1.0f + amount * 10.0f;
    const float bias = amount * 0.11f;
    const float fold = amount * amount * 0.42f;
    for (std::uint32_t channel = 0; channel < kMaxChannels; ++channel) {
        const float asym = in[channel] + (channel == 0 ? bias : -bias);
        const float folded = foldback(asym * drive);
        const float saturated = softClip(folded) / softClip(drive > 1.0f ? drive : 1.0f);
        const float rectified = std::fabs(saturated) * (saturated >= 0.0f ? 1.0f : -0.64f);
        const float wetRaw = saturated * (1.0f - fold) + rectified * fold;
        state.driveDc[channel] += (wetRaw - state.driveDc[channel]) * 0.006f;
        const float wet = wetRaw - state.driveDc[channel] * amount * 0.65f;
        out[channel] = in[channel] * (1.0f - amount) + wet * amount;
    }
    return out;
}

[[nodiscard]] std::array<float, kMaxChannels> processModule(EngineState& state,
                                                            ModuleId module,
                                                            const Parameters& parameters,
                                                            const std::array<float, kMaxChannels>& in,
                                                            double sampleRate)
{
    switch (module) {
    case ModuleId::Time: return processTime(state, in, parameters.time, sampleRate);
    case ModuleId::Spectral: return processSpectral(state, in, parameters.spectral);
    case ModuleId::Tape: return processTape(state, in, parameters.tape);
    case ModuleId::Shimmer: return processShimmer(state, in, parameters.shimmer, sampleRate);
    case ModuleId::Delay: return processDelay(state, in, parameters.delay, parameters.feedback, sampleRate);
    case ModuleId::Drive: return processDrive(state, in, parameters.drive);
    }
    return in;
}

void ensureBuffers(EngineState& state, double sampleRate)
{
    if (sampleRate <= 0.0)
        sampleRate = 48000.0;

    const auto frames = static_cast<std::uint32_t>(std::ceil(sampleRate * kMaxDelaySeconds));
    if (state.sampleRate == sampleRate && state.bufferFrames == frames && !state.timeBuffer.empty())
        return;

    state.sampleRate = sampleRate;
    state.bufferFrames = std::max<std::uint32_t>(1u, frames);
    state.writeHead = 0;
    state.delayWriteHead = 0;
    state.shimmerWriteHead = 0;
    state.diffusionWriteHead = 0;
    state.timeBuffer.assign(static_cast<std::size_t>(state.bufferFrames) * kMaxChannels, 0.0f);
    state.delayBuffer.assign(static_cast<std::size_t>(state.bufferFrames) * kMaxChannels, 0.0f);
    state.shimmerBuffer.assign(static_cast<std::size_t>(state.bufferFrames) * kMaxChannels, 0.0f);
    state.diffusionBuffer.assign(static_cast<std::size_t>(state.bufferFrames) * kMaxChannels, 0.0f);
    state.spectralLow = {};
    state.spectralMid = {};
    state.spectralFreeze = {};
    state.spectralAir = {};
    state.tapeLow = {};
    state.tapeHigh = {};
    state.driveDc = {};
    state.feedback = {};
    state.timePhase = 0.0f;
    state.tapePhase = 0.0f;
    state.spectralPhase = 0.0f;
    state.shimmerPhase = 0.0f;
}

}  // namespace

Parameters clampParameters(const Parameters& raw)
{
    Parameters parameters = raw;
    parameters.chain = static_cast<float>(chainIndex(parameters.chain));
    parameters.time = clampf(parameters.time, 0.0f, 1.0f);
    parameters.spectral = clampf(parameters.spectral, 0.0f, 1.0f);
    parameters.tape = clampf(parameters.tape, 0.0f, 1.0f);
    parameters.shimmer = clampf(parameters.shimmer, 0.0f, 1.0f);
    parameters.delay = clampf(parameters.delay, 0.0f, 1.0f);
    parameters.drive = clampf(parameters.drive, 0.0f, 1.0f);
    parameters.feedback = clampf(parameters.feedback, 0.0f, 0.96f);
    parameters.mix = clampf(parameters.mix, 0.0f, 1.0f);
    parameters.output = clampf(parameters.output, -24.0f, 12.0f);
    parameters.bypass = clampf(parameters.bypass, 0.0f, 1.0f);
    return parameters;
}

void activate(EngineState& state, double sampleRate)
{
    ensureBuffers(state, sampleRate);
    state.smoothingInitialized = false;
    state.activeChain = 0;
    state.pendingChain = 0;
    state.chainTransition = 1.0f;
    state.chainFadingOut = false;
}

OutputStatus processBlock(EngineState& state,
                          const Parameters& rawParameters,
                          std::uint32_t nframes,
                          double sampleRate,
                          const AudioBlock& audio)
{
    OutputStatus status {};
    if (nframes == 0)
        return status;

    if (sampleRate <= 0.0)
        sampleRate = state.sampleRate > 0.0 ? state.sampleRate : 48000.0;

    const Parameters targetParameters = clampParameters(rawParameters);
    if (!state.smoothingInitialized) {
        state.smoothedParameters = targetParameters;
        state.activeChain = chainIndex(targetParameters.chain);
        state.pendingChain = state.activeChain;
        state.smoothingInitialized = true;
    }

    const int targetChain = chainIndex(targetParameters.chain);
    if (targetChain != state.activeChain) {
        state.pendingChain = targetChain;
        state.chainFadingOut = true;
    }

    const std::uint32_t channelCount = std::min<std::uint32_t>(audio.channelCount, kMaxChannels);
    const float phaseStep = static_cast<float>((2.0 * kPi) / std::max(1.0, sampleRate));
    const float smoothingCoefficient = 1.0f - std::exp(-1.0f / static_cast<float>(sampleRate * kParameterSmoothingSeconds));
    const float chainFadeStep = 1.0f / std::max(1.0f, static_cast<float>(sampleRate * kChainFadeSeconds));

    for (std::uint32_t frame = 0; frame < nframes; ++frame) {
        Parameters& parameters = state.smoothedParameters;
        smoothTowards(parameters.time, targetParameters.time, smoothingCoefficient);
        smoothTowards(parameters.spectral, targetParameters.spectral, smoothingCoefficient);
        smoothTowards(parameters.tape, targetParameters.tape, smoothingCoefficient);
        smoothTowards(parameters.shimmer, targetParameters.shimmer, smoothingCoefficient);
        smoothTowards(parameters.delay, targetParameters.delay, smoothingCoefficient);
        smoothTowards(parameters.drive, targetParameters.drive, smoothingCoefficient);
        smoothTowards(parameters.feedback, targetParameters.feedback, smoothingCoefficient);
        smoothTowards(parameters.mix, targetParameters.mix, smoothingCoefficient);
        smoothTowards(parameters.output, targetParameters.output, smoothingCoefficient);
        smoothTowards(parameters.bypass, targetParameters.bypass, smoothingCoefficient);

        if (state.chainFadingOut) {
            state.chainTransition = std::max(0.0f, state.chainTransition - chainFadeStep);
            if (state.chainTransition <= 0.0f) {
                state.activeChain = state.pendingChain;
                state.chainFadingOut = false;
            }
        } else if (state.chainTransition < 1.0f) {
            state.chainTransition = std::min(1.0f, state.chainTransition + chainFadeStep);
        }

        const std::array<ModuleId, kModuleCount> modules = chainModules(state.activeChain);
        const float wetMix = parameters.mix;
        const float dryMix = 1.0f - wetMix;
        const float outputGain = dbToGain(parameters.output);
        const float feedbackAmount = parameters.feedback;
        std::array<float, kMaxChannels> dry {};
        for (std::uint32_t channel = 0; channel < kMaxChannels; ++channel) {
            const float* in = channel < channelCount ? audio.inputs[channel] : nullptr;
            dry[channel] = in != nullptr ? in[frame] : 0.0f;
        }

        writeChannel(state.timeBuffer, state.bufferFrames, state.writeHead, 0, dry[0]);
        writeChannel(state.timeBuffer, state.bufferFrames, state.writeHead, 1, dry[1]);

        const std::array<float, kMaxChannels> feedbackReturn {
            state.feedback[1] * feedbackAmount * 0.72f + state.feedback[0] * feedbackAmount * 0.28f,
            state.feedback[0] * feedbackAmount * 0.72f + state.feedback[1] * feedbackAmount * 0.28f,
        };
        std::array<float, kMaxChannels> wet {
            dry[0] + feedbackReturn[0],
            dry[1] + feedbackReturn[1],
        };

        for (ModuleId module : modules)
            wet = processModule(state, module, parameters, wet, sampleRate);

        wet[0] = clampf(wet[0], -4.0f, 4.0f);
        wet[1] = clampf(wet[1], -4.0f, 4.0f);
        state.feedback[0] = wet[0];
        state.feedback[1] = wet[1];

        for (std::uint32_t channel = 0; channel < channelCount; ++channel) {
            float* out = audio.outputs[channel];
            if (out == nullptr)
                continue;
            const float effected = (dry[channel] * dryMix + wet[channel] * wetMix) * outputGain;
            const float transitionMix = parameters.bypass
                + (1.0f - parameters.bypass) * (1.0f - state.chainTransition);
            const float mixed = effected + (dry[channel] - effected) * transitionMix;
            out[frame] = clampf(mixed, -1.5f, 1.5f);
        }

        status.wetEnergy += (std::fabs(wet[0]) + std::fabs(wet[1])) * wetMix * (1.0f - parameters.bypass);
        status.feedbackEnergy += std::fabs(feedbackReturn[0]) + std::fabs(feedbackReturn[1]);

        state.writeHead = (state.writeHead + 1u) % state.bufferFrames;
        state.delayWriteHead = (state.delayWriteHead + 1u) % state.bufferFrames;
        state.shimmerWriteHead = (state.shimmerWriteHead + 1u) % state.bufferFrames;
        state.diffusionWriteHead = (state.diffusionWriteHead + 1u) % state.bufferFrames;
        state.timePhase += phaseStep * (0.07f + parameters.time * 0.21f);
        state.tapePhase += phaseStep * (0.19f + parameters.tape * 0.53f);
        state.spectralPhase += phaseStep * (0.031f + parameters.spectral * 0.067f);
        state.shimmerPhase += phaseStep * (0.047f + parameters.shimmer * 0.083f);
        if (state.timePhase > kPi * 2.0f)
            state.timePhase -= kPi * 2.0f;
        if (state.tapePhase > kPi * 2.0f)
            state.tapePhase -= kPi * 2.0f;
        if (state.spectralPhase > kPi * 2.0f)
            state.spectralPhase -= kPi * 2.0f;
        if (state.shimmerPhase > kPi * 2.0f)
            state.shimmerPhase -= kPi * 2.0f;
    }

    const float denom = 1.0f / static_cast<float>(std::max<std::uint32_t>(1u, nframes * 2u));
    status.wetEnergy = clampf(status.wetEnergy * denom, 0.0f, 1.0f);
    status.feedbackEnergy = clampf(status.feedbackEnergy * denom, 0.0f, 1.0f);
    return status;
}

}  // namespace downspout::ambo
