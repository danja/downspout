#pragma once

#include "clatter_params.hpp"
#include "clatter_patches.hpp"
#include "clatter_rng.hpp"
#include "downspout/dsp/svf.hpp"

namespace clatter {

struct Voice {
    bool active = false;
    int sound = 0;
    long n = 0;                // samples since the hit
    long dur = 0;              // hit length in samples; the voice ends there
    float velocity = 1.0f;
    float gainL = 0.0f, gainR = 0.0f;
    float freqMul = 1.0f;
    float tau = 0.1f;          // e-fold time of the main envelope, seconds
    float toneGain = 1.0f;     // 2 x Tone: 1 at the neutral setting
    // envelopes (multiplied down once per sample, so no exp() in the render loop)
    float env = 1.0f, coef = 0.999f;
    float dropEnv = 1.0f, clickEnv = 1.0f;
    float dropCoef = 0.99f, clickCoef = 0.99f;
    float pe[6] = {}, pcoef[6] = {};   // per-partial envelopes (metal)
    // oscillators
    float phase[6] = {};
    float inc[6] = {};                 // phase step per sample, cycles
    float amp[6] = {};
    float stepMul = 1.0f;
    // restrikes (bells, castanets, claps)
    int strikesLeft = 0;
    long strikeEvery = 0, age = 0;
    // noise path
    downspout::dsp::Svf svf;
    downspout::dsp::SvfCoeff coeff;
    float lp = 0.0f;
    float rattlePhase = 0.0f, scrapePhase = 0.0f;
    long attack = 0;
    // choke
    bool choking = false;
    float chokeGain = 1.0f;
};

class ClatterEngine {
public:
    static constexpr int kVoices = 16;

    ClatterEngine() { setSampleRate(44100); }
    void setSampleRate(int sr) { sr_ = static_cast<float>(sr); }
    void setParams(const Params& p) { params_ = p; }
    const Params& params() const { return params_; }

    // Plays sound `sound` (0..kSoundCount-1, the Sound parameter order); velocity is 0..1.
    void trigger(int sound, float velocity = 1.0f);
    // Adds a block of stereo audio to outL/outR.
    void render(float* outL, float* outR, int n);
    bool active() const;

    // General MIDI note to sound index, or -1 when the note is not one of ours.
    static int soundForNote(int note);

private:
    float renderVoice(Voice& v);
    float noise() { return rng_.nextSigned(); }

    Params params_;
    Voice voices_[kVoices];
    downspout::clatter::Rng rng_;
    float sr_ = 44100.0f;
};

}  // namespace clatter
