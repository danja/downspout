#pragma once

namespace downspout::dsp {

// One-pole DC blocker: y[n] = x[n] - x[n-1] + R * y[n-1].
// R is the pole location (0.995-0.999 typical); higher means a lower cutoff. Shared by Drumkit and Syrinx, whose copies
// were identical; moved verbatim so their output does not change.
class DCBlocker {
public:
    explicit DCBlocker(float r = 0.999f) : R_(r) {}

    void setPole(float r) { R_ = r; }

    void reset()
    {
        x1_ = 0.0f;
        y1_ = 0.0f;
    }

    float process(float input)
    {
        const float output = input - x1_ + R_ * y1_;
        x1_ = input;
        y1_ = output;
        return output;
    }

private:
    float R_;       // pole location
    float x1_ = 0.0f;   // previous input
    float y1_ = 0.0f;   // previous output
};

}  // namespace downspout::dsp
