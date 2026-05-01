#include "gremlin_processor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace downspout::gremlin {
namespace {

template <typename T>
constexpr auto idx(const T id) noexcept
{
    return static_cast<std::size_t>(id);
}

float clampf(const float value, const float minValue = 0.0f, const float maxValue = 1.0f) noexcept
{
    return std::clamp(value, minValue, maxValue);
}

bool isControllerButtonNote(const std::uint8_t note) noexcept
{
    return std::find(kMuteNotes.begin(), kMuteNotes.end(), note) != kMuteNotes.end()
        || std::find(kSoloMuteNotes.begin(), kSoloMuteNotes.end(), note) != kSoloMuteNotes.end()
        || std::find(kRecArmNotes.begin(), kRecArmNotes.end(), note) != kRecArmNotes.end()
        || note == kBankLeftNote
        || note == kBankRightNote
        || note == kSoloNote;
}

template <std::size_t Count>
int findIndex(const std::array<std::uint8_t, Count>& ids, const std::uint8_t value) noexcept
{
    for (std::size_t i = 0; i < ids.size(); ++i)
    {
        if (ids[i] == value)
            return static_cast<int>(i);
    }
    return -1;
}

}  // namespace

void Processor::init(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    engine_ = flues::gremlin::GremlinEngine(static_cast<float>(sampleRate_));
    activate();
}

void Processor::activate()
{
    engine_ = flues::gremlin::GremlinEngine(static_cast<float>(sampleRate_));
    rngState_ = 0x4d3c2b1au;
    currentNote_ = -1;
    resetToDefaults();
    applyLiveState();
}

void Processor::resetToDefaults()
{
    for (std::size_t i = 0; i < kLiveParamSpecs.size(); ++i)
        live_[i] = kLiveParamSpecs[i].defaultValue;

    for (std::size_t i = 0; i < kHiddenParamSpecs.size(); ++i)
        hidden_[i] = kHiddenParamSpecs[i].defaultValue;

    macros_.fill(0.5f);
    momentary_.fill(false);
    masterTrim_ = 0.45f;
    status_.controllerActivity = 0.0f;
    status_.currentScene = static_cast<std::uint32_t>(SceneId::manual);
}

void Processor::setLiveParameter(const LiveParamId id, float value)
{
    const ParamSpec& spec = kLiveParamSpecs[idx(id)];
    if (spec.integer)
        value = std::round(value);
    live_[idx(id)] = clampf(value, spec.minimum, spec.maximum);
    status_.currentScene = static_cast<std::uint32_t>(SceneId::manual);
}

void Processor::setHiddenParameter(const HiddenParamId id, float value)
{
    const ParamSpec& spec = kHiddenParamSpecs[idx(id)];
    hidden_[idx(id)] = clampf(value, spec.minimum, spec.maximum);
    status_.currentScene = static_cast<std::uint32_t>(SceneId::manual);
}

void Processor::setMacro(const MacroId id, const float value)
{
    macros_[idx(id)] = clampf(value);
}

void Processor::setMomentary(const MomentaryId id, const bool enabled)
{
    momentary_[idx(id)] = enabled;
}

void Processor::setMasterTrim(const float value)
{
    masterTrim_ = clampf(value);
}

float Processor::getLiveParameter(const LiveParamId id) const noexcept
{
    return live_[idx(id)];
}

float Processor::getHiddenParameter(const HiddenParamId id) const noexcept
{
    return hidden_[idx(id)];
}

float Processor::getMacro(const MacroId id) const noexcept
{
    return macros_[idx(id)];
}

bool Processor::getMomentary(const MomentaryId id) const noexcept
{
    return momentary_[idx(id)];
}

float Processor::getMasterTrim() const noexcept
{
    return masterTrim_;
}

const Status& Processor::getStatus() const noexcept
{
    return status_;
}

