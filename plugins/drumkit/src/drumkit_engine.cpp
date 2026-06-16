#include "drumkit_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::drumkit {

namespace {

[[nodiscard]] float clampParameter(const std::uint32_t index, const float value)
{
    const ParameterSpec& spec = kParameterSpecs[index];
    if (spec.boolean)
        return value >= 0.5f ? 1.0f : 0.0f;
    return std::clamp(value, spec.minimum, spec.maximum);
}

[[nodiscard]] std::size_t instrumentIndex(const InstrumentId instrument)
{
    return static_cast<std::size_t>(instrument);
}

} // namespace

Engine::Engine(const float sampleRate)
    : sampleRate_(sampleRate)
    , reverbL_(sampleRate)
    , reverbR_(sampleRate)
    , dcBlockerL_(0.999f)
    , dcBlockerR_(0.999f)
{
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        parameters_[i] = kParameterSpecs[i].defaultValue;

    rebuildVoices();
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        applyParameter(i, parameters_[i]);
}

void Engine::setSampleRate(const float sampleRate)
{
    sampleRate_ = sampleRate > 1.0f ? sampleRate : 48000.0f;
    rebuildVoices();
    for (std::uint32_t i = 0; i < kParameterCount; ++i)
        applyParameter(i, parameters_[i]);
}

float Engine::getParameter(const std::uint32_t index) const
{
    return index < kParameterCount ? parameters_[index] : 0.0f;
}

void Engine::setParameter(const std::uint32_t index, const float value)
{
    if (index >= kParameterCount)
        return;

    const float clamped = clampParameter(index, value);
    const bool wasMuted = kParameterSpecs[index].boolean && parameters_[index] >= 0.5f;
    parameters_[index] = clamped;
    applyParameter(index, clamped);

    if (kParameterSpecs[index].boolean)
    {
        const bool muted = clamped >= 0.5f;
        for (const InstrumentSpec& spec : kInstrumentSpecs)
        {
            if (spec.muteParam == index)
            {
                muted_[instrumentIndex(spec.id)] = muted;
                if (muted && !wasMuted)
                    resetInstrument(spec.id);
                break;
            }
        }
    }
}

void Engine::setInstrumentMuted(const InstrumentId instrument, const bool muted)
{
    if (instrumentIndex(instrument) >= muted_.size())
        return;

    setParameter(kInstrumentSpecs[instrumentIndex(instrument)].muteParam, muted ? 1.0f : 0.0f);
}

bool Engine::isInstrumentMuted(const InstrumentId instrument) const
{
    return instrumentIndex(instrument) < muted_.size() && muted_[instrumentIndex(instrument)];
}

void Engine::handleMidi(const std::uint8_t* const data, const std::uint32_t size)
{
    if (data == nullptr || size < 2)
        return;

    const std::uint8_t command = data[0] & 0xf0u;
    if (command == 0x90u && size >= 3)
    {
        if (data[2] > 0)
            handleNoteOn(data[1], data[2]);
    }
    else if (command == 0xb0u && size >= 3)
    {
        if (data[1] == 120 || data[1] == 123)
            reset();
    }
}

void Engine::handleNoteOn(const std::uint8_t note, const std::uint8_t velocity)
{
    const float vel = static_cast<float>(velocity) / 127.0f;

    switch (note)
    {
    case 36:
        if (!isInstrumentMuted(InstrumentId::Kick))
            kick_->trigger(vel);
        break;
    case 39:
        if (!isInstrumentMuted(InstrumentId::Clap))
            clap_->trigger(1.0f);
        break;
    case 40:
        if (!isInstrumentMuted(InstrumentId::Snare))
            snare_->trigger(vel);
        break;
    case 41:
        if (!isInstrumentMuted(InstrumentId::Crash))
            crash_->trigger(1.0f);
        break;
    case 42:
        openHH_->kill();
        if (!isInstrumentMuted(InstrumentId::ClosedHH))
            closedHH_->trigger(1.0f);
        break;
    case 45:
        if (!isInstrumentMuted(InstrumentId::Tom1))
            loTom_->trigger(vel);
        break;
    case 46:
        if (!isInstrumentMuted(InstrumentId::OpenHH))
            openHH_->trigger(1.0f);
        break;
    case 50:
        if (!isInstrumentMuted(InstrumentId::Tom2))
            hiTom_->trigger(vel);
        break;
    case 51:
        if (!isInstrumentMuted(InstrumentId::Bash))
            bash_->trigger(vel);
        break;
    case 52:
        if (!isInstrumentMuted(InstrumentId::Cowbell))
            cowbell_->trigger(vel);
        break;
    case 53:
        if (!isInstrumentMuted(InstrumentId::Clave))
            clave_->trigger(vel);
        break;
    default:
        break;
    }
}

