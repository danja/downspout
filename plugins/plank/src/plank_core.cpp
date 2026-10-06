#include "plank_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::plank {
namespace {

constexpr std::uint8_t kProgrammerModeSysex[] = {
    0xf0, 0x00, 0x20, 0x29, 0x02, 0x0d, 0x0e, 0x01, 0xf7,
};

// Plinky alternates its eight voices across the stereo image. Plank keeps that
// spread so one string sits slightly off-centre rather than dead middle.
constexpr float kVoicePan[kStringCount] = { -0.30f, 0.30f, -0.19f, 0.19f, -0.10f, 0.10f, -0.04f, 0.04f };

// Comb lengths as fractions of the maximum tail, mutually prime-ish so the tail
// does not obviously repeat.
constexpr float kReverbLineScale[kReverbLineCount] = { 0.0297f, 0.0371f, 0.0411f, 0.0437f };

constexpr float kTwoPi = 6.28318530717958647692f;

template <typename T>
[[nodiscard]] T clampValue(const T value, const T minimum, const T maximum) noexcept
{
    return std::max(minimum, std::min(value, maximum));
}

[[nodiscard]] int findIndex(const std::uint8_t value, const std::uint8_t* data, const std::size_t count) noexcept
{
    for (std::size_t i = 0; i < count; ++i)
    {
        if (data[i] == value)
            return static_cast<int>(i);
    }
    return -1;
}

// PolyBLEP correction for the step discontinuity of a naive ramp. `phase` is
// the normalised position in the cycle and `width` the normalised increment.
[[nodiscard]] float polyBlep(const float phase, const float width) noexcept
{
    if (width <= 0.0f)
        return 0.0f;
    if (phase < width)
    {
        const float t = phase / width;
        return t + t - t * t - 1.0f;
    }
    if (phase > 1.0f - width)
    {
        const float t = (phase - 1.0f) / width;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

[[nodiscard]] float wavetableSample(const std::size_t table, const float phase) noexcept
{
    const WavetableSet& set = wavetables();
    const float scaled = phase * static_cast<float>(kWavetableSize);
    const auto position = static_cast<std::size_t>(scaled);
    const std::size_t index = position & (kWavetableSize - 1u);
    const std::size_t next = (index + 1u) & (kWavetableSize - 1u);
    const float fraction = scaled - static_cast<float>(position);
    const float a = static_cast<float>(set.data[table][index]);
    const float b = static_cast<float>(set.data[table][next]);
    return (a + (b - a) * fraction) * (1.0f / 32768.0f);
}

}  // namespace

// ── Lifecycle ───────────────────────────────────────────────────────────────

void Processor::init(const double sampleRate)
{
    sampleRate_ = sampleRate > 1.0 ? sampleRate : 48000.0;
    activate();
}

void Processor::activate()
{
    resetToDefaults();
}

void Processor::resetToDefaults()
{
    for (std::size_t i = 0; i < kParameterCount; ++i)
        parameters_[i] = kParameterSpecs[i].defaultValue;

    for (Voice& voice : voices_)
        voice.reset();
    for (Lfo& lfo : lfos_)
        lfo.reset();

    resetEffects();

    ledRefreshSamples_ = 0;
    randomState_ = 0x1f123bb5u;
    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    ledInitialized_ = false;
    panicRequested_ = false;
    suppressLedFeedbackOnce_ = false;
    pendingMidiCount_ = 0;
    scratch_ = ProcessResult {};
    status_ = Status {};
}

// ── Parameters ──────────────────────────────────────────────────────────────

void Processor::setParameter(const std::uint32_t index, const float value)
{
    if (index >= kInputParameterCount)
        return;  // read-only status parameters

    if (index == static_cast<std::uint32_t>(ParamId::panic))
    {
        if (value > 0.5f)
            requestPanic();
        return;
    }

    // Grid cells: rising edge plucks the string, falling edge releases it.
    // This is the UI's and host automation's route into the strings, and it
    // takes the same path as Launchpad input.
    if (index >= kCellParameterStart && index < kCellParameterStart + kCellCount)
    {
        const std::size_t cell = index - kCellParameterStart;
        const std::size_t row = cell / kGridWidth;
        const std::size_t col = cell % kGridWidth;
        const bool on = value > 0.5f;
        const bool wasOn = parameters_[index] > 0.5f;

        if (on != wasOn)
        {
            if (on)
                startNote(voices_[col], col, row, scratch_, 0);
            else if (voices_[col].held && voices_[col].degree == row)
                stopNote(voices_[col], col, scratch_, 0, false);

            // Preserve any MIDI that the note produced, to be emitted next block.
            for (std::uint32_t i = 0; i < scratch_.eventCount; ++i)
                queueMidi(scratch_.events[i].data[0], scratch_.events[i].data[1],
                          scratch_.events[i].data[2]);
            scratch_ = ProcessResult {};
        }

        parameters_[index] = on ? 1.0f : 0.0f;
        return;
    }

    const auto& spec = kParameterSpecs[index];
    float clamped = clampValue(value, spec.minimum, spec.maximum);
    if (spec.integer)
        clamped = std::round(clamped);

    parameters_[index] = clamped;

    // Tuning changes re-derive the pitch of every sounding string so a held
    // note follows the new scale instead of sticking at the old pitch.
    switch (static_cast<ParamId>(index))
    {
    case ParamId::scale:
    case ParamId::root:
    case ParamId::octave:
    case ParamId::rotate:
    case ParamId::microtune:
    case ParamId::stride:
        for (Voice& voice : voices_)
        {
            if (voice.sounding)
                setDegree(voice, voice.degree);
        }
        break;
    default:
        break;
    }
}

float Processor::getParameter(const std::uint32_t index) const noexcept
{
    if (index >= kParameterCount)
        return 0.0f;

    if (index >= kStatusParameterStart)
    {
        switch (static_cast<ParamId>(index))
        {
        case ParamId::outActiveStrings:
            return status_.activeStrings;
        case ParamId::outPeak:
            return status_.peak;
        default:
            break;
        }
        const std::size_t offset = index - kStatusParameterStart;
        return offset < kStringCount ? status_.stringLevels[offset] : 0.0f;
    }

    return parameters_[index];
}

const Status& Processor::getStatus() const noexcept
{
    return status_;
}

float Processor::levelForString(const std::size_t index) const noexcept
{
    return index < kStringCount ? status_.stringLevels[index] : 0.0f;
}

std::uint8_t Processor::baseChannel() const noexcept
{
    const int channel = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::baseChannel)]));
    return static_cast<std::uint8_t>(clampValue(channel - 1, 0, 15));
}

