#include "flues_synth_driver_processor.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::flues_synth_driver {
namespace {

float clampf(float v, float lo, float hi) noexcept { return std::clamp(v, lo, hi); }

// Convert a semantic param value to MIDI CC 0-127
std::uint8_t toCC(const SynthCC& spec, float value) noexcept
{
    if (spec.boolean)
        return value >= 0.5f ? 127 : 0;

    const float lo = spec.minimum;
    const float hi = spec.maximum;
    float normalized;
    if (spec.exponential && lo > 0.0f && hi > lo) {
        const float clamped = clampf(value, lo, hi);
        normalized = std::log(clamped / lo) / std::log(hi / lo);
    } else {
        normalized = (clampf(value, lo, hi) - lo) / (hi - lo);
    }
    return static_cast<std::uint8_t>(
        std::lround(clampf(normalized, 0.0f, 1.0f) * 127.0f));
}

}  // namespace

void Processor::init(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 44100.0;
    activate();
}

void Processor::activate()
{
    lastCC_.fill(-1);
    midiActivity_ = 0.0f;
    panicPending_ = false;

    // Load defaults
    for (std::size_t i = 0; i < kSynthParamCount; ++i)
        params_[i] = kSynthCCs[i].defaultValue;

    // Mark non-trajectory params dirty; trajectory params share CCs and
    // should only emit when explicitly set to avoid clobbering envelope/formants.
    dirty_.fill(false);
    const std::size_t trajStart = kParamTrajSides - kParamAlgorithm;
    for (std::size_t i = 0; i < trajStart; ++i)
        dirty_[i] = true;
}

// ── Setters ───────────────────────────────────────────────────────────────────

void Processor::markDirty(const std::size_t i) { dirty_[i] = true; }

#define SETI(idx, lo, hi, val) \
    params_[idx] = static_cast<float>(std::clamp(val, lo, hi)); markDirty(idx)
#define SETF(idx, lo, hi, val) \
    params_[idx] = clampf(val, lo, hi); markDirty(idx)
#define SETB(idx, val) \
    params_[idx] = (val) ? 1.0f : 0.0f; markDirty(idx)

void Processor::setAlgorithm(int v)       { SETI(kParamAlgorithm,     0, 17, v); }
void Processor::setDisynP1(float v)       { SETF(kParamDisynP1,       0.0f, 1.0f, v); }
void Processor::setDisynP2(float v)       { SETF(kParamDisynP2,       0.0f, 1.0f, v); }
void Processor::setDisynP3(float v)       { SETF(kParamDisynP3,       0.0f, 1.0f, v); }
void Processor::setNoiseLevel(float v)    { SETF(kParamNoiseLevel,    0.0f, 1.0f, v); }
void Processor::setDcLevel(float v)       { SETF(kParamDcLevel,       0.0f, 1.0f, v); }
void Processor::setInterfaceType(int v)   { SETI(kParamInterfaceType, 0, 11, v); }
void Processor::setIntensity(float v)     { SETF(kParamIntensity,     0.0f, 1.0f, v); }
void Processor::setF1(float v)            { SETF(kParamF1,            200.0f, 1000.0f, v); }
void Processor::setF2(float v)            { SETF(kParamF2,            500.0f, 3000.0f, v); }
void Processor::setF3(float v)            { SETF(kParamF3,            1500.0f, 4000.0f, v); }
void Processor::setF4(float v)            { SETF(kParamF4,            2500.0f, 4500.0f, v); }
void Processor::setNasal(bool v)          { SETB(kParamNasal,  v); }
void Processor::setSing(bool v)           { SETB(kParamSing,   v); }
void Processor::setShout(bool v)          { SETB(kParamShout,  v); }
void Processor::setFry(bool v)            { SETB(kParamFry,    v); }
void Processor::setAttack(float v)        { SETF(kParamAttack,        0.001f, 1.0f,    v); }
void Processor::setRelease(float v)       { SETF(kParamRelease,       0.01f,  3.0f,    v); }
void Processor::setTuning(float v)        { SETF(kParamTuning,        -12.0f, 12.0f,   v); }
void Processor::setDelay1Fb(float v)      { SETF(kParamDelay1Fb,      0.0f, 1.0f, v); }
void Processor::setDelay2Fb(float v)      { SETF(kParamDelay2Fb,      0.0f, 1.0f, v); }
void Processor::setDelayRatio(float v)    { SETF(kParamDelayRatio,    0.5f, 2.0f, v); }
void Processor::setFilterFb(float v)      { SETF(kParamFilterFb,      0.0f, 1.0f, v); }
void Processor::setFilterFreq(float v)    { SETF(kParamFilterFreq,    20.0f, 20000.0f, v); }
void Processor::setFilterQ(float v)       { SETF(kParamFilterQ,       0.1f, 10.0f, v); }
void Processor::setFilterShape(float v)   { SETF(kParamFilterShape,   0.0f, 1.0f, v); }
void Processor::setLfoFreq(float v)       { SETF(kParamLfoFreq,       0.1f, 20.0f, v); }
void Processor::setAmFmDepth(float v)     { SETF(kParamAmFmDepth,     -1.0f, 1.0f, v); }
void Processor::setGain(float v)          { SETF(kParamGain,          0.0f, 1.0f, v); }
void Processor::setTrajSides(int v)       { SETI(kParamTrajSides,     3, 24, v); }
void Processor::setTrajStartPos(float v)  { SETF(kParamTrajStartPos,  0.0f, 360.0f, v); }
void Processor::setTrajStartAngle(float v){ SETF(kParamTrajStartAngle,0.0f, 360.0f, v); }
void Processor::setTrajJitter(float v)    { SETF(kParamTrajJitter,    0.0f, 10.0f, v); }
void Processor::setTrajClip(float v)      { SETF(kParamTrajClip,      0.0f, 1.0f, v); }
void Processor::setTrajMixX(float v)      { SETF(kParamTrajMixX,      0.0f, 1.0f, v); }
void Processor::setTrajMixY(float v)      { SETF(kParamTrajMixY,      0.0f, 1.0f, v); }