void Processor::loadScene(const SceneId scene)
{
    switch (scene)
    {
    case SceneId::splinter:
        live_[idx(LiveParamId::mode)] = 0.0f;
        live_[idx(LiveParamId::damage)] = 0.34f;
        live_[idx(LiveParamId::chaos)] = 0.26f;
        live_[idx(LiveParamId::noise)] = 0.05f;
        live_[idx(LiveParamId::drift)] = 0.12f;
        live_[idx(LiveParamId::crunch)] = 0.10f;
        live_[idx(LiveParamId::fold)] = 0.12f;
        live_[idx(LiveParamId::delayTime)] = 0.12f;
        live_[idx(LiveParamId::feedback)] = 0.28f;
        live_[idx(LiveParamId::warp)] = 0.12f;
        live_[idx(LiveParamId::stutter)] = 0.18f;
        live_[idx(LiveParamId::tone)] = 0.84f;
        live_[idx(LiveParamId::damping)] = 0.68f;
        live_[idx(LiveParamId::space)] = 0.20f;
        live_[idx(LiveParamId::attack)] = 0.01f;
        live_[idx(LiveParamId::release)] = 0.10f;
        live_[idx(LiveParamId::output)] = 0.38f;
        hidden_[idx(HiddenParamId::sourceGain)] = 0.70f;
        hidden_[idx(HiddenParamId::burst)] = 0.88f;
        hidden_[idx(HiddenParamId::pitchSpread)] = 0.16f;
        hidden_[idx(HiddenParamId::delayMix)] = 0.22f;
        hidden_[idx(HiddenParamId::crossFeedback)] = 0.16f;
        hidden_[idx(HiddenParamId::glitchLength)] = 0.14f;
        hidden_[idx(HiddenParamId::chaosRate)] = 0.18f;
        hidden_[idx(HiddenParamId::duck)] = 0.40f;
        break;
    case SceneId::melt:
        live_[idx(LiveParamId::mode)] = 1.0f;
        live_[idx(LiveParamId::damage)] = 0.30f;
        live_[idx(LiveParamId::chaos)] = 0.46f;
        live_[idx(LiveParamId::noise)] = 0.08f;
        live_[idx(LiveParamId::drift)] = 0.24f;
        live_[idx(LiveParamId::crunch)] = 0.14f;
        live_[idx(LiveParamId::fold)] = 0.16f;
        live_[idx(LiveParamId::delayTime)] = 0.34f;
        live_[idx(LiveParamId::feedback)] = 0.48f;
        live_[idx(LiveParamId::warp)] = 0.34f;
        live_[idx(LiveParamId::stutter)] = 0.20f;
        live_[idx(LiveParamId::tone)] = 0.74f;
        live_[idx(LiveParamId::damping)] = 0.56f;
        live_[idx(LiveParamId::space)] = 0.44f;
        live_[idx(LiveParamId::attack)] = 0.02f;
        live_[idx(LiveParamId::release)] = 0.18f;
        live_[idx(LiveParamId::output)] = 0.40f;
        hidden_[idx(HiddenParamId::sourceGain)] = 0.62f;
        hidden_[idx(HiddenParamId::burst)] = 0.76f;
        hidden_[idx(HiddenParamId::pitchSpread)] = 0.28f;
        hidden_[idx(HiddenParamId::delayMix)] = 0.42f;
        hidden_[idx(HiddenParamId::crossFeedback)] = 0.36f;
        hidden_[idx(HiddenParamId::glitchLength)] = 0.22f;
        hidden_[idx(HiddenParamId::chaosRate)] = 0.34f;
        hidden_[idx(HiddenParamId::duck)] = 0.32f;
        break;
    case SceneId::rust:
        live_[idx(LiveParamId::mode)] = 2.0f;
        live_[idx(LiveParamId::damage)] = 0.48f;
        live_[idx(LiveParamId::chaos)] = 0.56f;
        live_[idx(LiveParamId::noise)] = 0.16f;
        live_[idx(LiveParamId::drift)] = 0.26f;
        live_[idx(LiveParamId::crunch)] = 0.34f;
        live_[idx(LiveParamId::fold)] = 0.24f;
        live_[idx(LiveParamId::delayTime)] = 0.18f;
        live_[idx(LiveParamId::feedback)] = 0.34f;
        live_[idx(LiveParamId::warp)] = 0.24f;
        live_[idx(LiveParamId::stutter)] = 0.40f;
        live_[idx(LiveParamId::tone)] = 0.68f;
        live_[idx(LiveParamId::damping)] = 0.54f;
        live_[idx(LiveParamId::space)] = 0.36f;
        live_[idx(LiveParamId::attack)] = 0.01f;
        live_[idx(LiveParamId::release)] = 0.14f;
        live_[idx(LiveParamId::output)] = 0.42f;
        hidden_[idx(HiddenParamId::sourceGain)] = 0.74f;
        hidden_[idx(HiddenParamId::burst)] = 0.82f;
        hidden_[idx(HiddenParamId::pitchSpread)] = 0.42f;
        hidden_[idx(HiddenParamId::delayMix)] = 0.32f;
        hidden_[idx(HiddenParamId::crossFeedback)] = 0.28f;
        hidden_[idx(HiddenParamId::glitchLength)] = 0.32f;
        hidden_[idx(HiddenParamId::chaosRate)] = 0.44f;
        hidden_[idx(HiddenParamId::duck)] = 0.26f;
        break;
    case SceneId::tunnel:
        live_[idx(LiveParamId::mode)] = 3.0f;
        live_[idx(LiveParamId::damage)] = 0.40f;
        live_[idx(LiveParamId::chaos)] = 0.52f;
        live_[idx(LiveParamId::noise)] = 0.12f;
        live_[idx(LiveParamId::drift)] = 0.34f;
        live_[idx(LiveParamId::crunch)] = 0.20f;
        live_[idx(LiveParamId::fold)] = 0.18f;
        live_[idx(LiveParamId::delayTime)] = 0.44f;
        live_[idx(LiveParamId::feedback)] = 0.62f;
        live_[idx(LiveParamId::warp)] = 0.50f;
        live_[idx(LiveParamId::stutter)] = 0.28f;
        live_[idx(LiveParamId::tone)] = 0.58f;
        live_[idx(LiveParamId::damping)] = 0.66f;
        live_[idx(LiveParamId::space)] = 0.62f;
        live_[idx(LiveParamId::attack)] = 0.02f;
        live_[idx(LiveParamId::release)] = 0.24f;
        live_[idx(LiveParamId::output)] = 0.40f;
        hidden_[idx(HiddenParamId::sourceGain)] = 0.58f;
        hidden_[idx(HiddenParamId::burst)] = 0.68f;
        hidden_[idx(HiddenParamId::pitchSpread)] = 0.30f;
        hidden_[idx(HiddenParamId::delayMix)] = 0.56f;
        hidden_[idx(HiddenParamId::crossFeedback)] = 0.50f;
        hidden_[idx(HiddenParamId::glitchLength)] = 0.28f;
        hidden_[idx(HiddenParamId::chaosRate)] = 0.40f;
        hidden_[idx(HiddenParamId::duck)] = 0.36f;
        break;
    case SceneId::manual:
    default:
        return;
    }

    status_.currentScene = static_cast<std::uint32_t>(scene);
    engine_.triggerBurst(0.65f);
}