// ── Grid mapping ────────────────────────────────────────────────────────────

std::uint8_t Processor::noteForCell(const std::size_t row, const std::size_t col) const noexcept
{
    if (row >= kGridHeight || col >= kGridWidth)
        return 0u;

    const int root = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::root)]));
    const int octave = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::octave)]));
    const int rotate = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::rotate)]));
    const int stride = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::stride)]));
    const int fine = static_cast<int>(
        std::lround(parameters_[static_cast<std::size_t>(ParamId::microtune)] * 100.0f));

    // Rotating the scale shifts which degree sits under each row, so the ladder
    // moves without the rows losing their pitch ordering.
    const int degree = (static_cast<int>(row) + rotate) % static_cast<int>(kGridHeight);
    const int interval = scaleInterval(static_cast<std::size_t>(degree));

    // Stride is a constant semitone push, in the manner of Plinky's P_STRIDE.
    // Column is part of the signature so callers can pass a grid coordinate
    // directly; pitch is per-string, so every column yields the same ladder.
    (void)col;

    const int note = root + octave * 12 + interval + stride + fine / 100;
    return static_cast<std::uint8_t>(clampValue(note, 0, 127));
}

int Processor::scaleInterval(const std::size_t degree) const noexcept
{
    const int scale = static_cast<int>(std::lround(parameters_[static_cast<std::size_t>(ParamId::scale)]));
    const auto index = static_cast<std::size_t>(clampValue(scale, 0, static_cast<int>(ScaleId::count) - 1));
    const std::size_t wrapped = degree < kGridHeight ? degree : degree % kGridHeight;
    return static_cast<int>(kScaleIntervals[index][wrapped]);
}

void Processor::setDegree(Voice& voice, const std::size_t row)
{
    voice.degree = static_cast<std::uint8_t>(row);
    voice.pitchSemitones = static_cast<float>(noteForCell(row, 0)) - 69.0f;  // A4 = MIDI 69
}

std::uint8_t Processor::midiNoteFor(const Voice& voice) const noexcept
{
    return static_cast<std::uint8_t>(clampValue(voice.pitchSemitones + 69.0f, 0.0f, 127.0f));
}

std::uint32_t Processor::oscillatorIncrement(const float semitones) const noexcept
{
    const float frequency = 440.0f * std::pow(2.0f, semitones * (1.0f / 12.0f));
    const double increment = static_cast<double>(frequency) / sampleRate_;
    if (increment <= 0.0)
        return 0u;
    return static_cast<std::uint32_t>(clampValue(increment * 4294967296.0, 1.0, 4294967295.0));
}

float Processor::envelopeStep(const float milliseconds) const noexcept
{
    const float seconds = std::max(0.0005f, milliseconds * 0.001f);
    return 1.0f / static_cast<float>(std::max(1.0, seconds * sampleRate_));
}

// ── String control ──────────────────────────────────────────────────────────

void Processor::startNote(Voice& voice,
                          const std::size_t col,
                          const std::size_t row,
                          ProcessResult& result,
                          const std::uint32_t frame)
{
    setDegree(voice, row);
    voice.held = true;
    voice.sounding = true;
    voice.amplitude.gateOn();
    voice.modulation.gateOn();

    // Keep the cell parameter in step so the host sees the same grid state
    // whether the string was played from the UI or from hardware.
    if (col < kStringCount)
        parameters_[kCellParameterStart + cellIndex(row, col)] = 1.0f;

    if (parameters_[static_cast<std::size_t>(ParamId::midiThru)] >= 0.5f)
    {
        const auto status = static_cast<std::uint8_t>(0x90u | baseChannel());
        appendMidi(result, frame, status, midiNoteFor(voice), 100);
    }
}

void Processor::stopNote(Voice& voice,
                         const std::size_t col,
                         ProcessResult& result,
                         const std::uint32_t frame,
                         const bool immediate)
{
    const std::size_t row = voice.degree;

    if (immediate)
        voice.amplitude.reset();
    else
        voice.amplitude.gateOff();
    voice.modulation.gateOff();
    voice.held = false;
    voice.sounding = false;

    if (col < kStringCount)
        parameters_[kCellParameterStart + cellIndex(row, col)] = 0.0f;

    if (parameters_[static_cast<std::size_t>(ParamId::midiThru)] >= 0.5f)
    {
        const auto status = static_cast<std::uint8_t>(0x80u | baseChannel());
        appendMidi(result, frame, status, midiNoteFor(voice), 0);
    }
}

