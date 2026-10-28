#pragma once

#include "thumper_params.hpp"
#include "thumper_rng.hpp"
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
    float basePitch = 0.0f;  // resting pitch for this hit; 0 = use Params::pitch
    float velocity = 1.0f;
};

class ThumperEngine {
public:
    ThumperEngine();
    void setSampleRate(int sr);
    void setParams(const Params& p);
    void applyPreset();
    const Params& params() const { return params_; }
    // trigger a kick. pitchHz > 0 overrides the Pitch parameter for this hit only; velocity is 0..1.
    void trigger(float pitchHz = 0.0f, float velocity = 1.0f);
    // render block of stereo output (interleaved L/R)
    void render(float* outL, float* outR, int n);
    bool active() const;
    // General MIDI kick notes (35 Acoustic Bass Drum, 36 Bass Drum 1).
    static bool isKickNote(int note) { return note == 35 || note == 36; }
private:
    float noise() { return rng_.nextSigned(); }
    downspout::thumper::Rng rng_;
    float renderVoice(int engineId, KickVoice& v);
    Params params_;
    KickVoice v1_;
    KickVoice v2_;
    int sr_ = 44100;
};

} // namespace thumper