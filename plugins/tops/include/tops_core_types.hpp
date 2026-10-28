#pragma once
#include "tops_params.hpp"
#include "tops_rng.hpp"
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
    float shelfState=0;
    float decayCoef=0.999f;  // per-sample envelope multiplier
    float velocity=1;
    float noiseBias=0;  // added to the Noise control for this instrument
};
class TopsEngine {
public:
    TopsEngine(){ setSampleRate(44100); }
    void setSampleRate(int sr){ sr_=sr; for(auto&v:voices_) v.sr=sr; }
    void setParams(const Params& p){ params_=p; }
    void applyPreset();
    const Params& params() const { return params_; }
    // Plays `instrument` (-1 = the Instrument parameter); velocity is 0..1.
    void trigger(int instrument = -1, float velocity = 1.0f);
    // General MIDI drum note to Instrument, or -1 when the note is not one of ours.
    static int instrumentForNote(int note);
    void render(float* L, float* R, int n);
    bool active() const;
private:
    float noise() { return rng_.nextSigned(); }
    downspout::tops::Rng rng_;
    float renderVoice(Voice& v);
    Params params_;
    static constexpr int kVoices=8;
    Voice voices_[kVoices];
    int sr_=44100;
};
} // namespace tops