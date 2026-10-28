#pragma once
#include "snatch_params.hpp"
#include "snatch_rng.hpp"
namespace snatch {
struct Voice { bool active=false; float phase=0; float env=0; float noiseEnv=0; float samplesLeft=0; int sr=44100; float velocity=1; int engine=0; float decay=0.25f; };
class SnatchEngine {
public:
    SnatchEngine(){ setSampleRate(44100); }
    void setSampleRate(int sr){ sr_=sr; v_.sr=sr; }
    void setParams(const Params& p){ params_=p; }
    void applyPreset();
    const Params& params() const { return params_; }
    // Plays `engine` (-1 = the Engine parameter); velocity is 0..1.
    void trigger(int engine = -1, float velocity = 1.0f);
    // General MIDI snare-family note to Engine, or -1 when the note is not ours. 38 plays `selected`.
    static int engineForNote(int note, int selected);
    void render(float* L, float* R, int n);
    bool active() const { return v_.active; }
private:
    float noise() { return rng_.nextSigned(); }
    downspout::snatch::Rng rng_;
    float renderSample();
    Params params_;
    Voice v_;
    int sr_=44100;
};
} // namespace snatch