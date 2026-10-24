#include "syrinx_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace downspout::syrinx {

Engine::Engine(float sampleRate)
    : sampleRate_(sampleRate)
    , analyzer_(sampleRate)
    , drivenVoice_(sampleRate)
    , reverbL_(sampleRate)
    , reverbR_(sampleRate)
    , dcL_(0.999f)
    , dcR_(0.999f)
{
    resetBend();
    // Initialize parameters from defaults
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto spec = getParameterSpec(i);
        params_[i] = spec.defaultValue;
    }

    for (auto& v : voices_)
        v.setSampleRate(sampleRate_);
    drivenVoice_.setSampleRate(sampleRate_);

    rebuildFx();
}

void Engine::setSampleRate(float sampleRate)
{
    sampleRate_ = sampleRate > 1.0f ? sampleRate : 48000.0f;
    for (auto& v : voices_)
        v.setSampleRate(sampleRate_);
    drivenVoice_.setSampleRate(sampleRate_);
    analyzer_.setSampleRate(sampleRate_);
    drivenRunning_ = false;
    reverbL_ = ReverbModule(sampleRate_);
    reverbR_ = ReverbModule(sampleRate_);
    rebuildFx();
}

void Engine::rebuildFx()
{
    reverbL_.setSize(0.58f);
    reverbR_.setSize(0.64f);
    const float dist = params_[kParamDistance];
    reverbL_.setLevel(dist * 0.45f);
    reverbR_.setLevel(dist * 0.45f);
}

float Engine::getParameter(std::uint32_t index) const
{
    return index < kParameterCount ? params_[index] : 0.0f;
}

void Engine::setParameter(std::uint32_t index, float value)
{
    if (index >= kParameterCount) return;
    const auto spec = getParameterSpec(index);
    if (spec.output) return;   // status values belong to the engine
    if (spec.boolean)
        value = value >= 0.5f ? 1.0f : 0.0f;
    else if (spec.integer)
        value = std::round(std::clamp(value, spec.minimum, spec.maximum));
    else
        value = std::clamp(value, spec.minimum, spec.maximum);
    params_[index] = value;

    if (index == kParamDistance)
        rebuildFx();
    // A knob on the selected preset, or a new preset or source, reaches the driven voice.
    if (index == kParamSelectedPreset || index == kParamSource
        || (index < kPresetCount * kParamsPerPreset && index / kParamsPerPreset == selectedPreset()))
        drivenDirty_ = true;
}

std::uint32_t Engine::selectedPreset() const
{
    return static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::round(params_[kParamSelectedPreset])), 0, 9));
}

void Engine::handleMidi(const std::uint8_t* data, std::uint32_t size)
{
    if (!data || size < 2) return;
    const std::uint8_t cmd = data[0] & 0xf0u;
    const int channel = data[0] & 0x0f;
    const auto ch = static_cast<std::size_t>(channel);
    if (cmd == 0x90u && size >= 3) {
        if (data[2] > 0)
            handleNoteOn(data[1], data[2], channel);
        else
            handleNoteOff(data[1], channel);
    } else if (cmd == 0x80u && size >= 3) {
        handleNoteOff(data[1], channel);
    } else if (cmd == 0xe0u && size >= 3) {
        handlePitchBend(channel, (data[1] & 0x7F) | ((data[2] & 0x7F) << 7));
    } else if (cmd == 0xb0u && size >= 3) {
        if (data[1] == 120 || data[1] == 123)
            reset();
        else if (data[1] == 101)
            rpnMsb_[ch] = data[2];
        else if (data[1] == 100)
            rpnLsb_[ch] = data[2];
        else if (data[1] == 6 && rpnMsb_[ch] == 0 && rpnLsb_[ch] == 0 && data[2] >= 1) {
            // RPN 0: pitch bend range in semitones, per channel.
            bendRange_[ch] = static_cast<float>(std::min<int>(data[2], 48));
            handlePitchBend(channel, bendValue_[ch]);
        } else
            handleCC(data[1], data[2]);
    }
}