#undef SETI
#undef SETF
#undef SETB

void Processor::setProgram(const int prog)
{
    program_        = std::clamp(prog, 0, 30);
    programPending_ = true;
}

void Processor::setOutputChannel(const int ch)
{
    outputChannel_ = std::clamp(ch, 1, 16);
}

void Processor::setConductorCh(const int ch)
{
    conductorCh_ = std::clamp(ch, 0, 16);
}

void Processor::setPassInput(const bool v)
{
    passInput_ = v;
}

void Processor::triggerPanic()
{
    panicPending_ = true;
}

void Processor::triggerRandomize()
{
    randomizePending_ = true;
}

float Processor::getSynthParam(const std::size_t paramIndex) const noexcept
{
    if (paramIndex < kSynthParamCount)
        return params_[paramIndex];
    return 0.0f;
}

// ── CC helpers ────────────────────────────────────────────────────────────────

std::uint8_t Processor::computeCC(const std::size_t i) const noexcept
{
    return toCC(kSynthCCs[i], params_[i]);
}

void Processor::emitCC(ProcessResult& result, const std::uint32_t frame,
                       const std::uint8_t ch, const std::uint8_t cc,
                       const std::uint8_t value)
{
    if (result.eventCount >= kMaxOutputEvents)
        return;
    MidiMessage& m = result.events[result.eventCount++];
    m.frame   = frame;
    m.size    = 3;
    m.data[0] = static_cast<std::uint8_t>(0xB0 | (ch - 1));
    m.data[1] = cc;
    m.data[2] = value;
}

void Processor::emitAllNotesOff(ProcessResult& result, const std::uint32_t frame)
{
    for (int ch = 1; ch <= 16; ++ch)
        emitCC(result, frame, static_cast<std::uint8_t>(ch), 123, 0);
}

void Processor::emitSliderCCs(ProcessResult& result, const std::uint32_t frame)
{
    // Send the 9 slider CCs with current param values for the current program
    for (int s = 0; s < 9; ++s) {
        const FlueSynthParam fsp = kProgramMap[program_][s];
        const Param p = flueSynthParamToParam(fsp);
        if (p >= kParamCount) continue;
        const std::size_t idx = static_cast<std::size_t>(p - kParamAlgorithm);
        if (idx >= kSynthParamCount) continue;
        const std::uint8_t ccVal = toCC(kSynthCCs[idx], params_[idx]);
        emitCC(result, frame, static_cast<std::uint8_t>(outputChannel_),
               kSliderCCs[static_cast<std::size_t>(s)], ccVal);
    }
}

void Processor::emitDefaultCCs(ProcessResult& result, const std::uint32_t frame)
{
    emitSliderCCs(result, frame);
}

// ── Conductor CC handling ─────────────────────────────────────────────────────

