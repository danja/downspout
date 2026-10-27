#pragma once
#include "tops_params.hpp"
namespace tops {
struct Voice {
    bool active=false;
    int instrument=-1;
    float phase=0;
    float env=0;
    float noiseEnv=0;
    float samplesLeft=0;
    float pitch=8000;
    int sr=44100;
    float toneState=0;
};
class TopsEngine {
public:
    TopsEngine(){ setSampleRate(44100); }
    void setSampleRate(int sr){ sr_=sr; for(auto&v:voices_) v.sr=sr; }
    void setParams(const Params& p){ params_=p; }
    void applyPreset();
    const Params& params() const { return params_; }
    void trigger();
    void render(float* L, float* R, int n);
    bool active() const;
private:
    float renderVoice(Voice& v);
    Params params_;
    static constexpr int kVoices=8;
    Voice voices_[kVoices];
    int sr_=44100;
};
} // namespace tops