StereoFrame Engine::processStereo()
{
    StereoFrame frame;

    for (std::size_t i = 0; i < kInstrumentCount; ++i)
    {
        if (muted_[i])
            continue;
        addPanned(processInstrument(static_cast<InstrumentId>(i)), kVoicePan[i], frame.left, frame.right);
    }

    frame.left = bitcrusherL_.process(frame.left);
    frame.right = bitcrusherR_.process(frame.right);
    frame.left = distortionL_.process(frame.left);
    frame.right = distortionR_.process(frame.right);
    frame.left = reverbL_.process(frame.left);
    frame.right = reverbR_.process(frame.right);
    frame.left = dcBlockerL_.process(frame.left);
    frame.right = dcBlockerR_.process(frame.right);

    frame.left *= parameters_[kParamMasterGain];
    frame.right *= parameters_[kParamMasterGain];
    frame.left = std::clamp(frame.left, -1.0f, 1.0f);
    frame.right = std::clamp(frame.right, -1.0f, 1.0f);
    return frame;
}

void Engine::reset()
{
    for (std::size_t i = 0; i < kInstrumentCount; ++i)
        resetInstrument(static_cast<InstrumentId>(i));

    reverbL_.reset();
    reverbR_.reset();
    dcBlockerL_.reset();
    dcBlockerR_.reset();
}

void Engine::addPanned(const float sample, const float pan, float& left, float& right)
{
    const float clamped = std::clamp(pan, -1.0f, 1.0f);
    const float angle = (clamped + 1.0f) * (kPi * 0.25f);
    left += sample * std::cos(angle);
    right += sample * std::sin(angle);
}

float Engine::processInstrument(const InstrumentId instrument)
{
    switch (instrument)
    {
    case InstrumentId::Kick: return kick_->process();
    case InstrumentId::Clap: return clap_->process();
    case InstrumentId::Snare: return snare_->process();
    case InstrumentId::Crash: return crash_->process();
    case InstrumentId::ClosedHH: return closedHH_->process();
    case InstrumentId::Tom1: return loTom_->process();
    case InstrumentId::OpenHH: return openHH_->process();
    case InstrumentId::Tom2: return hiTom_->process();
    case InstrumentId::Bash: return bash_->process();
    case InstrumentId::Cowbell: return cowbell_->process();
    case InstrumentId::Clave: return clave_->process();
    case InstrumentId::Count: break;
    }
    return 0.0f;
}

void Engine::resetInstrument(const InstrumentId instrument)
{
    switch (instrument)
    {
    case InstrumentId::Kick: kick_->reset(); break;
    case InstrumentId::Clap: clap_->reset(); break;
    case InstrumentId::Snare: snare_->reset(); break;
    case InstrumentId::Crash: crash_->reset(); break;
    case InstrumentId::ClosedHH: closedHH_->reset(); break;
    case InstrumentId::Tom1: loTom_->reset(); break;
    case InstrumentId::OpenHH: openHH_->reset(); break;
    case InstrumentId::Tom2: hiTom_->reset(); break;
    case InstrumentId::Bash: bash_->reset(); break;
    case InstrumentId::Cowbell: cowbell_->reset(); break;
    case InstrumentId::Clave: clave_->reset(); break;
    case InstrumentId::Count: break;
    }
}

void Engine::rebuildVoices()
{
    kick_ = std::make_unique<KickVoice>(sampleRate_);
    snare_ = std::make_unique<SnareVoice>(sampleRate_);
    clap_ = std::make_unique<ClapVoice>(sampleRate_);
    loTom_ = std::make_unique<TomVoice>(sampleRate_, 80.0f);
    hiTom_ = std::make_unique<TomVoice>(sampleRate_, 200.0f);
    closedHH_ = std::make_unique<HiHatVoice>(sampleRate_, true);
    openHH_ = std::make_unique<HiHatVoice>(sampleRate_, false);
    crash_ = std::make_unique<CrashVoice>(sampleRate_);
    bash_ = std::make_unique<BashVoice>(sampleRate_);
    cowbell_ = std::make_unique<CowbellVoice>(sampleRate_);
    clave_ = std::make_unique<ClaveVoice>(sampleRate_);

    bitcrusherL_ = Bitcrusher {};
    bitcrusherR_ = Bitcrusher {};
    distortionL_ = Distortion {};
    distortionR_ = Distortion {};
    reverbL_ = ReverbModule {sampleRate_};
    reverbR_ = ReverbModule {sampleRate_};
    reverbL_.setSize(0.58f);
    reverbR_.setSize(0.64f);
    dcBlockerL_ = DCBlocker {0.999f};
    dcBlockerR_ = DCBlocker {0.999f};
}

