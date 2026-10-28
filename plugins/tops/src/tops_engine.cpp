#include "tops_core_types.hpp"
#include <cmath>
#include <cstdlib>
#include <algorithm>
namespace tops {
static float clampf(float x,float lo,float hi){ if(x<lo)return lo; if(x>hi)return hi; return x; }
void TopsEngine::applyPreset() {
    switch((Preset)params_.preset){
        case Preset::Rock: params_.decay=0.1f; params_.brightness=0.7f; params_.noise=0.5f; break;
        case Preset::Jazz: params_.decay=0.15f; params_.brightness=0.5f; params_.noise=0.4f; break;
        case Preset::Electronic: params_.decay=0.05f; params_.brightness=0.9f; params_.noise=0.7f; break;
        case Preset::Metal: params_.decay=0.3f; params_.brightness=0.8f; params_.noise=0.3f; break;
        case Preset::Latin: params_.decay=0.12f; params_.brightness=0.6f; params_.noise=0.5f; break;
        default: break;
    }
}
int TopsEngine::instrumentForNote(int note) {
    switch (note) {
        case 42: return (int)Instrument::ClosedHH;
        case 44: return (int)Instrument::PedalHH;
        case 46: return (int)Instrument::OpenHH;
        case 49: case 57: return (int)Instrument::Crash;
        case 51: case 53: case 59: return (int)Instrument::Ride;
        case 55: return (int)Instrument::Splash;
        case 52: return (int)Instrument::China;
        case 56: return (int)Instrument::Cowbell;
        case 54: return (int)Instrument::Tambourine;
        case 81: return (int)Instrument::Triangle;
        default: return -1;
    }
}
void TopsEngine::trigger(int instrument, float velocity) {
    // choke open HH on closed/pedal
    int inst = instrument >= 0 ? instrument : params_.instrument;
    if (inst==(int)Instrument::ClosedHH || inst==(int)Instrument::PedalHH) {
        for (auto& v : voices_) if (v.active && v.instrument==(int)Instrument::OpenHH) v.active=false;
    }
    // find free voice
    int slot=-1;
    for (int i=0;i<kVoices;i++) if(!voices_[i].active){ slot=i; break; }
    if (slot<0) slot=0; // steal
    Voice& v = voices_[slot];
    v.active=true; v.instrument=inst;
    v.phase=0; v.env=1; v.noiseEnv=1; v.toneState=0;
    v.pitch = params_.pitch;
    v.noiseBias = 0;
    float decay = params_.decay;
    // instrument-specific decay
    if (inst==(int)Instrument::Crash) decay*=4.0f;
    if (inst==(int)Instrument::Ride) decay*=2.5f;
    if (inst==(int)Instrument::OpenHH) decay*=2.0f;
    if (inst==(int)Instrument::ClosedHH) decay=0.05f;
    if (inst==(int)Instrument::PedalHH) decay=0.07f;
    // Voicing of the instruments that otherwise share a recipe (splash is the unmodified one).
    if (inst==(int)Instrument::China)     { v.pitch*=0.75f; decay*=1.5f; v.noiseBias=0.2f; }
    if (inst==(int)Instrument::Tambourine){ v.pitch*=1.3f;  decay*=0.6f; v.noiseBias=0.3f; }
    if (inst==(int)Instrument::Cowbell)   { v.pitch*=0.08f; decay*=3.0f; }
    if (inst==(int)Instrument::Triangle)  { decay*=6.0f; }
    v.samplesLeft = decay*(float)sr_;
    // The envelope falls 8 e-folds (about -70 dB) over the voice's life.
    v.decayCoef = std::exp(-8.0f / std::max(1.0f, v.samplesLeft));
    v.velocity = clampf(velocity, 0.0f, 1.0f);
    v.shelfState = 0;
}
float TopsEngine::renderVoice(Voice& v) {
    if(!v.active) return 0;
    v.env *= v.decayCoef;
    v.noiseEnv *= 0.995f;
    float sample=0;
    int eng = params_.engine;
    v.phase += 2.0f*(float)M_PI*v.pitch/(float)v.sr;
    if(v.phase>2*M_PI) v.phase-=2*M_PI;
    // metallic partials
    if (eng==(int)Engine::Metallic) {
        sample = std::sin(v.phase) + 0.7f*std::sin(v.phase*2.3f) + 0.4f*std::sin(v.phase*3.7f) + 0.2f*std::sin(v.phase*5.1f);
    } else if (eng==(int)Engine::Noise) {
        sample = (noise());
    } else if (eng==(int)Engine::FM) {
        float mod = std::sin(v.phase*params_.pitch*0.01f)*3.0f; sample=std::sin(v.phase+mod);
    } else if (eng==(int)Engine::Physical) {
        sample = std::sin(v.phase) + 0.5f*std::sin(v.phase*1.4f)*v.env + 0.25f*std::sin(v.phase*2.1f)*v.env*v.env;
    } else if (eng==(int)Engine::Wavetable) {
        sample = 2.0f*(v.phase/(2.0f*(float)M_PI))-1.0f;
    } else {
        sample = std::sin(v.phase)*std::sin(v.phase*0.5f);
    }
    // noise component
    float noiseMix = clampf(params_.noise + v.noiseBias, 0.0f, 1.0f);
    sample = sample*(1-noiseMix) + ((noise()))*noiseMix;
    // brightness filter
    float cutoff = 0.2f + params_.brightness*0.75f;
    v.toneState += cutoff*(sample-v.toneState);
    sample = v.toneState;
    // Tone: high-frequency tilt around a ~3 kHz split; neutral at the 0.6 default.
    v.shelfState += 0.35f*(sample-v.shelfState);
    sample = v.shelfState + std::max(0.0f, 1.0f + (params_.tone-0.6f)*2.5f)*(sample-v.shelfState);
    // cowbell/tambourine/triangle special: lower pitch
    if (v.instrument==(int)Instrument::Cowbell) sample *= 0.8f;
    if (v.instrument==(int)Instrument::Triangle) sample = std::sin(v.phase)*v.env;
    sample *= v.env * params_.volume * v.velocity;
    if (params_.distortion>0){ float g=1+params_.distortion*10; sample=std::tanh(sample*g); }
    v.samplesLeft -= 1;
    if (v.samplesLeft<=0) v.active=false;
    return sample;
}
void TopsEngine::render(float* L,float* R,int n){
    for(int i=0;i<n;i++){
        float s=0;
        for(auto& v: voices_) s+=renderVoice(v);
        L[i]+=s; R[i]+=s;
    }
}
bool TopsEngine::active() const { for(auto&v:voices_) if(v.active) return true; return false; }
} // namespace tops