void Processor::pressGrid(const std::size_t row, const std::size_t col, ProcessResult& result, const std::uint32_t frame)
{
    Voice& voice = voices_[col];
    const bool latched = parameters_[static_cast<std::size_t>(ParamId::latch)] >= 0.5f;

    // With latch engaged a string keeps ringing after the pad is released, so a
    // second tap on the same cell is how you silence it.
    if (voice.sounding && voice.degree == row)
    {
        if (latched)
            stopNote(voice, col, result, frame, true);
        return;
    }

    startNote(voice, col, row, result, frame);
}

void Processor::releaseGrid(const std::size_t row, const std::size_t col, ProcessResult& result, const std::uint32_t frame)
{
    Voice& voice = voices_[col];
    if (!voice.sounding || voice.degree != row)
        return;

    if (parameters_[static_cast<std::size_t>(ParamId::latch)] >= 0.5f)
        return;  // latched: keep ringing

    stopNote(voice, col, result, frame, false);
}

void Processor::releaseAllStrings(ProcessResult& result, const std::uint32_t frame)
{
    for (std::size_t col = 0; col < kStringCount; ++col)
    {
        if (voices_[col].sounding)
            stopNote(voices_[col], col, result, frame, false);
    }
}

// ── Launchpad and MIDI input ────────────────────────────────────────────────

bool Processor::handleMidi(const MidiMessage& event, ProcessResult& result)
{
    if (event.size < 3)
        return false;

    const auto status = static_cast<std::uint8_t>(event.data[0] & 0xf0u);
    const std::uint8_t data1 = event.data[1];
    const std::uint8_t data2 = event.data[2];

    std::size_t row = 0;
    std::size_t col = 0;
    if ((status == 0x90u || status == 0x80u) && noteToGrid(data1, row, col))
    {
        if (status == 0x90u && data2 > 0u)
            pressGrid(row, col, result, event.frame);
        else
            releaseGrid(row, col, result, event.frame);

        // Consumed, never echoed. A note-off echoed back to the Launchpad reads
        // as an LED-off command on the programmer-mode LED channels.
        return true;
    }

    const bool topButton = findIndex(data1, kTopButtonCCs.data(), kTopButtonCCs.size()) >= 0;
    const bool sideButton = findIndex(data1, kSideButtonCCs.data(), kSideButtonCCs.size()) >= 0;
    if (status == 0xb0u && (topButton || sideButton))
    {
        if (data2 > 0u)
            return handleTopButton(data1, result, event.frame) || handleSideButton(data1);
        return true;
    }

    if (parameters_[static_cast<std::size_t>(ParamId::passInput)] >= 0.5f && event.size == 3)
        appendMidi(result, event.frame, event.data[0], data1, data2);
    return false;
}

bool Processor::handleTopButton(const std::uint8_t cc, ProcessResult& result, const std::uint32_t frame)
{
    const int index = findIndex(cc, kTopButtonCCs.data(), kTopButtonCCs.size());
    if (index < 0)
        return false;

    const auto p = [](ParamId id) { return static_cast<std::uint32_t>(id); };

    switch (static_cast<TopButton>(index))
    {
    case TopButton::octaveDown:
        setParameter(p(ParamId::octave), parameters_[p(ParamId::octave)] - 1.0f);
        break;
    case TopButton::octaveUp:
        setParameter(p(ParamId::octave), parameters_[p(ParamId::octave)] + 1.0f);
        break;
    case TopButton::morphDown:
        setParameter(p(ParamId::morph), parameters_[p(ParamId::morph)] - 0.06f);
        break;
    case TopButton::morphUp:
        setParameter(p(ParamId::morph), parameters_[p(ParamId::morph)] + 0.06f);
        break;
    case TopButton::latch: {
        const bool next = parameters_[p(ParamId::latch)] < 0.5f;
        setParameter(p(ParamId::latch), next ? 1.0f : 0.0f);
        if (!next)
            releaseAllStrings(result, frame);
        break;
    }
    case TopButton::driveUp:
        setParameter(p(ParamId::drive), parameters_[p(ParamId::drive)] + 0.05f);
        break;
    case TopButton::resonanceUp:
        setParameter(p(ParamId::resonance), parameters_[p(ParamId::resonance)] + 0.05f);
        break;
    case TopButton::ledFeedback:
        setParameter(p(ParamId::ledFeedback), parameters_[p(ParamId::ledFeedback)] >= 0.5f ? 0.0f : 1.0f);
        break;
    case TopButton::panic:
        requestPanic();
        break;
    }

    return true;
}

bool Processor::handleSideButton(const std::uint8_t cc)
{
    const int index = findIndex(cc, kSideButtonCCs.data(), kSideButtonCCs.size());
    if (index < 0)
        return false;

    setParameter(static_cast<std::uint32_t>(ParamId::scale),
                 static_cast<float>(kSideButtonScales[static_cast<std::size_t>(index)]));
    return true;
}

// ── Modulation ──────────────────────────────────────────────────────────────