void Processor::handleConductorCC(ProcessResult& result, const std::uint32_t frame,
                                   const std::uint8_t cc, const std::uint8_t value)
{
    const float n = value / 127.0f;
    if (cc == kConductorCcScene) {
        // Scene → Interface Type (0-11)
        const int t = static_cast<int>(std::lround(n * 11.0f));
        setInterfaceType(t);
        // Emit immediately (dirty will also fire next block, but emit now for tight timing)
        emitCC(result, frame, static_cast<std::uint8_t>(outputChannel_), 24,
               toCC(kSynthCCs[kParamInterfaceType], params_[kParamInterfaceType]));
        dirty_[kParamInterfaceType] = false;
        lastCC_[kParamInterfaceType] = computeCC(kParamInterfaceType);
    } else if (cc == kConductorCcDensity) {
        // Density → Disyn P1
        setDisynP1(n);
    } else if (cc == kConductorCcEnergy) {
        // Energy → Master Gain
        setGain(n);
    } else if (cc == kConductorCcMutation) {
        // Mutation → Algorithm (0-17)
        setAlgorithm(static_cast<int>(std::lround(n * 17.0f)));
    } else if (cc == kConductorCcReset) {
        // Reset → panic + restore defaults
        panicPending_ = true;
        dirty_.fill(true);
    }
}

// ── LCG random helper ─────────────────────────────────────────────────────────

namespace {
float randomFloat(std::uint32_t& state) noexcept
{
    state = state * 1664525u + 1013904223u;
    return static_cast<float>(state >> 16) / 65535.0f;
}
}  // namespace

// ── Main process block ────────────────────────────────────────────────────────

ProcessResult Processor::processBlock(const std::uint32_t frameCount,
                                      const MidiMessage* inputEvents,
                                      const std::uint32_t inputEventCount)
{
    ProcessResult result {};

    // Panic
    if (panicPending_) {
        panicPending_      = false;
        result.wasPanicked = true;
        emitAllNotesOff(result, 0);
        emitDefaultCCs(result, 0);
        dirty_.fill(false);
        lastCC_.fill(-1);
    }

    // Program Change: emit PC then resend all 9 slider CCs for the new program
    if (programPending_) {
        programPending_ = false;
        lastCC_.fill(-1);
        if (result.eventCount < kMaxOutputEvents) {
            MidiMessage& m = result.events[result.eventCount++];
            m.frame   = 0;
            m.size    = 2;
            m.data[0] = static_cast<std::uint8_t>(0xC0 | (outputChannel_ - 1));
            m.data[1] = static_cast<std::uint8_t>(program_);
        }
        emitSliderCCs(result, 0);
    }

    // Randomize: set all non-boolean synth params to random values in range
    if (randomizePending_) {
        randomizePending_   = false;
        result.wasRandomized = true;
        const std::size_t trajStart = kParamTrajSides - kParamAlgorithm;
        for (std::size_t i = 0; i < trajStart; ++i) {
            const SynthCC& spec = kSynthCCs[i];
            if (spec.boolean) continue;
            const float r = randomFloat(randomState_);
            params_[i] = spec.minimum + r * (spec.maximum - spec.minimum);
            dirty_[i]  = true;
        }
    }

    // Emit dirty params on their slider CC for the current program
    for (std::size_t i = 0; i < kSynthParamCount; ++i) {
        if (!dirty_[i])
            continue;
        dirty_[i] = false;
        const Param p = static_cast<Param>(kParamAlgorithm + i);
        const int sliderIdx = getSliderIndex(program_, paramToFlueSynthParam(p));
        if (sliderIdx < 0)
            continue; // not controllable in current program
        const int ccVal = static_cast<int>(computeCC(i));
        if (ccVal == lastCC_[i])
            continue;
        lastCC_[i] = ccVal;
        emitCC(result, 0,
               static_cast<std::uint8_t>(outputChannel_),
               kSliderCCs[static_cast<std::size_t>(sliderIdx)],
               static_cast<std::uint8_t>(ccVal));
    }

    // Process input events
    for (std::uint32_t e = 0; e < inputEventCount; ++e) {
        const MidiMessage& msg = inputEvents[e];
        if (msg.size < 1)
            continue;

        const std::uint8_t status  = msg.data[0] & 0xF0;
        const std::uint8_t channel = (msg.data[0] & 0x0F) + 1;

        // Intercept Conductor CCs on the configured channel
        if (conductorCh_ > 0 && status == 0xB0 && channel == static_cast<std::uint8_t>(conductorCh_)) {
            handleConductorCC(result, msg.frame, msg.data[1], msg.data[2]);
            // Do NOT forward Conductor CCs downstream
            continue;
        }

        if (passInput_) {
            if (result.eventCount < kMaxOutputEvents)
                result.events[result.eventCount++] = msg;
            midiActivity_ = 1.0f;
        }
    }

    // Decay activity indicator
    if (midiActivity_ > 0.0f)
        midiActivity_ = std::max(0.0f, midiActivity_ - static_cast<float>(frameCount) / static_cast<float>(sampleRate_) * 8.0f);

    result.midiActivity = midiActivity_;
    return result;
}

}  // namespace downspout::flues_synth_driver
