#pragma once
#include "snatch_params.hpp"
namespace snatch {
struct Voice { bool active=false; float phase=0; float env=0; float noiseEnv=0; float samplesLeft=0; int sr=44100; };
class SnatchEngine {
public:
    SnatchEngine(){ setSampleRate(44100); }
    void setSampleRate(int sr){ sr_=sr; v_.sr=sr; }
    void setParams(const Params& p){ params_=p; }
    void applyPreset();
    const Params& params() const { return params_; }
    void trigger();
    void render(float* L, float* R, int n);
    bool active() const { return v_.active; }
private:
    float renderSample();
    Params params_;
    Voice v_;
    int sr_=44100;
};
} // namespace snatch