float Processor::lfoSample(Lfo& lfo, const std::size_t shape) noexcept
{
    const float phase = static_cast<float>(lfo.phase);
    float value = 0.0f;

    switch (static_cast<LfoShape>(shape))
    {
    case LfoShape::triangle:
        value = phase < 0.5f ? (phase * 4.0f - 1.0f) : (3.0f - phase * 4.0f);
        break;
    case LfoShape::sine:
        value = std::sin(phase * kTwoPi);
        break;
    case LfoShape::smoothNoise:
        if (phase >= lfo.held)
        {
            lfo.held = phase + 0.125f;
            value = static_cast<float>(static_cast<int>(lfo.random()) >> 9) * (1.0f / 8388607.0f) - 1.0f;
        }
        else
        {
            value = lfo.value;
        }
        break;
    case LfoShape::stepNoise:
        if (phase >= lfo.held)
        {
            lfo.held = phase + 0.0625f;
            value = static_cast<float>(static_cast<int>(lfo.random()) >> 9) * (1.0f / 8388607.0f) - 1.0f;
        }
        else
        {
            value = lfo.value;
        }
        break;
    case LfoShape::square:
        value = phase < 0.5f ? 1.0f : -1.0f;
        break;
    case LfoShape::biSquare:
        value = phase < 0.25f ? 1.0f : (phase < 0.5f ? -1.0f : (phase < 0.75f ? 1.0f : -1.0f));
        break;
    case LfoShape::saw:
        value = phase * 2.0f - 1.0f;
        break;
    case LfoShape::sandcastle: {
        // Plinky's "castle": a coarse staircase that alternates sign each cycle.
        const float stepped = std::floor(phase * 7.0f) / 7.0f;
        value = ((lfo.random() & 1u) != 0u ? stepped : -stepped) * 2.0f;
        break;
    }
    case LfoShape::trigs:
    case LfoShape::biTrigs:
        if (phase >= lfo.held)
        {
            lfo.held = phase + (static_cast<LfoShape>(shape) == LfoShape::trigs ? 0.25f : 0.125f);
            value = 1.0f;
        }
        else
        {
            value = lfo.value;
        }
        break;
    case LfoShape::envelope:
        // Slow asymmetric contour, in the manner of Plinky's LFO_ENV.
        lfo.value = lfo.value * 0.995f + (phase < 0.5f ? 0.005f : -0.005f);
        value = lfo.value;
        break;
    case LfoShape::count:
        value = 0.0f;
        break;
    }

    lfo.value = clampValue(value, -1.0f, 1.0f);
    return lfo.value;
}

void Processor::advanceLfos(const std::uint32_t frameCount)
{
    for (std::size_t i = 0; i < lfos_.size(); ++i)
    {
        const auto frequency = static_cast<std::uint32_t>(ParamId::lfoAFrequency) + static_cast<std::uint32_t>(i * 3u);
        const auto shape = static_cast<std::uint32_t>(ParamId::lfoAShape) + static_cast<std::uint32_t>(i * 3u);

        const double rate = parameters_[frequency] / sampleRate_;
        lfos_[i].phase += rate * static_cast<double>(frameCount);
        while (lfos_[i].phase >= 1.0)
            lfos_[i].phase -= 1.0;

        // The result is cached in lfo.value, which computeModulation() reads.
        static_cast<void>(lfoSample(lfos_[i], static_cast<std::size_t>(std::lround(parameters_[shape]))));
    }
}

Processor::Modulation Processor::computeModulation() const noexcept
{
    Modulation modulation;
    for (std::size_t i = 0; i < lfos_.size(); ++i)
    {
        const auto target = static_cast<std::uint32_t>(ParamId::lfoATarget) + static_cast<std::uint32_t>(i * 3u);
        const float amount = lfos_[i].value;

        switch (static_cast<ModTarget>(static_cast<int>(std::lround(parameters_[target]))))
        {
        case ModTarget::pitch:
            modulation.pitch += amount * 2.0f;
            break;
        case ModTarget::cutoff:
            modulation.cutoff += amount * 0.75f;
            break;
        case ModTarget::morph:
            modulation.morph += amount * 0.35f;
            break;
        case ModTarget::noise:
            modulation.noise += amount * 0.5f;
            break;
        case ModTarget::drive:
            modulation.drive += amount * 0.8f;
            break;
        case ModTarget::off:
        case ModTarget::count:
            break;
        }
    }

    modulation.cutoff = std::max(0.05f, modulation.cutoff);
    modulation.drive = std::max(0.05f, modulation.drive);
    return modulation;
}

// ── Voice rendering ─────────────────────────────────────────────────────────

