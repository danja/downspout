#include "thumper_core_types.hpp"
#include <cmath>
#include <algorithm>
#include <cstdlib>

namespace thumper {

static float clampf(float x, float lo, float hi){ if(x<lo) return lo; if(x>hi) return hi; return x; }

ThumperEngine::ThumperEngine() { setSampleRate(44100); }

void ThumperEngine::setSampleRate(int sr) { sr_ = sr; v1_.sampleRate = sr; v2_.sampleRate = sr; }

void ThumperEngine::setParams(const Params& p) { params_ = p; }

void ThumperEngine::applyPreset() {
    switch ((Preset)params_.preset) {
        case Preset::Techno: params_.engine=(int)Engine::VirtualAnalog; params_.pitch=50; params_.pitchDrop=14; params_.decay=0.4f; params_.click=0.6f; params_.body=0.8f; params_.drive=0.4f; break;
        case Preset::Gabber: params_.engine=(int)Engine::VirtualAnalog; params_.pitch=60; params_.pitchDrop=18; params_.decay=0.25f; params_.click=0.9f; params_.body=0.9f; params_.drive=0.8f; break;
        case Preset::Rock: params_.engine=(int)Engine::Subtractive; params_.pitch=60; params_.pitchDrop=8; params_.decay=0.35f; params_.click=0.4f; params_.body=0.7f; params_.drive=0.2f; break;
        case Preset::Latin: params_.engine=(int)Engine::Modal; params_.pitch=70; params_.pitchDrop=6; params_.decay=0.3f; params_.click=0.5f; params_.body=0.6f; params_.drive=0.1f; break;
        case Preset::House: params_.engine=(int)Engine::VirtualAnalog; params_.pitch=48; params_.pitchDrop=10; params_.decay=0.5f; params_.click=0.5f; params_.body=0.75f; params_.drive=0.3f; break;
        case Preset::Trap: params_.engine=(int)Engine::FM; params_.pitch=40; params_.pitchDrop=16; params_.decay=0.6f; params_.click=0.7f; params_.body=0.85f; params_.drive=0.5f; break;
        case Preset::Acoustic: params_.engine=(int)Engine::Modal; params_.pitch=65; params_.pitchDrop=5; params_.decay=0.45f; params_.click=0.3f; params_.body=0.65f; params_.drive=0.0f; break;
        default: break;
    }
}

void ThumperEngine::trigger(float pitchHz, float velocity) {
    for (int i=0;i<2;i++) {
        KickVoice* v = (i==0)?&v1_:&v2_;
        v->active = true;
        v->basePitch = pitchHz;
        v->velocity = clampf(velocity, 0.0f, 1.0f);
        v->phase = 0.0f;
        v->pitch = params_.pitch * std::pow(2.0f, params_.pitchDrop / 12.0f);
        v->env = 1.0f;
        v->clickEnv = 1.0f;
        v->samplesLeft = params_.decay * (float)sr_;
        v->toneState = 0.0f;
        v->pitchEnv = 1.0f;
        v->filterEnv = 1.0f;
        v->ampEnv = 1.0f;
    }
}

bool ThumperEngine::active() const { return v1_.active || v2_.active; }

float ThumperEngine::renderVoice(int engineId, KickVoice& v) {
    if (!v.active) return 0.0f;
    float total = params_.decay * (float)sr_ + 1.0f;
    float t = 1.0f - (v.samplesLeft / total);
    t = clampf(t, 0.0f, 1.0f);
    float ped = std::max(0.001f, params_.pitchEnvDecay);
    float fed = std::max(0.001f, params_.filterEnvDecay);
    float aed = std::max(0.001f, params_.ampEnvDecay);
    v.pitchEnv = std::exp(-t / ped);
    v.filterEnv = std::exp(-t / fed);
    v.ampEnv = std::exp(-t / aed);
    float base = v.basePitch > 0.0f ? v.basePitch : params_.pitch;
    float pitch = base * (1.0f + (params_.pitchDrop/12.0f) * v.pitchEnv);
    v.pitch = pitch;

    float env = v.ampEnv;
    float modalEnv = std::exp(-t / std::max(0.001f, aed * params_.modalDecay / 0.4f));
    v.env = env;
    v.clickEnv *= 0.992f;

    float sample = 0.0f;
    int eng = engineId;

    v.phase += 2.0f * (float)M_PI * v.pitch / (float)sr_;
    if (v.phase > 2.0f*(float)M_PI) v.phase -= 2.0f*(float)M_PI;

    if (eng == (int)Engine::VirtualAnalog) sample = std::sin(v.phase);
    else if (eng == (int)Engine::Subtractive) sample = (v.phase / (float)M_PI) - 1.0f;
    else if (eng == (int)Engine::FM) { float mod = std::sin(v.phase * params_.fmRatio) * params_.fmIndex * env; sample = std::sin(v.phase + mod); }
    else if (eng == (int)Engine::Modal) sample = std::sin(v.phase) + 0.5f*std::sin(v.phase*2.0f)*modalEnv + 0.25f*std::sin(v.phase*3.1f)*modalEnv*modalEnv;
    else if (eng == (int)Engine::PhaseDistortion) sample = std::sin(v.phase + 0.5f*std::sin(v.phase));
    else if (eng == (int)Engine::Wavetable) sample = (1.0f - t)*(2.0f*(v.phase/(2.0f*(float)M_PI)) - 1.0f) + t*std::sin(v.phase);  // saw morphing to sine
    else if (eng == (int)Engine::EightOhEight) sample = std::sin(v.phase) * (1.0f + 0.3f*env);
    else if (eng == (int)Engine::NineOhNine) sample = std::sin(v.phase) + 0.4f*std::sin(v.phase*4.0f)*env;
    else if (eng == (int)Engine::Physical) sample = std::sin(v.phase) + 0.6f*std::sin(v.phase*1.59f)*env + 0.3f*std::sin(v.phase*2.14f)*env*env;
    else sample = noise() * env;

    float subPhase = v.phase * 0.5f;
    sample = sample*(1.0f - params_.subMix*0.5f) + std::sin(subPhase)*params_.subMix*0.5f;
    sample *= params_.body;

    float cutoff = 0.05f + params_.tone * 0.6f + v.filterEnv * 0.3f;
    cutoff = clampf(cutoff, 0.01f, 0.99f);
    v.toneState += cutoff * (sample - v.toneState);
    sample = v.toneState;

    if (v.clickEnv > 0.001f) {
        float click = noise();
        sample += click * v.clickEnv * params_.click * (1.0f - t*0.5f);
    }

    if (params_.drive > 0.0f) {
        float g = 1.0f + params_.drive * 10.0f;
        sample = std::tanh(sample * g);
    }

    sample *= env * params_.volume * v.velocity;

    v.samplesLeft -= 1.0f;
    if (v.samplesLeft <= 0.0f) v.active = false;
    return sample;
}

void ThumperEngine::render(float* outL, float* outR, int n) {
    for (int i = 0; i < n; i++) {
        float s1 = renderVoice(params_.engine, v1_);
        float s = s1;
        if (params_.engine2 >= 0) {
            float s2 = renderVoice(params_.engine2, v2_);
            s = s1*(1.0f-params_.layerMix) + s2*params_.layerMix;
        }
        if (params_.distortion > 0.0f) {
            float g = 1.0f + params_.distortion * 20.0f;
            s = std::tanh(s * g) / std::tanh(g);
        }
        outL[i] += s;
        outR[i] += s;
    }
}

} // namespace thumper