void Processor::triggerAction(const ActionId action)
{
    switch (action)
    {
    case ActionId::reseed:
        reseedEngine();
        break;
    case ActionId::burst:
        engine_.triggerBurst(1.0f);
        break;
    case ActionId::randomSource:
        randomizeSource();
        break;
    case ActionId::randomDelay:
        randomizeDelay();
        break;
    case ActionId::randomAll:
        randomizeSource();
        randomizeDelay();
        break;
    case ActionId::panic:
        panic();
        break;
    }
}

void Processor::processBlock(float* outLeft,
                             float* outRight,
                             const std::uint32_t frameCount,
                             const MidiMessage* midiEvents,
                             const std::uint32_t midiEventCount)
{
    if (outLeft == nullptr && outRight == nullptr)
        return;

    const float activityDrop = static_cast<float>(frameCount) / std::max(1.0, sampleRate_ * 0.35);
    status_.controllerActivity = std::max(0.0f, status_.controllerActivity - activityDrop);

    if (outLeft != nullptr)
        std::memset(outLeft, 0, frameCount * sizeof(float));
    if (outRight != nullptr)
        std::memset(outRight, 0, frameCount * sizeof(float));

    applyLiveState();

    std::uint32_t frame = 0;
    for (std::uint32_t i = 0; i < midiEventCount; ++i)
    {
        const MidiMessage& event = midiEvents[i];
        const std::uint32_t eventFrame = std::min(event.frame, frameCount);
        renderRange(outLeft, outRight, frame, eventFrame);
        frame = eventFrame;

        if (handleMidiMessage(event))
            applyLiveState();
    }

    renderRange(outLeft, outRight, frame, frameCount);
}