void Processor::renderVoice(const std::size_t index,
                            float* left,
                            float* right,
                            const std::uint32_t frameCount,
                            const Modulation& modulation)
{
    if (index >= kStringCount)
        return;

    Voice& voice = voices_[index];
    const auto p = [](ParamId id) { return static_cast<std::size_t>(id); };

    const float envLevel = parameters_[p(ParamId::envLevel)];
    const float modLevel = parameters_[p(ParamId::modLevel)];
    const float attackStep = envelopeStep(parameters_[p(ParamId::envAttack)]);
    const float decayStep = envelopeStep(parameters_[p(ParamId::envDecay)]);
    const float releaseStep = envelopeStep(parameters_[p(ParamId::envRelease)]);
    const float modAttackStep = envelopeStep(parameters_[p(ParamId::modAttack)]);
    const float modDecayStep = envelopeStep(parameters_[p(ParamId::modDecay)]);
    const float modReleaseStep = envelopeStep(parameters_[p(ParamId::modRelease)]);
    const float sustain = parameters_[p(ParamId::envSustain)];
    const float modSustain = parameters_[p(ParamId::modSustain)];

    const float detuneSemitones = parameters_[p(ParamId::detune)] * (1.0f / 100.0f);
    const float interval = parameters_[p(ParamId::interval)];
    const float drive = clampValue(parameters_[p(ParamId::drive)] * modulation.drive, 0.0f, 1.0f);
    const float noiseTarget = clampValue(parameters_[p(ParamId::noise)] + modulation.noise, 0.0f, 1.0f);
    const float morph = clampValue(parameters_[p(ParamId::morph)] + modulation.morph, 0.0f, 1.0f);

    // Morph selects the wavetable pair and crossfades away from the polyBLEP
    // oscillators, so it acts as a single continuous timbre axis.
    const float tablePosition = morph * static_cast<float>(kWavetableCount - 1u);
    const auto tableA = static_cast<std::size_t>(std::floor(tablePosition));
    const auto tableB = std::min(tableA + 1u, kWavetableCount - 1u);
    const float tableBlend = tablePosition - static_cast<float>(tableA);

    const float cutoffBase = parameters_[p(ParamId::cutoff)];
    const float resonance = parameters_[p(ParamId::resonance)] * 2.0f;
    const float filterEnvAmount = parameters_[p(ParamId::filterEnv)];

    // Drive is compensated against resonance so more feedback does not simply
    // make the filter louder, following Plinky's `2 / (resonance + 2)` law.
    const float driveGain = (1.0f + drive * 7.0f) / (1.0f + resonance * 0.5f);

    const float glideMs = parameters_[p(ParamId::glide)];
    const float glide = glideMs > 0.5f
                            ? clampValue(120.0f / (glideMs * static_cast<float>(sampleRate_)), 0.0005f, 0.4f)
                            : 1.0f;

    const float pan = kVoicePan[index];
    const float panLeft = std::sqrt((1.0f - pan) * 0.5f) * 1.41421f;
    const float panRight = std::sqrt((1.0f + pan) * 0.5f) * 1.41421f;

    const float basePitch = voice.pitchSemitones + modulation.pitch;
    const std::uint32_t incrementA = oscillatorIncrement(basePitch);
    const std::uint32_t incrementB = oscillatorIncrement(basePitch + interval + detuneSemitones);

    // A quarter-cycle offset between the pair is what gives the wavetable path
    // its hollow, reed-like character.
    constexpr float kMorphPhaseOffset = 0.25f;
    constexpr float kPhaseScale = 1.0f / 4294967296.0f;

    float noise = voice.noise;
    const float noiseStep = (noiseTarget * noiseTarget - noise) / static_cast<float>(frameCount);

    float levelFollower = voice.level;

    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        voice.amplitude.advance(attackStep, decayStep, releaseStep, sustain);
        voice.modulation.advance(modAttackStep, modDecayStep, modReleaseStep, modSustain);

        const float amplitude = voice.amplitude.level() * envLevel;
        if (amplitude <= 0.0f && !voice.amplitude.active())
            voice.sounding = false;

        levelFollower += ((amplitude > 1.0f ? 1.0f : amplitude) - levelFollower) * 0.0015f;
        const float modulationEnv = voice.modulation.level() * modLevel;

        // ── Phase advance, once per sample, both oscillators ──
        float phases[2] {};
        float widths[2] {};
        for (int osc = 0; osc < 2; ++osc)
        {
            Oscillator& oscillator = voice.oscillator[osc];
            const std::uint32_t target = osc == 0 ? incrementA : incrementB;
            const auto delta = static_cast<float>(static_cast<std::int64_t>(target) -
                                                  static_cast<std::int64_t>(oscillator.increment));
            const float step = clampValue(delta * glide, -1.0e7f, 1.0e7f);
            oscillator.increment = static_cast<std::uint32_t>(
                static_cast<std::int64_t>(oscillator.increment) + static_cast<std::int64_t>(step));
            oscillator.phase += oscillator.increment;

            phases[osc] = static_cast<float>(oscillator.phase) * kPhaseScale;
            widths[osc] = static_cast<float>(oscillator.increment) * kPhaseScale;
        }

        // ── PolyBLEP saw pair ──
        // Plinky sums two saws with the second inverted, which hollows the tone
        // out as the interval shrinks. A little square is mixed back in so the
        // pair never falls silent when interval and detune are both zero.
        float raw = 0.0f;
        if (morph < 0.999f)
        {
            float sawA = phases[0] * 2.0f - 1.0f;
            sawA -= polyBlep(phases[0], widths[0]);

            float sawB = phases[1] * 2.0f - 1.0f;
            sawB -= polyBlep(phases[1], widths[1]);

            float square = phases[0] < 0.5f ? 1.0f : -1.0f;
            square += polyBlep(phases[0], widths[0]);
            square -= polyBlep(phases[0] < 0.5f ? phases[0] + 0.5f : phases[0] - 0.5f, widths[0]);

            raw = (sawA - sawB + 0.25f * square) * 0.4f * (1.0f - morph);
        }

        // ── Band-limited wavetable pair ──
        if (morph > 0.001f)
        {
            const float phaseB = phases[0] + kMorphPhaseOffset;
            const float wrappedB = phaseB >= 1.0f ? phaseB - 1.0f : phaseB;

            const float a = wavetableSample(tableA, phases[0]) + wavetableSample(tableA, wrappedB);
            const float b = wavetableSample(tableB, phases[0]) + wavetableSample(tableB, wrappedB);

            // 0.35 rather than 0.5: the summed pair peaks near 2.0 while the
            // polyBLEP path peaks near 1.8, and the two are crossfaded by the
            // same control, so this trim keeps the level even as morph sweeps.
            raw += (a + (b - a) * tableBlend) * 0.35f * morph;
        }

        // ── Noise and filter ──
        noise += noiseStep;
        const float noiseSample =
            (static_cast<float>(random() & 0xffffu) * (1.0f / 32768.0f) - 1.0f) * noise * 0.25f;

        // Cutoff is the filter's one-pole coefficient. Squaring the control
        // gives fine control at the bottom of the range where a linear sweep
        // would spend most of its travel above audibly bright.
        float cutoff = cutoffBase * cutoffBase;
        cutoff += modulationEnv * filterEnvAmount * 0.55f;
        cutoff *= modulation.cutoff;
        cutoff = clampValue(cutoff, 0.0008f, 0.98f);

        const float input = raw * driveGain + noiseSample;
        const float filtered = voice.filter.process(input, cutoff, resonance, amplitude);

        left[frame] += filtered * panLeft;
        right[frame] += filtered * panRight;
    }

    voice.noise = noiseTarget;
    voice.level = clampValue(levelFollower, 0.0f, 1.0f);
}

