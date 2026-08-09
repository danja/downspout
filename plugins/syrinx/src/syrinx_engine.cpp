#include "syrinx_engine.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace downspout::syrinx {

Engine::Engine(float sampleRate)
    : sampleRate_(sampleRate)
    , reverbL_(sampleRate)
    , reverbR_(sampleRate)
    , dcL_(0.999f)
    , dcR_(0.999f)
{
    // Initialize parameters from defaults
    for (std::uint32_t i = 0; i < kParameterCount; ++i) {
        const auto spec = getParameterSpec(i);
        params_[i] = spec.defaultValue;
    }

    for (auto& v : voices_)
        v.setSampleRate(sampleRate_);

    rebuildFx();
}

void Engine::setSampleRate(float sampleRate)
{
    sampleRate_ = sampleRate > 1.0f ? sampleRate : 48000.0f;
    for (auto& v : voices_)
        v.setSampleRate(sampleRate_);
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
    if (spec.boolean)
        value = value >= 0.5f ? 1.0f : 0.0f;
    else if (spec.integer)
        value = std::round(std::clamp(value, spec.minimum, spec.maximum));
    else
        value = std::clamp(value, spec.minimum, spec.maximum);
    params_[index] = value;

    if (index == kParamDistance)
        rebuildFx();
}

void Engine::handleMidi(const std::uint8_t* data, std::uint32_t size)
{
    if (!data || size < 2) return;
    const std::uint8_t cmd = data[0] & 0xf0u;
    if (cmd == 0x90u && size >= 3) {
        if (data[2] > 0)
            handleNoteOn(data[1], data[2]);
        else
            handleNoteOff(data[1]);
    } else if (cmd == 0x80u && size >= 3) {
        handleNoteOff(data[1]);
    } else if (cmd == 0xb0u && size >= 3) {
        if (data[1] == 120 || data[1] == 123)
            reset();
        else
            handleCC(data[1], data[2]);
    }
}

void Engine::handleNoteOn(std::uint8_t note, std::uint8_t velocity)
{
    const std::uint32_t preset = static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::round(params_[kParamSelectedPreset])), 0, 9));

    // Check mute for selected preset
    if (params_[presetParam(preset, kPresetParamMute)] >= 0.5f)
        return;

    const PresetParams pp = decodePreset(params_.data(), preset);
    const int vi = allocateVoice();
    voices_[vi].trigger(note, static_cast<float>(velocity) / 127.0f, pp, noiseStream_++);
}

void Engine::handleNoteOff(std::uint8_t note)
{
    for (auto& v : voices_)
        if (v.isActive() && v.midiNote() == note)
            v.release();
}

void Engine::handleCC(std::uint8_t cc, std::uint8_t value)
{
    const float norm = static_cast<float>(value) / 127.0f;
    const std::uint32_t preset = static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::round(params_[kParamSelectedPreset])), 0, 9));

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
    case 85:  setPreset(kPresetParamRespiration, norm); break;
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

    // Steal voice 0 as last resort
    return 0;
}

StereoFrame Engine::processStereo()
{
    float sumL = 0.0f, sumR = 0.0f;

    for (int i = 0; i < kMaxVoices; ++i) {
        if (!voices_[i].isActive()) continue;
        const float sample = voices_[i].process();
        // Simple equal-power stereo spread: voice i gets position in [-0.5, +0.5]
        const float pan   = (static_cast<float>(i) / (kMaxVoices - 1)) - 0.5f;
        const float angle = (pan + 1.0f) * 3.14159265359f * 0.25f;
        sumL += sample * std::cos(angle);
        sumR += sample * std::sin(angle);
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
    sumL = std::clamp(sumL * gain, -1.0f, 1.0f);
    sumR = std::clamp(sumR * gain, -1.0f, 1.0f);

    return { sumL, sumR };
}

void Engine::reset()
{
    for (auto& v : voices_) v.kill();
    reverbL_.reset();
    reverbR_.reset();
    dcL_.reset();
    dcR_.reset();
}

void Engine::randomizeSelectedPreset(std::uint32_t seed)
{
    const std::uint32_t preset = static_cast<std::uint32_t>(
        std::clamp(static_cast<int>(std::round(params_[kParamSelectedPreset])), 0, 9));

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
