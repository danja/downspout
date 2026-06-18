#pragma once

#include <vector>

namespace downspout::basilico {

struct FlangerFrame {
    float left = 0.0f;
    float right = 0.0f;
};

class BasilicoFlanger {
public:
    void setSampleRate(float sampleRate);
    void reset();
    FlangerFrame process(float input, float modulation, float depth);

private:
    void ensureBuffer();
    float readDelay(float delaySamples) const;

    float sampleRate_ = 44100.0f;
    std::vector<float> buffer_;
    std::size_t writeIndex_ = 0;
    float feedbackLeft_ = 0.0f;
    float feedbackRight_ = 0.0f;
    float smoothedDepth_ = 0.0f;
};

} // namespace downspout::basilico