// ── Effects ─────────────────────────────────────────────────────────────────

void Processor::resetEffects()
{
    const auto delayFrames = static_cast<std::size_t>(static_cast<double>(kDelayMaxSeconds) * sampleRate_);
    delayLeft_.assign(delayFrames, 0.0f);
    delayRight_.assign(delayFrames, 0.0f);
    delayPosition_ = 0;
    delaySamples_ = static_cast<float>(delayFrames) * 0.25f;
    delayWobblePhase_ = 0.0f;

    const auto reverbFrames = static_cast<std::size_t>(static_cast<double>(kReverbMaxSeconds) * sampleRate_);
    for (std::size_t i = 0; i < kReverbLines; ++i)
    {
        const auto length = std::max<std::size_t>(
            64u, static_cast<std::size_t>(static_cast<float>(reverbFrames) * kReverbLineScale[i]));
        reverbLines_[i].assign(length, 0.0f);
        reverbPositions_[i] = 0;
    }
    reverbDamp_ = 0.0f;
    reverbWobblePhase_ = 0.0f;
}

void Processor::renderDelay(float* left, float* right, const std::uint32_t frameCount, const TransportSnapshot& transport)
{
    const auto p = [](ParamId id) { return static_cast<std::size_t>(id); };

    const float send = parameters_[p(ParamId::delaySend)];
    if (send <= 0.0f || delayLeft_.size() < 4u)
        return;

    // Time is tempo-linked whenever the host supplies a tempo, as on Plinky.
    float seconds = 0.05f + parameters_[p(ParamId::delayTime)] * 1.95f;
    if (transport.valid && transport.bpm > 1.0)
    {
        const float beat = 60.0f / static_cast<float>(transport.bpm);
        const float beats = std::max(1.0f, std::round(seconds / beat));
        seconds = beat * std::min(beats, 4.0f);
    }

    const float wobble = parameters_[p(ParamId::delayWobble)];
    delayWobblePhase_ += 0.11f * static_cast<float>(frameCount) / static_cast<float>(sampleRate_);
    if (delayWobblePhase_ >= 1.0f)
        delayWobblePhase_ -= 1.0f;
    const float wobbleOffset = std::sin(delayWobblePhase_ * kTwoPi) * wobble * 0.008f *
                               static_cast<float>(sampleRate_);

    const float maxSamples = static_cast<float>(delayLeft_.size()) - 2.0f;
    const float target = clampValue(seconds * static_cast<float>(sampleRate_) + wobbleOffset, 16.0f, maxSamples);
    delaySamples_ += (target - delaySamples_) * 0.002f;

    const float ratio = clampValue(parameters_[p(ParamId::delayRatio)], 0.05f, 4.0f);
    const float feedback = parameters_[p(ParamId::delayFeedback)];
    const float sendGain = send * 0.6f;

    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        const float leftPosition = delaySamples_;
        const float rightPosition = delaySamples_ * ratio;

        const auto readInterpolated = [this](const std::vector<float>& line, const float position) {
            const auto size = static_cast<float>(line.size());
            auto wrapped = std::fmod(position, size);
            if (wrapped < 0.0f)
                wrapped += size;
            const auto whole = static_cast<std::size_t>(wrapped);
            const auto next = (whole + 1u) % line.size();
            const float fraction = wrapped - static_cast<float>(whole);
            return line[whole] + (line[next] - line[whole]) * fraction;
        };

        const float readLeft = readInterpolated(delayLeft_, leftPosition);
        const float readRight = readInterpolated(delayRight_, rightPosition);

        delayLeft_[delayPosition_] = left[frame] * sendGain + readRight * feedback;
        delayRight_[delayPosition_] = right[frame] * sendGain + readLeft * feedback;

        left[frame] += readLeft;
        right[frame] += readRight;

        delayPosition_ = (delayPosition_ + 1u) % delayLeft_.size();
    }
}

void Processor::renderReverb(float* left, float* right, const std::uint32_t frameCount)
{
    const auto p = [](ParamId id) { return static_cast<std::size_t>(id); };

    const float send = parameters_[p(ParamId::reverbSend)];
    if (send <= 0.0f)
        return;

    const float time = parameters_[p(ParamId::reverbTime)];
    const float shimmer = parameters_[p(ParamId::reverbShimmer)];

    // Feedback sets the tail; time sets how far down the comb lines the tail is
    // allowed to run before it is absorbed.
    const float feedback = clampValue(0.72f + time * 0.27f, 0.0f, 0.985f);
    const float damping = clampValue(0.25f + time * 0.55f, 0.0f, 0.95f);
    const float sendGain = send * 0.35f;

    reverbDamp_ += (damping - reverbDamp_) * 0.0005f;
    reverbWobblePhase_ += 0.07f * static_cast<float>(frameCount) / static_cast<float>(sampleRate_);
    if (reverbWobblePhase_ >= 1.0f)
        reverbWobblePhase_ -= 1.0f;
    const float shimmerPhase = std::sin(reverbWobblePhase_ * kTwoPi) * shimmer;

    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        const float inputLeft = left[frame] * sendGain;
        const float inputRight = right[frame] * sendGain;

        float wetLeft = 0.0f;
        float wetRight = 0.0f;

        for (std::size_t i = 0; i < kReverbLines; ++i)
        {
            std::vector<float>& line = reverbLines_[i];
            const std::size_t read = reverbPositions_[i];
            const float stored = line[read];
            const float input = (i & 1u) == 0u ? inputLeft : inputRight;

            // Damped feedback plus a slow shimmer that lengthens the tail.
            const float fed = stored * (1.0f - reverbDamp_) + stored * reverbDamp_ * shimmerPhase;
            line[read] = input + fed * feedback;
            reverbPositions_[i] = (read + 1u) % line.size();

            if ((i & 1u) == 0u)
                wetLeft += stored;
            else
                wetRight += stored;
        }

        const float scale = 0.25f;
        left[frame] += wetLeft * scale;
        right[frame] += wetRight * scale;
    }
}