void Processor::applyLiveState()
{
    float damage = live_[idx(LiveParamId::damage)];
    float chaos = live_[idx(LiveParamId::chaos)];
    float noise = live_[idx(LiveParamId::noise)];
    float drift = live_[idx(LiveParamId::drift)];
    float crunch = live_[idx(LiveParamId::crunch)];
    float fold = live_[idx(LiveParamId::fold)];
    float delayTime = live_[idx(LiveParamId::delayTime)];
    float feedback = live_[idx(LiveParamId::feedback)];
    float warp = live_[idx(LiveParamId::warp)];
    float stutter = live_[idx(LiveParamId::stutter)];
    float tone = live_[idx(LiveParamId::tone)];
    float damping = live_[idx(LiveParamId::damping)];
    float space = live_[idx(LiveParamId::space)];
    float attack = live_[idx(LiveParamId::attack)];
    float release = live_[idx(LiveParamId::release)];
    float output = live_[idx(LiveParamId::output)];

    float sourceGain = hidden_[idx(HiddenParamId::sourceGain)];
    float burst = hidden_[idx(HiddenParamId::burst)];
    float pitchSpread = hidden_[idx(HiddenParamId::pitchSpread)];
    float delayMix = hidden_[idx(HiddenParamId::delayMix)];
    float crossFeedback = hidden_[idx(HiddenParamId::crossFeedback)];
    float glitchLength = hidden_[idx(HiddenParamId::glitchLength)];
    float chaosRate = hidden_[idx(HiddenParamId::chaosRate)];
    float duck = hidden_[idx(HiddenParamId::duck)];

    const float sourceMacro = macros_[idx(MacroId::source)] - 0.5f;
    const float pitchMacro = macros_[idx(MacroId::pitch)] - 0.5f;
    const float breakMacro = macros_[idx(MacroId::breakage)] - 0.5f;
    const float delayMacro = macros_[idx(MacroId::delay)] - 0.5f;
    const float spaceMacro = macros_[idx(MacroId::space)] - 0.5f;
    const float stutterMacro = macros_[idx(MacroId::stutter)] - 0.5f;
    const float toneMacro = macros_[idx(MacroId::tone)] - 0.5f;
    const float outputMacro = macros_[idx(MacroId::output)] - 0.5f;

    damage = clampf(damage + sourceMacro * 0.18f + breakMacro * 0.26f);
    chaos = clampf(chaos + sourceMacro * 0.20f + pitchMacro * 0.12f + spaceMacro * 0.08f
                   + (momentary_[idx(MomentaryId::chaosBoost)] ? 0.28f : 0.0f));
    noise = clampf(noise + sourceMacro * 0.10f + breakMacro * 0.10f - toneMacro * 0.14f
                   + (momentary_[idx(MomentaryId::noiseBurst)] ? 0.46f : 0.0f));
    drift = clampf(drift + pitchMacro * 0.48f);
    attack = clampf(attack - pitchMacro * 0.20f - sourceMacro * 0.08f);
    release = clampf(release - pitchMacro * 0.16f + delayMacro * 0.10f);

    crunch = clampf(crunch + breakMacro * 0.45f + stutterMacro * 0.10f
                    + (momentary_[idx(MomentaryId::crunchBlast)] ? 0.44f : 0.0f));
    fold = clampf(fold + breakMacro * 0.34f + sourceMacro * 0.08f);

    delayTime = clampf(delayTime + delayMacro * 0.42f - stutterMacro * 0.08f);
    feedback = clampf(feedback + delayMacro * 0.34f + spaceMacro * 0.12f
                      + (momentary_[idx(MomentaryId::feedbackPush)] ? 0.26f : 0.0f));
    warp = clampf(warp + delayMacro * 0.12f + spaceMacro * 0.30f
                  + (momentary_[idx(MomentaryId::warpPush)] ? 0.32f : 0.0f));
    stutter = clampf(stutter + stutterMacro * 0.42f + delayMacro * 0.12f
                     + (momentary_[idx(MomentaryId::stutterMax)] ? 0.68f : 0.0f));

    tone = clampf(tone + toneMacro * 0.42f - breakMacro * 0.08f);
    damping = clampf(damping - toneMacro * 0.32f + spaceMacro * 0.10f);
    space = clampf(space + spaceMacro * 0.48f + delayMacro * 0.10f);
    output = clampf(output + outputMacro * 0.34f);

    sourceGain = clampf(sourceGain + sourceMacro * 0.40f + outputMacro * 0.08f);
    burst = clampf(burst + sourceMacro * 0.34f + pitchMacro * 0.18f + stutterMacro * 0.08f);
    pitchSpread = clampf(pitchSpread + pitchMacro * 0.48f + sourceMacro * 0.12f);
    delayMix = clampf(delayMix + delayMacro * 0.36f + spaceMacro * 0.08f);
    crossFeedback = clampf(crossFeedback + spaceMacro * 0.36f);
    glitchLength = clampf(glitchLength + stutterMacro * 0.34f + delayMacro * 0.10f);
    chaosRate = clampf(chaosRate + sourceMacro * 0.12f + spaceMacro * 0.10f + pitchMacro * 0.06f);
    duck = clampf(duck + delayMacro * 0.18f + toneMacro * 0.20f
                  - (momentary_[idx(MomentaryId::duckKill)] ? 0.55f : 0.0f));

    status_.effective[0] = live_[idx(LiveParamId::mode)];
    status_.effective[1] = damage;
    status_.effective[2] = chaos;
    status_.effective[3] = noise;
    status_.effective[4] = drift;
    status_.effective[5] = crunch;
    status_.effective[6] = fold;
    status_.effective[7] = delayTime;
    status_.effective[8] = feedback;
    status_.effective[9] = warp;
    status_.effective[10] = stutter;
    status_.effective[11] = tone;
    status_.effective[12] = damping;
    status_.effective[13] = space;
    status_.effective[14] = attack;
    status_.effective[15] = release;
    status_.effective[16] = output;
    status_.effective[17] = sourceGain;
    status_.effective[18] = burst;
    status_.effective[19] = pitchSpread;
    status_.effective[20] = delayMix;
    status_.effective[21] = crossFeedback;
    status_.effective[22] = glitchLength;
    status_.effective[23] = chaosRate;
    status_.effective[24] = duck;

    engine_.setMode(live_[idx(LiveParamId::mode)]);
    engine_.setDamage(damage);
    engine_.setChaos(chaos);
    engine_.setNoise(noise);
    engine_.setDrift(drift);
    engine_.setCrunch(crunch);
    engine_.setFold(fold);
    engine_.setDelayTime(delayTime);
    engine_.setFeedback(feedback);
    engine_.setWarp(warp);
    engine_.setStutter(stutter);
    engine_.setTone(tone);
    engine_.setDamping(damping);
    engine_.setSpace(space);
    engine_.setAttack(attack);
    engine_.setRelease(release);
    engine_.setOutput(output);
    engine_.setSourceGain(sourceGain);
    engine_.setBurst(burst);
    engine_.setPitchSpread(pitchSpread);
    engine_.setDelayMix(delayMix);
    engine_.setCrossFeedback(crossFeedback);
    engine_.setGlitchLength(glitchLength);
    engine_.setChaosRate(chaosRate);
    engine_.setDuck(duck);
    engine_.setFreezeDelay(momentary_[idx(MomentaryId::freeze)]);

    postGain_ = 0.05f + masterTrim_ * 0.75f;
}

