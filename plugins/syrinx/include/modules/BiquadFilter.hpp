#pragma once

#include <algorithm>
#include <cmath>

namespace downspout::syrinx {

class BiquadFilter {
public:
    enum class Type { Bandpass, Highpass };

    explicit BiquadFilter(float sampleRate = 48000.0f, Type type = Type::Bandpass)
        : sampleRate_(sampleRate), type_(type)
    {
        setParameters(1000.0f, 1.0f);
    }

    void setSampleRate(float sr) { sampleRate_ = sr; }

    void setParameters(float freq, float q)
    {
        freq_ = freq;
        q_ = q;
        updateCoeffs();
    }

    void setFrequency(float freq) { setParameters(freq, q_); }
    void setQ(float q) { setParameters(freq_, q); }

    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

    float process(float in)
    {
        const float out = a0_ * in + a1_ * x1_ + a2_ * x2_ - b1_ * y1_ - b2_ * y2_;
        x2_ = x1_; x1_ = in;
        y2_ = y1_; y1_ = out;
        if (!std::isfinite(out)) { reset(); return 0.0f; }
        return out;
    }

private:
    void updateCoeffs()
    {
        const float f = std::clamp(freq_, 20.0f, sampleRate_ * 0.49f);
        const float w0 = 2.0f * 3.14159265359f * f / sampleRate_;
        const float sn = std::sin(w0);
        const float cs = std::cos(w0);
        const float alpha = sn / (2.0f * std::max(0.3f, q_));
        const float a0r = 1.0f + alpha;

        if (type_ == Type::Bandpass) {
            a0_ = alpha / a0r;
            a1_ = 0.0f;
            a2_ = -alpha / a0r;
        } else {
            a0_ = (1.0f + cs) * 0.5f / a0r;
            a1_ = -(1.0f + cs) / a0r;
            a2_ = (1.0f + cs) * 0.5f / a0r;
        }
        b1_ = -2.0f * cs / a0r;
        b2_ = (1.0f - alpha) / a0r;
    }

    float sampleRate_;
    Type type_;
    float freq_ = 1000.0f, q_ = 1.0f;
    float a0_ = 1.0f, a1_ = 0.0f, a2_ = 0.0f;
    float b1_ = 0.0f, b2_ = 0.0f;
    float x1_ = 0.0f, x2_ = 0.0f;
    float y1_ = 0.0f, y2_ = 0.0f;
};

} // namespace downspout::syrinx