void Engine::handleNoteOn(std::uint8_t note, std::uint8_t velocity, int channel)
{
    if (std::lround(params_[kParamSource]) == 1)   // Audio only: the keyboard is ignored
        return;
    const std::uint32_t preset = selectedPreset();

    // Check mute for selected preset
    if (params_[presetParam(preset, kPresetParamMute)] >= 0.5f)
        return;

    const PresetParams pp = decodePreset(params_.data(), preset);
    const float vel = static_cast<float>(velocity) / 127.0f;
    const int syllables = std::clamp(static_cast<int>(std::lround(params_[kParamSyllables])), 1, 16);
    const float chirp = params_[kParamChirp];

    if (syllables <= 1 && chirp <= 0.001f) {   // the original single steady tone
        const int vi = allocateVoice();
        voices_[vi].trigger(note, vel, pp, noiseStream_++, channel,
                            bendRatio_[static_cast<std::size_t>(channel & 15)]);
        return;
    }

    std::size_t slot = 0;
    for (std::size_t i = 0; i < phrases_.size(); ++i)
        if (!phrases_[i].active) { slot = i; break; }
    Phrase& ph = phrases_[slot];
    ph = Phrase {};
    ph.active = true;
    ph.note = note;
    ph.velocity = vel;
    ph.channel = channel;
    ph.count = syllables;
    ph.base = pp;
    ph.rng = Xorshift32(noiseStream_++ * 7u + 3u);
    fireSyllable(ph);
}

// Plays the phrase's next syllable and schedules the one after.
void Engine::fireSyllable(Phrase& ph)
{
    const float chirp = params_[kParamChirp];
    const float var = params_[kParamVariation];
    const float gap = params_[kParamGap];
    auto r01 = [&] { return 0.5f + 0.5f * ph.rng.next(); };

    PresetParams q = ph.base;
    q.shaped = true;
    q.bend = 0.0f;
    // Chirp shortens the preset's duration toward a real syllable: x0.28 at 0.6, x0.12 at 1.
    const float len = std::max(0.025f, ph.base.durationSec * std::pow(0.12f, chirp)
                                           * (1.0f + (r01() - 0.5f) * 0.7f * var));
    q.durationSec = len;

    // Contour: mostly ramps biased by the preset's Bend, some arches and valleys.
    const float mag = chirp * 2.2f * (1.0f - 0.5f * var + var * r01());
    const float pUp = 0.5f + 0.4f * ph.base.bend;
    const float r = r01();
    float sign = 1.0f;
    q.arch = false;
    if (r < 0.7f * pUp) {
        sign = 1.0f;
    } else if (r < 0.7f * pUp + 0.7f * (1.0f - pUp)) {
        sign = -1.0f;
    } else {
        q.arch = true;
        sign = r01() < 0.5f ? 1.0f : -1.0f;
    }
    q.sweepOct = sign * mag;

    // Each syllable lands on its own pitch; the preset's Bend leans the phrase up or down.
    q.pitchSemitones += (r01() - 0.5f) * 2.0f * var * 4.0f + ph.base.bend * static_cast<float>(ph.index) * 0.8f;
    q.noise = std::clamp(ph.base.noise + 0.50f * chirp * (0.5f + r01()), 0.0f, 1.0f);
    q.regime = std::clamp(ph.base.regime + var * 0.25f * (r01() - 0.3f), 0.0f, 1.0f);
    const float vel = ph.velocity * (1.0f - var * 0.45f * r01());

    const int vi = allocateVoice();
    voices_[vi].trigger(ph.note, vel, q, noiseStream_++, ph.channel,
                        bendRatio_[static_cast<std::size_t>(ph.channel & 15)]);

    ++ph.index;
    if (ph.index >= ph.count) {
        ph.active = false;
        return;
    }
    const float gapSec = len * (0.15f + 1.6f * gap) * (1.0f + (r01() - 0.5f) * 0.6f * var);
    ph.countdown = std::max(1, static_cast<int>((len + gapSec) * sampleRate_));
}

