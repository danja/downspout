#pragma once

#include "thumper_params.hpp"
#include <vector>
#include <cmath>

namespace thumper {

struct KickVoice {
    bool active = false;
    float phase = 0.0f;
    float pitch = 55.0f;
    float env = 0.0f;
    float clickEnv = 0.0f;
    float toneState = 0.0f;
    float pitchEnv = 1.0f;
    float filterEnv = 1.0f;
    float ampEnv = 1.0f;
    int sampleRate = 44100;
    float samplesLeft = 0.0f;
};

class ThumperEngine {
public:
    ThumperEngine();
    void setSampleRate(int sr);
    void setParams(const Params& p);
    void applyPreset();
    const Params& params() const { return params_; }
    // trigger a kick
    void trigger();
    // render block of stereo output (interleaved L/R)
    void render(float* outL, float* outR, int n);
    bool active() const;
private:
    float renderVoice(int engineId, KickVoice& v);
    Params params_;
    KickVoice v1_;
    KickVoice v2_;
    int sr_ = 44100;
};

} // namespace thumper