void Processor::applyOutputStage(float* left, float* right, const std::uint32_t frameCount)
{
    const float width = parameters_[static_cast<std::size_t>(ParamId::width)];
    const float level = parameters_[static_cast<std::size_t>(ParamId::level)];

    // Mid/side width, as Plinky does on its synth bus.
    const float midScale = 1.0f - width * 0.5f;
    const float sideScale = width;

    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
    {
        const float mid = (left[frame] + right[frame]) * 0.5f;
        const float side = (left[frame] - right[frame]) * 0.5f;
        const float outLeft = mid * midScale + side * sideScale;
        const float outRight = mid * midScale - side * sideScale;

        left[frame] = std::tanh(outLeft * level * 1.2f) * 0.85f;
        right[frame] = std::tanh(outRight * level * 1.2f) * 0.85f;
    }
}

// ── Launchpad LED feedback ──────────────────────────────────────────────────

std::uint8_t Processor::ledColorForCell(const std::size_t row, const std::size_t col) const noexcept
{
    if (col >= kStringCount)
        return kLedOff;

    const float level = voices_[col].level;
    if (level <= 0.001f)
        return kLedOff;
    if (voices_[col].sounding && voices_[col].degree == row)
        return kLedWhite;  // the struck cell, so the eye finds the pitch at once
    if (level > 0.85f)
        return kLedRed;
    if (level > 0.55f)
        return kLedOrange;
    if (level > 0.25f)
        return kLedYellow;
    return kLedGreen;
}

void Processor::emitLedFeedback(ProcessResult& result, const bool force)
{
    const auto p = [](ParamId id) { return static_cast<std::size_t>(id); };

    if (parameters_[p(ParamId::ledFeedback)] < 0.5f)
        return;

    if (!ledInitialized_)
        appendSysex(result, kProgrammerModeSysex, static_cast<std::uint16_t>(sizeof(kProgrammerModeSysex)));

    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
        {
            const std::size_t index = cellIndex(row, col);
            const std::uint8_t color = ledColorForCell(row, col);
            if (force || color != lastCellLeds_[index])
            {
                appendMidi(result, 0, 0x90u, gridToNote(row, col), color);
                lastCellLeds_[index] = color;
            }
        }
    }

    static constexpr std::array<std::uint8_t, kTopButtonCCs.size()> kTopColors = {{
        kLedBlue, kLedBlue, kLedPurple, kLedPurple, kLedDim,
        kLedOrange, kLedCyan, kLedDim, kLedRed,
    }};

    const bool latched = parameters_[p(ParamId::latch)] >= 0.5f;
    for (std::size_t i = 0; i < kTopColors.size(); ++i)
    {
        std::uint8_t color = kTopColors[i];
        if (static_cast<TopButton>(i) == TopButton::latch)
            color = latched ? kLedYellow : kLedDim;

        if (force || color != lastTopLeds_[i])
        {
            appendMidi(result, 0, 0xb0u, kTopButtonCCs[i], color);
            lastTopLeds_[i] = color;
        }
    }

    const auto scale = static_cast<std::size_t>(std::lround(parameters_[p(ParamId::scale)]));
    for (std::size_t i = 0; i < kSideButtonCCs.size(); ++i)
    {
        const std::size_t sideScale = static_cast<std::size_t>(kSideButtonScales[i]);
        const std::uint8_t color = sideScale == scale ? kLedPink : kLedDim;
        if (force || color != lastSideLeds_[i])
        {
            appendMidi(result, 0, 0xb0u, kSideButtonCCs[i], color);
            lastSideLeds_[i] = color;
        }
    }

    ledInitialized_ = true;
}

void Processor::queueMidi(const std::uint8_t status, const std::uint8_t data1, const std::uint8_t data2)
{
    if (pendingMidiCount_ >= pendingMidi_.size())
        return;

    MidiMessage& message = pendingMidi_[pendingMidiCount_++];
    message.frame = 0;
    message.size = 3;
    message.data[0] = status;
    message.data[1] = data1;
    message.data[2] = data2;
}

void Processor::flushPendingMidi(ProcessResult& result)
{
    for (std::uint32_t i = 0; i < pendingMidiCount_; ++i)
        appendMidi(result, pendingMidi_[i].frame, pendingMidi_[i].data[0], pendingMidi_[i].data[1],
                   pendingMidi_[i].data[2]);
    pendingMidiCount_ = 0;
}

void Processor::requestPanic()
{
    panicRequested_ = true;
}

void Processor::runPanic(ProcessResult& result)
{
    // Silence anything already sounding on the MIDI output before clearing.
    if (parameters_[static_cast<std::size_t>(ParamId::midiThru)] >= 0.5f)
    {
        for (const Voice& voice : voices_)
        {
            if (!voice.sounding)
                continue;
            const auto status = static_cast<std::uint8_t>(0x80u | baseChannel());
            appendMidi(result, 0, status, midiNoteFor(voice), 0);
        }
    }

    appendSysex(result, kProgrammerModeSysex, static_cast<std::uint16_t>(sizeof(kProgrammerModeSysex)));
    appendClearAllSysex(result);
    appendClearAllMidi(result);

    for (Voice& voice : voices_)
        voice.reset();
    for (Lfo& lfo : lfos_)
        lfo.reset();

    resetEffects();

    lastCellLeds_.fill(255u);
    lastTopLeds_.fill(255u);
    lastSideLeds_.fill(255u);
    ledInitialized_ = true;
    suppressLedFeedbackOnce_ = true;
    panicRequested_ = false;
    updateStatus(0.0f);
}