void Engine::applyParameter(const std::uint32_t index, const float value)
{
    switch (index)
    {
    case kParamKickPitch: kick_->setPitch(value); break;
    case kParamKickDecay: kick_->setDecay(value); break;
    case kParamKickDrive: kick_->setDrive(value); break;
    case kParamKickPunch: kick_->setPunch(value); break;
    case kParamKickLevel: kick_->setLevel(value); break;
    case kParamKickTransient: kick_->setTransient(value); break;
    case kParamSnareTone: snare_->setTone(value); break;
    case kParamSnareSnap: snare_->setSnap(value); break;
    case kParamSnareLevel: snare_->setLevel(value); break;
    case kParamSnareMetal: snare_->setMetal(value); break;
    case kParamClapDensity: clap_->setDensity(value); break;
    case kParamClapTone: clap_->setTone(value); break;
    case kParamClapLevel: clap_->setLevel(value); break;
    case kParamClapMetal: clap_->setMetal(value); break;
    case kParamTom1Pitch: loTom_->setPitch(value); break;
    case kParamTom1Decay: loTom_->setDecay(value); break;
    case kParamTom1Level: loTom_->setLevel(value); break;
    case kParamTom1Metal: loTom_->setMetal(value); break;
    case kParamTom2Pitch: hiTom_->setPitch(value); break;
    case kParamTom2Decay: hiTom_->setDecay(value); break;
    case kParamTom2Level: hiTom_->setLevel(value); break;
    case kParamTom2Metal: hiTom_->setMetal(value); break;
    case kParamClosedHHBrightness: closedHH_->setBrightness(value); break;
    case kParamClosedHHDecay: closedHH_->setDecay(value); break;
    case kParamClosedHHLevel: closedHH_->setLevel(value); break;
    case kParamClosedHHMetal: closedHH_->setMetal(value); break;
    case kParamOpenHHBrightness: openHH_->setBrightness(value); break;
    case kParamOpenHHDecay: openHH_->setDecay(value); break;
    case kParamOpenHHLevel: openHH_->setLevel(value); break;
    case kParamOpenHHMetal: openHH_->setMetal(value); break;
    case kParamCrashBrightness: crash_->setBrightness(value); break;
    case kParamCrashDecay: crash_->setDecay(value); break;
    case kParamCrashLevel: crash_->setLevel(value); break;
    case kParamCrashMetal: crash_->setMetal(value); break;
    case kParamCowbellTone: cowbell_->setTone(value); break;
    case kParamCowbellDecay: cowbell_->setDecay(value); break;
    case kParamCowbellLevel: cowbell_->setLevel(value); break;
    case kParamCowbellMetal: cowbell_->setMetal(value); break;
    case kParamClaveTone: clave_->setTone(value); break;
    case kParamClaveDecay: clave_->setDecay(value); break;
    case kParamClaveLevel: clave_->setLevel(value); break;
    case kParamClaveMetal: clave_->setMetal(value); break;
    case kParamBashSize: bash_->setSize(value); break;
    case kParamBashSpread: bash_->setSpread(value); break;
    case kParamBashDecay: bash_->setDecay(value); break;
    case kParamBashDrive: bash_->setDrive(value); break;
    case kParamBashNoise: bash_->setNoise(value); break;
    case kParamBashEdge: bash_->setEdge(value); break;
    case kParamBashLevel: bash_->setLevel(value); break;
    case kParamBitCrush:
        bitcrusherL_.setAmount(value);
        bitcrusherR_.setAmount(value);
        break;
    case kParamMasterDrive:
        distortionL_.setDrive(1.0f + value * 4.0f);
        distortionR_.setDrive(1.0f + value * 4.0f);
        break;
    case kParamMasterReverb:
        reverbL_.setLevel(value * 0.6f);
        reverbR_.setLevel(value * 0.6f);
        break;
    case kParamMasterGain:
        break;
    default:
        break;
    }
}

} // namespace downspout::drumkit