void Engine::handleNoteOff(std::uint8_t note, int channel)
{
    for (auto& v : voices_)
        if (v.isActive() && v.midiNote() == note && (channel < 0 || v.channel() == channel))
            v.release();
}

void Engine::handlePitchBend(int channel, int value14)
{
    const auto ch = static_cast<std::size_t>(channel & 15);
    bendValue_[ch] = value14;
    const float semitones = (static_cast<float>(value14) - 8192.0f) / 8192.0f * bendRange_[ch];
    bendRatio_[ch] = std::pow(2.0f, semitones / 12.0f);
    for (auto& v : voices_)
        if (v.isActive() && v.channel() == channel)
            v.setChannelBend(bendRatio_[ch]);
}

void Engine::resetBend()
{
    bendRatio_.fill(1.0f);
    bendRange_.fill(2.0f);
    bendValue_.fill(8192);
    rpnMsb_.fill(127);
    rpnLsb_.fill(127);
}

void Engine::handleCC(std::uint8_t cc, std::uint8_t value)
{
    const float norm = static_cast<float>(value) / 127.0f;
    const std::uint32_t preset = selectedPreset();

    auto setPreset = [&](std::uint32_t p, float v) {
        setParameter(presetParam(preset, p), v);
    };

    switch (cc) {
    case 1:   setPreset(kPresetParamVibDepth,    norm); break;
    case 7:   setParameter(kParamMasterGain,     norm); break;
    case 11:  setPreset(kPresetParamLevel,       norm * 1.4f); break;  // expression → level
    case 71:  setPreset(kPresetParamRoughness,   norm); break;
    case 73:  setPreset(kPresetParamDuration,    norm); break;
    case 74:  setPreset(kPresetParamTimbre,      norm); break;
    case 76:  setPreset(kPresetParamVibRate,     norm); break;
    case 77:  setPreset(kPresetParamNoise,       norm); break;
    case 78:  setPreset(kPresetParamHarmonic,    norm); break;
    case 79:  setPreset(kPresetParamCoupling,    norm); break;
    case 80:  setPreset(kPresetParamAMDepth,     norm); break;
    case 83:  setPreset(kPresetParamPitch,       norm * 2.0f - 1.0f); break;
    case 81:  setParameter(kParamSyllables,      1.0f + norm * 15.0f); break;
    case 82:  setParameter(kParamChirp,          norm); break;
    case 84:  setParameter(kParamVariation,      norm); break;
    case 85:  setPreset(kPresetParamRespiration, norm); break;
    case 86:  setParameter(kParamGap,            norm); break;
    case 87:  setParameter(kParamInMix,          norm); break;
    case 91:  setParameter(kParamDistance,       norm); break;
    case 94:  setPreset(kPresetParamBend,        norm * 2.0f - 1.0f); break;
    default:  break;
    }
}

int Engine::allocateVoice()
{
    // First: find an inactive voice
    for (int i = 0; i < kMaxVoices; ++i)
        if (!voices_[i].isActive()) return i;

    // Next: steal a releasing voice
    for (int i = 0; i < kMaxVoices; ++i)
        if (voices_[i].isReleasing()) return i;

    // Last resort: the oldest voice
    int oldest = 0;
    for (int i = 1; i < kMaxVoices; ++i)
        if (voices_[i].age() > voices_[oldest].age()) oldest = i;
    return oldest;
}