// ── MIDI output plumbing ────────────────────────────────────────────────────

void Processor::appendMidi(ProcessResult& result,
                           const std::uint32_t frame,
                           const std::uint8_t status,
                           const std::uint8_t data1,
                           const std::uint8_t data2)
{
    if (result.eventCount >= result.events.size())
        return;

    MidiMessage& event = result.events[result.eventCount++];
    event.frame = frame;
    event.size = 3;
    event.data[0] = status;
    event.data[1] = data1;
    event.data[2] = data2;
}

void Processor::appendSysex(ProcessResult& result, const std::uint8_t* data, const std::uint16_t size)
{
    if (result.eventCount >= result.events.size() || data == nullptr || size > result.events[0].data.size())
        return;

    MidiMessage& event = result.events[result.eventCount++];
    event.frame = 0;
    event.size = size;
    for (std::uint16_t i = 0; i < size; ++i)
        event.data[i] = data[i];
}

void Processor::appendClearAllSysex(ProcessResult& result)
{
    std::array<std::uint8_t, 256> data {};
    std::uint16_t size = 0;
    const auto append = [&data, &size](const std::uint8_t byte) {
        if (size < data.size())
            data[size++] = byte;
    };
    const auto appendLight = [&append](const std::uint8_t id) {
        append(0x00u);
        append(id);
        append(kLedOff);
    };

    append(0xf0u);
    append(0x00u);
    append(0x20u);
    append(0x29u);
    append(0x02u);
    append(0x0du);
    append(0x03u);

    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
            appendLight(gridToNote(row, col));
    }
    for (const std::uint8_t cc : kSideButtonCCs)
        appendLight(cc);
    for (const std::uint8_t cc : kTopButtonCCs)
        appendLight(cc);

    append(0xf7u);
    appendSysex(result, data.data(), size);
}

void Processor::appendClearAllMidi(ProcessResult& result)
{
    for (std::size_t row = 0; row < kGridHeight; ++row)
    {
        for (std::size_t col = 0; col < kGridWidth; ++col)
            appendMidi(result, 0, 0x90u, gridToNote(row, col), kLedOff);
    }
    for (const std::uint8_t cc : kSideButtonCCs)
        appendMidi(result, 0, 0xb0u, cc, kLedOff);
    for (const std::uint8_t cc : kTopButtonCCs)
        appendMidi(result, 0, 0xb0u, cc, kLedOff);
}

// ── Status ──────────────────────────────────────────────────────────────────

std::uint32_t Processor::random()
{
    randomState_ ^= randomState_ << 13u;
    randomState_ ^= randomState_ >> 17u;
    randomState_ ^= randomState_ << 5u;
    return randomState_;
}

void Processor::updateStatus(const float peak)
{
    int active = 0;
    for (std::size_t i = 0; i < kStringCount; ++i)
    {
        status_.stringLevels[i] = clampValue(voices_[i].level, 0.0f, 1.0f);
        if (voices_[i].sounding || voices_[i].amplitude.active())
            ++active;
    }
    status_.activeStrings = static_cast<float>(active);
    status_.peak = clampValue(peak, 0.0f, 1.0f);
}

// ── Process ─────────────────────────────────────────────────────────────────

void Processor::process(float* left,
                        float* right,
                        const std::uint32_t frameCount,
                        const TransportSnapshot& transport,
                        const MidiMessage* inputEvents,
                        const std::uint32_t inputEventCount,
                        ProcessResult& result)
{
    if (left == nullptr || right == nullptr || frameCount == 0u)
    {
        result.status = status_;
        return;
    }

    std::fill_n(left, frameCount, 0.0f);
    std::fill_n(right, frameCount, 0.0f);

    if (panicRequested_)
        runPanic(result);

    // MIDI generated by parameter changes since the last block goes out first,
    // ahead of anything this block generates.
    flushPendingMidi(result);

    bool forceLeds = false;
    for (std::uint32_t i = 0; i < inputEventCount; ++i)
    {
        if (inputEvents == nullptr)
            break;
        if (handleMidi(inputEvents[i], result))
            forceLeds = true;
    }

    advanceLfos(frameCount);
    const Modulation modulation = computeModulation();

    for (std::size_t i = 0; i < kStringCount; ++i)
        renderVoice(i, left, right, frameCount, modulation);

    renderDelay(left, right, frameCount, transport);
    renderReverb(left, right, frameCount);
    applyOutputStage(left, right, frameCount);

    float peak = 0.0f;
    for (std::uint32_t frame = 0; frame < frameCount; ++frame)
        peak = std::max(peak, std::max(std::fabs(left[frame]), std::fabs(right[frame])));
    updateStatus(peak);

    // The same 0.75 second refresh cadence lifeform uses, so a Launchpad that
    // missed an update recovers without the plugin flooding the port.
    const bool periodicRefresh = ledRefreshSamples_ >= static_cast<std::uint32_t>(sampleRate_ * 0.75);
    if (suppressLedFeedbackOnce_)
        suppressLedFeedbackOnce_ = false;
    else
        emitLedFeedback(result, forceLeds || periodicRefresh || !ledInitialized_);
    ledRefreshSamples_ = periodicRefresh ? 0u : ledRefreshSamples_ + frameCount;

    result.status = status_;
}

}  // namespace downspout::plank