bool Processor::handleMidiMessage(const MidiMessage& message)
{
    if (message.size == 0)
        return false;

    const std::uint8_t status = message.data[0] & 0xF0u;
    const std::uint8_t data1 = message.size > 1 ? message.data[1] : 0u;
    const std::uint8_t data2 = message.size > 2 ? message.data[2] : 0u;

    switch (status)
    {
    case 0x90:
        if (data2 == 0u)
        {
            if (isControllerButtonNote(data1))
                return handleControllerButton(data1, false);

            if (currentNote_ == static_cast<int>(data1))
            {
                engine_.noteOff(data1);
                currentNote_ = -1;
            }
            return false;
        }

        if (data1 <= kSoloNote && data2 == 127u && isControllerButtonNote(data1))
            return handleControllerButton(data1, true);

        engine_.noteOn(data1, static_cast<float>(data2) / 127.0f);
        currentNote_ = static_cast<int>(data1);
        return false;
    case 0x80:
        if (isControllerButtonNote(data1))
            return handleControllerButton(data1, false);

        if (currentNote_ == static_cast<int>(data1))
        {
            engine_.noteOff(data1);
            currentNote_ = -1;
        }
        return false;
    case 0xB0:
        if (data1 == 120u || data1 == 123u)
        {
            engine_.allNotesOff();
            currentNote_ = -1;
            return false;
        }
        return handleControllerCC(data1, data2);
    default:
        return false;
    }
}