// Analyses the input and steers the free-running bird voice with it. Adds the voice to `wet`.
void Engine::updateAudioVoice(const float inL, const float inR, float& wet)
{
    analyzer_.setRange(params_[kParamInRange]);
    analyzer_.push(0.5f * (inL + inR));

    if (!drivenRunning_) {
        drivenVoice_.startDriven(decodePreset(params_.data(), selectedPreset()), 0x5eedu);
        drivenRunning_ = true;
        drivenDirty_ = false;
        pitchValid_ = false;
    } else if (drivenDirty_) {
        drivenVoice_.applyParams(decodePreset(params_.data(), selectedPreset()));
        drivenDirty_ = false;
    }

    // Gate on loudness relative to the recent peak, with hysteresis so breaths do not chatter.
    const float rel = analyzer_.relativeLevel();
    const float threshold = 0.04f + 0.45f * params_[kParamInGate];
    const bool audible = analyzer_.level() > 2.0e-4f;
    if (!gateOpen_ && audible && rel > threshold * 1.15f) gateOpen_ = true;
    if (gateOpen_ && (!audible || rel < threshold * 0.85f)) gateOpen_ = false;

    const float hz = analyzer_.hz();
    const bool voiced = gateOpen_ && hz > 0.0f;
    const float unvoiced = params_[kParamInUnvoiced];
    const float sib = analyzer_.sibilance();

    if (voiced) {
        float f = hz * params_[kParamInPitchMult];
        for (int i = 0; i < 6 && f > 9000.0f; ++i) f *= 0.5f;
        for (int i = 0; i < 6 && f < 300.0f; ++i) f *= 2.0f;
        const float target = std::log2(f);
        const float tau = 0.002f * std::pow(60.0f, params_[kParamInResponse]);
        if (!pitchValid_) { logHz_ = target; pitchValid_ = true; }
        logHz_ += (1.0f - std::exp(-1.0f / (tau * sampleRate_))) * (target - logHz_);
    }
    silentSamples_ = gateOpen_ ? 0 : silentSamples_ + 1;
    if (silentSamples_ > static_cast<int>(0.15f * sampleRate_)) pitchValid_ = false;

    if (analyzer_.takeOnset() && gateOpen_ && params_[kParamChirp] > 0.01f) {
        const float up = audioRng_.next() > -0.3f ? 1.0f : -1.0f;
        drivenVoice_.kick(up * params_[kParamChirp] * (0.4f + 0.8f * (0.5f + 0.5f * audioRng_.next())));
    }

    // Sibilants and breath: the tone fades and turbulence takes over.
    const float toneTarget = voiced ? 1.0f - 0.5f * sib * unvoiced : 1.0f - unvoiced;
    const float noiseTarget = gateOpen_ ? (voiced ? 0.3f * sib * unvoiced : unvoiced * (0.5f + 0.5f * sib)) : 0.0f;
    const float ampTarget = gateOpen_ ? std::sqrt(std::clamp(rel, 0.0f, 1.0f)) : 0.0f;
    const float k = 1.0f - std::exp(-1.0f / (0.004f * sampleRate_));
    ampSm_ += k * (ampTarget - ampSm_);
    toneSm_ += k * (toneTarget - toneSm_);
    noiseSm_ += k * (noiseTarget - noiseSm_);

    drivenVoice_.setDrive(std::exp2(logHz_), ampSm_, toneSm_, noiseSm_);
    wet += drivenVoice_.process();

    if ((++statusCounter_ & 63) == 0) {
        params_[kParamOutLevel] = gateOpen_ ? rel : 0.0f;
        params_[kParamOutInHz] = voiced ? hz : 0.0f;
        params_[kParamOutBirdHz] = ampSm_ > 0.02f ? std::exp2(logHz_) : 0.0f;
        params_[kParamOutVoiced] = !gateOpen_ ? 0.0f : (voiced ? 1.0f : 0.5f);
    }
}

