#include "basilico_flanger.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::basilico {
namespace {

float clampUnit(const float value)
{
    return std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
}

float sanitizeAudio(const float value)
{
    if (!std::isfinite(value))
        return 0.0f;
    return std::tanh(std::clamp(value, -4.0f, 4.0f));
}

} // namespace

void BasilicoFlanger::setSampleRate(const float sampleRate)
{
    sampleRate_ = std::max(1000.0f, sampleRate);
    ensureBuffer();
    reset();
}

void BasilicoFlanger::reset()
{
    std::fill(buffer_.begin(), buffer_.end(), 0.0f);
    writeIndex_ = 0;
    feedbackLeft_ = 0.0f;
    feedbackRight_ = 0.0f;
    smoothedDepth_ = 0.0f;
}

FlangerFrame BasilicoFlanger::process(const float input, const float modulation, const float depth)
{
    ensureBuffer();

    const float targetDepth = clampUnit(depth);
    smoothedDepth_ += (targetDepth - smoothedDepth_) * 0.0025f;
    if (targetDepth <= 0.0001f && smoothedDepth_ <= 0.0001f)
    {
        buffer_[writeIndex_] = input;
        ++writeIndex_;
        if (writeIndex_ >= buffer_.size())
            writeIndex_ = 0;
        feedbackLeft_ = 0.0f;
        feedbackRight_ = 0.0f;
        return {input, input};
    }

    const float wet = smoothedDepth_ * (0.18f + smoothedDepth_ * 0.72f);

    const float safeMod = std::clamp(std::isfinite(modulation) ? modulation : 0.0f, -1.0f, 1.0f);
    const float baseMs = 0.55f + smoothedDepth_ * 1.30f;
    const float rangeMs = 0.35f + smoothedDepth_ * 5.75f;
    const float leftMs = baseMs + (0.5f + safeMod * 0.5f) * rangeMs;
    const float rightMs = baseMs + (0.5f - safeMod * 0.5f) * rangeMs * 0.93f;
    const float leftDelay = readDelay(leftMs * sampleRate_ * 0.001f);
    const float rightDelay = readDelay(rightMs * sampleRate_ * 0.001f);

    const float feedback = smoothedDepth_ * 0.42f;
    buffer_[writeIndex_] = sanitizeAudio(input + (feedbackLeft_ + feedbackRight_) * 0.5f * feedback);
    ++writeIndex_;
    if (writeIndex_ >= buffer_.size())
        writeIndex_ = 0;

    feedbackLeft_ = sanitizeAudio(leftDelay);
    feedbackRight_ = sanitizeAudio(rightDelay);

    const float dryGain = 1.0f - wet * 0.42f;
    return {
        sanitizeAudio(input * dryGain + leftDelay * wet),
        sanitizeAudio(input * dryGain + rightDelay * wet),
    };
}

void BasilicoFlanger::ensureBuffer()
{
    const std::size_t required = static_cast<std::size_t>(std::ceil(sampleRate_ * 0.030f)) + 4U;
    if (buffer_.size() != required)
        buffer_.assign(required, 0.0f);
    if (writeIndex_ >= buffer_.size())
        writeIndex_ = 0;
}

float BasilicoFlanger::readDelay(const float delaySamples) const
{
    if (buffer_.empty())
        return 0.0f;

    const float boundedDelay = std::clamp(delaySamples, 1.0f, static_cast<float>(buffer_.size() - 2U));
    float readPosition = static_cast<float>(writeIndex_) - boundedDelay;
    while (readPosition < 0.0f)
        readPosition += static_cast<float>(buffer_.size());

    const auto index0 = static_cast<std::size_t>(readPosition) % buffer_.size();
    const auto index1 = (index0 + 1U) % buffer_.size();
    const float frac = readPosition - std::floor(readPosition);
    return buffer_[index0] + (buffer_[index1] - buffer_[index0]) * frac;
}

} // namespace downspout::basilico