bool Processor::handleControllerCC(const std::uint8_t cc, const std::uint8_t value)
{
    const float normalized = static_cast<float>(value) / 127.0f;

    const int primary = findIndex(kPrimaryKnobCCs, cc);
    if (primary >= 0)
    {
        setLiveParameter(kPrimaryKnobTargets[static_cast<std::size_t>(primary)], normalized);
        status_.controllerActivity = 1.0f;
        return true;
    }

    const int hidden = findIndex(kHiddenKnobCCs, cc);
    if (hidden >= 0)
    {
        setHiddenParameter(kHiddenKnobTargets[static_cast<std::size_t>(hidden)], normalized);
        status_.controllerActivity = 1.0f;
        return true;
    }

    const int macro = findIndex(kMacroFaderCCs, cc);
    if (macro >= 0)
    {
        setMacro(static_cast<MacroId>(macro), normalized);
        status_.controllerActivity = 1.0f;
        return true;
    }

    if (cc == kMasterFaderCC)
    {
        setMasterTrim(normalized);
        status_.controllerActivity = 1.0f;
        return true;
    }

    return false;
}

bool Processor::handleControllerButton(const std::uint8_t note, const bool pressed)
{
    const int muteIndex = findIndex(kMuteNotes, note);
    if (muteIndex >= 0)
    {
        setMomentary(static_cast<MomentaryId>(muteIndex), pressed);
        status_.controllerActivity = 1.0f;
        return true;
    }

    if (!pressed)
        return false;

    const int recIndex = findIndex(kRecArmNotes, note);
    if (recIndex >= 0)
    {
        if (recIndex < 4)
            setLiveParameter(LiveParamId::mode, static_cast<float>(recIndex));
        else
            loadScene(static_cast<SceneId>(recIndex - 3));
        status_.controllerActivity = 1.0f;
        return true;
    }

    const int actionIndex = findIndex(kSoloMuteNotes, note);
    if (actionIndex >= 0)
    {
        switch (actionIndex)
        {
        case 0: triggerAction(ActionId::reseed); break;
        case 1: triggerAction(ActionId::burst); break;
        case 2: triggerAction(ActionId::randomSource); break;
        case 3: triggerAction(ActionId::randomDelay); break;
        case 4: triggerAction(ActionId::randomAll); break;
        case 5: cycleScene(-1); break;
        case 6: cycleScene(1); break;
        case 7: triggerAction(ActionId::panic); break;
        default: break;
        }
        status_.controllerActivity = 1.0f;
        return true;
    }

    if (note == kBankLeftNote)
    {
        cycleMode(-1);
        status_.controllerActivity = 1.0f;
        return true;
    }
    if (note == kBankRightNote)
    {
        cycleMode(1);
        status_.controllerActivity = 1.0f;
        return true;
    }

    return false;
}

void Processor::cycleMode(const int delta)
{
    const int mode = (static_cast<int>(std::lround(live_[idx(LiveParamId::mode)])) + delta + 4) % 4;
    setLiveParameter(LiveParamId::mode, static_cast<float>(mode));
}