StereoFrame Engine::processStereo(const float inL, const float inR)
{
    float sumL = 0.0f, sumR = 0.0f;

    for (auto& ph : phrases_)
        if (ph.active && --ph.countdown <= 0)
            fireSyllable(ph);

    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].isActive()) continue;
        const float sample = voices_[i].process();
        // Simple equal-power stereo spread: voice i gets position in [-0.5, +0.5]
        const float pan   = (static_cast<float>(i) / (kMaxVoices - 1)) - 0.5f;
        const float angle = (pan + 1.0f) * 3.14159265359f * 0.25f;
        sumL += sample * std::cos(angle);
        sumR += sample * std::sin(angle);
    }

    const bool audioIn = std::lround(params_[kParamSource]) != 0;
    const float mix = params_[kParamInMix];
    if (audioIn) {
        float bird = 0.0f;
        updateAudioVoice(inL, inR, bird);
        sumL += bird * mix * 0.7071f;
        sumR += bird * mix * 0.7071f;
    } else if (drivenRunning_) {
        drivenVoice_.kill();
        drivenRunning_ = false;
        analyzer_.reset();
        gateOpen_ = false;
        ampSm_ = noiseSm_ = 0.0f;
        params_[kParamOutLevel] = params_[kParamOutInHz] = params_[kParamOutBirdHz] = params_[kParamOutVoiced] = 0.0f;
    }

    // Tanh soft saturation (matches syrinx-processor.js output stage)
    sumL = std::tanh(sumL * 1.6f) * 0.8f;
    sumR = std::tanh(sumR * 1.6f) * 0.8f;

    // Reverb (distance)
    sumL = reverbL_.process(sumL);
    sumR = reverbR_.process(sumR);

    // DC block
    sumL = dcL_.process(sumL);
    sumR = dcR_.process(sumR);

    // Master gain
    const float gain = params_[kParamMasterGain];
    sumL = sumL * gain;
    sumR = sumR * gain;

    // The dry voice skips the master gain so the blend keeps the input level.
    if (audioIn) {
        sumL += inL * (1.0f - mix);
        sumR += inR * (1.0f - mix);
    }
    return { std::clamp(sumL, -1.0f, 1.0f), std::clamp(sumR, -1.0f, 1.0f) };
}

void Engine::reset()
{
    for (auto& v : voices_) v.kill();
    for (auto& ph : phrases_) ph.active = false;
    drivenVoice_.kill();
    drivenRunning_ = false;
    analyzer_.reset();
    gateOpen_ = false;
    ampSm_ = noiseSm_ = 0.0f;
    reverbL_.reset();
    reverbR_.reset();
    dcL_.reset();
    dcR_.reset();
}

void Engine::randomizeSelectedPreset(std::uint32_t seed)
{
    const std::uint32_t preset = selectedPreset();

    // Simple LCG for UI randomization
    auto lcg = [&]() -> float {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>(seed >> 8) / static_cast<float>(1u << 24);
    };

    auto randRange = [&](float lo, float hi) { return lo + lcg() * (hi - lo); };

    setParameter(presetParam(preset, kPresetParamLevel),        randRange(0.5f,   1.2f));
    setParameter(presetParam(preset, kPresetParamNoise),        randRange(0.0f,   0.4f));
    setParameter(presetParam(preset, kPresetParamRoughness),    randRange(0.0f,   0.3f));
    setParameter(presetParam(preset, kPresetParamTimbre),       randRange(0.0f,   0.8f));
    setParameter(presetParam(preset, kPresetParamVibRate),      randRange(0.1f,   0.8f));
    setParameter(presetParam(preset, kPresetParamVibDepth),     randRange(0.0f,   0.4f));
    setParameter(presetParam(preset, kPresetParamBend),         randRange(-0.7f,  0.7f));
    setParameter(presetParam(preset, kPresetParamHarmonic),     randRange(0.0f,   1.0f));
    setParameter(presetParam(preset, kPresetParamAMRate),       randRange(0.0f,   0.3f));
    setParameter(presetParam(preset, kPresetParamPitch),        randRange(-0.25f, 0.25f));
    setParameter(presetParam(preset, kPresetParamDuration),     randRange(0.04f,  0.55f));
    setParameter(presetParam(preset, kPresetParamRespiration),  randRange(0.0f,   0.3f));
    setParameter(presetParam(preset, kPresetParamAMDepth),      randRange(0.0f,   0.8f));
    setParameter(presetParam(preset, kPresetParamFormant1),     randRange(400.0f, 4000.0f));
    setParameter(presetParam(preset, kPresetParamFormant2),     randRange(800.0f, 7000.0f));
    setParameter(presetParam(preset, kPresetParamFormantQ),     randRange(2.0f,   12.0f));
    setParameter(presetParam(preset, kPresetParamCoupling),     randRange(0.0f,   0.4f));
    setParameter(presetParam(preset, kPresetParamVoiceOffset),  randRange(0.0f,   0.5f));
    // preserve mute
}

} // namespace downspout::syrinx