void Processor::cycleScene(const int delta)
{
    const int scene = static_cast<int>(status_.currentScene == 0u
        ? static_cast<std::uint32_t>(SceneId::splinter)
        : status_.currentScene);
    int nextScene = scene + delta;
    if (nextScene < 1)
        nextScene = static_cast<int>(SceneId::tunnel);
    if (nextScene > static_cast<int>(SceneId::tunnel))
        nextScene = static_cast<int>(SceneId::splinter);
    loadScene(static_cast<SceneId>(nextScene));
}

void Processor::randomizeSource()
{
    setLiveParameter(LiveParamId::mode, static_cast<float>(static_cast<int>(randomUnit() * 4.0f) % 4));
    setLiveParameter(LiveParamId::damage, randomRange(0.18f, 0.72f));
    setLiveParameter(LiveParamId::chaos, randomRange(0.12f, 0.68f));
    setLiveParameter(LiveParamId::noise, randomRange(0.0f, 0.30f));
    setLiveParameter(LiveParamId::drift, randomRange(0.04f, 0.48f));
    setLiveParameter(LiveParamId::crunch, randomRange(0.0f, 0.44f));
    setLiveParameter(LiveParamId::fold, randomRange(0.04f, 0.50f));
    setLiveParameter(LiveParamId::attack, randomRange(0.0f, 0.08f));
    setLiveParameter(LiveParamId::release, randomRange(0.06f, 0.24f));
    setHiddenParameter(HiddenParamId::sourceGain, randomRange(0.50f, 0.82f));
    setHiddenParameter(HiddenParamId::burst, randomRange(0.56f, 0.96f));
    setHiddenParameter(HiddenParamId::pitchSpread, randomRange(0.08f, 0.56f));
    setHiddenParameter(HiddenParamId::chaosRate, randomRange(0.10f, 0.56f));
    reseedEngine();
}

void Processor::randomizeDelay()
{
    setLiveParameter(LiveParamId::delayTime, randomRange(0.06f, 0.56f));
    setLiveParameter(LiveParamId::feedback, randomRange(0.18f, 0.72f));
    setLiveParameter(LiveParamId::warp, randomRange(0.05f, 0.58f));
    setLiveParameter(LiveParamId::stutter, randomRange(0.06f, 0.55f));
    setLiveParameter(LiveParamId::tone, randomRange(0.42f, 0.92f));
    setLiveParameter(LiveParamId::damping, randomRange(0.38f, 0.78f));
    setLiveParameter(LiveParamId::space, randomRange(0.18f, 0.70f));
    setHiddenParameter(HiddenParamId::delayMix, randomRange(0.18f, 0.66f));
    setHiddenParameter(HiddenParamId::crossFeedback, randomRange(0.10f, 0.58f));
    setHiddenParameter(HiddenParamId::glitchLength, randomRange(0.10f, 0.48f));
    setHiddenParameter(HiddenParamId::duck, randomRange(0.18f, 0.55f));
}

void Processor::panic()
{
    engine_.allNotesOff();
    currentNote_ = -1;
    momentary_.fill(false);
}

void Processor::renderRange(float* outLeft,
                            float* outRight,
                            const std::uint32_t beginFrame,
                            const std::uint32_t endFrame)
{
    for (std::uint32_t frame = beginFrame; frame < endFrame; ++frame)
    {
        const flues::gremlin::StereoFrame sample = engine_.process();
        if (outLeft != nullptr)
            outLeft[frame] = 0.92f * std::tanh(sample.left * postGain_);
        if (outRight != nullptr)
            outRight[frame] = 0.92f * std::tanh(sample.right * postGain_);
    }
}

std::uint32_t Processor::nextRandom()
{
    rngState_ = rngState_ * 1664525u + 1013904223u;
    return rngState_;
}

float Processor::randomUnit()
{
    return static_cast<float>((nextRandom() >> 8u) & 0x00ffffffu) * (1.0f / 16777215.0f);
}

float Processor::randomRange(const float minValue, const float maxValue)
{
    return minValue + (maxValue - minValue) * randomUnit();
}

void Processor::reseedEngine()
{
    engine_.reseedChaos(nextRandom());
}

}  // namespace downspout::gremlin
