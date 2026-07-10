#include "arpgen_core.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace downspout::arpgen {
namespace {

constexpr double kEpsilon = 1e-7;

struct ScaleDefinition {
    std::array<int, 12> semitones {};
    int count = 0;
};

constexpr std::array<ScaleDefinition, SCALE_COUNT> kScales {{
    {{{0, 2, 4, 5, 7, 9, 11}}, 7},
    {{{0, 2, 3, 5, 7, 8, 10}}, 7},
    {{{0, 2, 3, 5, 7, 8, 11}}, 7},
    {{{0, 2, 3, 5, 7, 9, 10}}, 7},
    {{{0, 2, 4, 5, 7, 9, 10}}, 7},
    {{{0, 2, 4, 7, 9}}, 5},
    {{{0, 3, 5, 7, 10}}, 5},
    {{{0, 3, 5, 6, 7, 10}}, 6},
    {{{0, 2, 4, 6, 7, 9, 11}}, 7},
    {{{0, 1, 4, 5, 7, 8, 10}}, 7},
}};

template <typename T>
constexpr T clampValue(const T value, const T low, const T high) noexcept
{
    return value < low ? low : (value > high ? high : value);
}

void append(BlockResult& result,
            const std::uint32_t frame,
            const std::uint8_t status,
            const std::uint8_t data1,
            const std::uint8_t data2) noexcept
{
    if (result.eventCount >= kMaxOutputEvents)
        return;
    auto& event = result.events[static_cast<std::size_t>(result.eventCount++)];
    event.frame = frame;
    event.size = 3;
    event.data = {status, data1, data2, 0};
}

void appendInput(BlockResult& result, const InputMidiEvent& source) noexcept
{
    if (result.eventCount < kMaxOutputEvents)
        result.events[static_cast<std::size_t>(result.eventCount++)] = source;
}

std::uint32_t frameAt(const double quarter,
                      const double blockStart,
                      const double quartersPerFrame,
                      const std::uint32_t nframes) noexcept
{
    if (quarter <= blockStart || quartersPerFrame <= 0.0)
        return 0;
    const double raw = (quarter - blockStart) / quartersPerFrame;
    return static_cast<std::uint32_t>(clampValue(std::llround(raw), 0LL,
                                                 static_cast<long long>(nframes > 0 ? nframes - 1 : 0)));
}

int outputChannel(const EngineState& state) noexcept
{
    return state.controls.outputChannel == 0 ? state.lastInputChannel : state.controls.outputChannel;
}

void noteOff(EngineState& state, BlockResult& result, const std::uint32_t frame) noexcept
{
    if (state.activeNote >= 0) {
        append(result, frame,
               static_cast<std::uint8_t>(0x80 | (clampValue(state.activeChannel, 1, 16) - 1)),
               static_cast<std::uint8_t>(state.activeNote), 0);
    }
    state.activeNote = -1;
    state.noteOffPending = false;
}

double barLengthInQuarters(const TransportSnapshot& transport) noexcept
{
    if (transport.beatType <= 0.0)
        return 4.0;
    return std::max(0.25, transport.beatsPerBar * 4.0 / transport.beatType);
}

double absoluteQuarter(const TransportSnapshot& transport) noexcept
{
    const double beatUnit = transport.beatType > 0.0 ? 4.0 / transport.beatType : 1.0;
    return transport.bar * barLengthInQuarters(transport) + transport.barBeat * beatUnit;
}

int captureDivisions(const int slice) noexcept
{
    switch (slice) {
    case CAPTURE_QUARTER_BAR: return 4;
    case CAPTURE_HALF_BAR: return 2;
    default: return 1;
    }
}

void latchCaptured(EngineState& state) noexcept
{
    const bool haveCapture = std::any_of(state.captured.begin(), state.captured.end(), [](const bool value) {
        return value;
    });
    if (haveCapture) {
        state.latched = state.captured;
        state.latchedVelocity = state.capturedVelocity;
        state.capturePriming = false;
    }
    state.captured.fill(false);
    state.capturedVelocity.fill(0);
}

void updateCaptureSegment(EngineState& state,
                          const TransportSnapshot& transport,
                          const double quarter) noexcept
{
    const double barLength = barLengthInQuarters(transport);
    const int divisions = captureDivisions(state.controls.captureSlice);
    const double segmentLength = barLength / static_cast<double>(divisions);
    const auto segment = static_cast<std::int64_t>(std::floor((quarter + kEpsilon) / segmentLength));
    if (state.captureSegment < 0) {
        state.captureSegment = segment;
    } else if (segment != state.captureSegment) {
        latchCaptured(state);
        state.captureSegment = segment;
    }
}

int nearestScaleDegree(const int note, const Controls& controls) noexcept
{
    const auto& scale = kScales[static_cast<std::size_t>(controls.scale)];
    int bestDegree = 0;
    int bestDistance = 128;
    for (int octave = -2; octave <= 11; ++octave) {
        for (int degree = 0; degree < scale.count; ++degree) {
            const int candidate = controls.key + octave * 12 + scale.semitones[static_cast<std::size_t>(degree)];
            const int distance = std::abs(candidate - note);
            if (distance < bestDistance || (distance == bestDistance && candidate <
                                            controls.key + (bestDegree / scale.count) * 12 +
                                                scale.semitones[static_cast<std::size_t>((bestDegree % scale.count + scale.count) % scale.count)])) {
                bestDistance = distance;
                bestDegree = octave * scale.count + degree;
            }
        }
    }
    return bestDegree;
}

int noteForDegree(const int degree, const Controls& controls) noexcept
{
    const auto& scale = kScales[static_cast<std::size_t>(controls.scale)];
    int octave = degree / scale.count;
    int index = degree % scale.count;
    if (index < 0) {
        index += scale.count;
        --octave;
    }
    return controls.key + octave * 12 + scale.semitones[static_cast<std::size_t>(index)];
}

void addMaterialNote(std::array<int, kMaxMaterialNotes>& notes, int& count, const int note) noexcept
{
    if (note < 0 || note > 127 || count >= kMaxMaterialNotes)
        return;
    for (int i = 0; i < count; ++i) {
        if (notes[static_cast<std::size_t>(i)] == note)
            return;
    }
    notes[static_cast<std::size_t>(count++)] = note;
}

int buildMaterial(const EngineState& state,
                  std::array<int, kMaxMaterialNotes>& notes,
                  int& velocity) noexcept
{
    int count = 0;
    int velocitySum = 0;
    int velocityCount = 0;
    if (state.controls.mode == MODE_CHORD) {
        for (int note = 0; note < 128; ++note) {
            if (!state.latched[static_cast<std::size_t>(note)])
                continue;
            velocitySum += state.latchedVelocity[static_cast<std::size_t>(note)];
            ++velocityCount;
            for (int octave = 0; octave < state.controls.octaves; ++octave)
                addMaterialNote(notes, count, note + octave * 12);
        }
    } else {
        const auto& scale = kScales[static_cast<std::size_t>(state.controls.scale)];
        const int shapeCount = state.controls.scaleShape == SHAPE_RUN
            ? scale.count
            : (state.controls.scaleShape == SHAPE_TRIAD ? 3 : 4);
        constexpr std::array<int, 4> stackDegrees {0, 2, 4, 6};
        for (int note = 0; note < 128; ++note) {
            if (!state.held[static_cast<std::size_t>(note)])
                continue;
            velocitySum += state.heldVelocity[static_cast<std::size_t>(note)];
            ++velocityCount;
            const int anchorDegree = nearestScaleDegree(note, state.controls);
            for (int octave = 0; octave < state.controls.octaves; ++octave) {
                for (int i = 0; i < shapeCount; ++i) {
                    const int offset = state.controls.scaleShape == SHAPE_RUN ? i : stackDegrees[static_cast<std::size_t>(i)];
                    addMaterialNote(notes, count,
                                    noteForDegree(anchorDegree + octave * scale.count + offset, state.controls));
                }
            }
        }
    }
    std::sort(notes.begin(), notes.begin() + count);
    const int followed = velocityCount > 0 ? velocitySum / velocityCount : 96;
    velocity = clampValue(static_cast<int>(std::lround(96.0 + state.controls.velocityFollow * (followed - 96.0))), 1, 127);
    return count;
}

std::uint64_t fingerprint(const std::array<int, kMaxMaterialNotes>& notes, const int count) noexcept
{
    std::uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < count; ++i) {
        hash ^= static_cast<std::uint64_t>(notes[static_cast<std::size_t>(i)] + 1);
        hash *= 1099511628211ULL;
    }
    hash ^= static_cast<std::uint64_t>(count);
    return hash;
}

int traversalIndex(const int count, const Order order, const std::uint64_t step) noexcept
{
    if (count <= 1)
        return 0;
    if (order == ORDER_UP)
        return static_cast<int>(step % static_cast<std::uint64_t>(count));
    if (order == ORDER_DOWN)
        return count - 1 - static_cast<int>(step % static_cast<std::uint64_t>(count));
    const int period = count * 2 - 2;
    const int phase = static_cast<int>(step % static_cast<std::uint64_t>(period));
    if (order == ORDER_UP_DOWN)
        return phase < count ? phase : period - phase;
    return phase < count ? count - 1 - phase : phase - count + 1;
}

void handleInput(EngineState& state,
                 BlockResult& result,
                 const InputMidiEvent& event) noexcept
{
    if (event.size < 1)
        return;
    const int kind = event.data[0] & 0xf0;
    const int channel = (event.data[0] & 0x0f) + 1;
    const bool noteMessage = (kind == 0x80 || kind == 0x90) && event.size >= 3;
    if (noteMessage) {
        const int note = event.data[1] & 0x7f;
        const int eventVelocity = event.data[2] & 0x7f;
        const bool isOn = kind == 0x90 && eventVelocity > 0;
        state.lastInputChannel = channel;
        state.held[static_cast<std::size_t>(note)] = isOn;
        state.heldVelocity[static_cast<std::size_t>(note)] = isOn ? static_cast<std::uint8_t>(eventVelocity) : 0;
        if (isOn && state.controls.mode == MODE_CHORD) {
            state.captured[static_cast<std::size_t>(note)] = true;
            state.capturedVelocity[static_cast<std::size_t>(note)] = static_cast<std::uint8_t>(eventVelocity);
            if (state.capturePriming) {
                state.latched[static_cast<std::size_t>(note)] = true;
                state.latchedVelocity[static_cast<std::size_t>(note)] = static_cast<std::uint8_t>(eventVelocity);
            }
        }
    }
    if (state.controls.passInput)
        appendInput(result, event);
}

void resetTraversal(EngineState& state) noexcept
{
    state.materialFingerprint = 0;
    state.traversalStep = 0;
}

}  // namespace

Controls clampControls(const Controls& raw) noexcept
{
    Controls controls = raw;
    controls.mode = clampValue(controls.mode, 0, MODE_COUNT - 1);
    controls.order = clampValue(controls.order, 0, ORDER_COUNT - 1);
    controls.rate = clampValue(controls.rate, 0, RATE_COUNT - 1);
    controls.captureSlice = clampValue(controls.captureSlice, 0, CAPTURE_COUNT - 1);
    controls.key = clampValue(controls.key, 0, 11);
    controls.scale = clampValue(controls.scale, 0, SCALE_COUNT - 1);
    controls.scaleShape = clampValue(controls.scaleShape, 0, SHAPE_COUNT - 1);
    controls.octaves = clampValue(controls.octaves, 1, 4);
    controls.gate = clampValue(controls.gate, 0.05f, 1.0f);
    controls.velocityFollow = clampValue(controls.velocityFollow, 0.0f, 1.0f);
    controls.outputChannel = clampValue(controls.outputChannel, 0, 16);
    return controls;
}

double rateInQuarterNotes(const int rate) noexcept
{
    constexpr std::array<double, RATE_COUNT> rates {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
    return rates[static_cast<std::size_t>(clampValue(rate, 0, RATE_COUNT - 1))];
}

void activate(EngineState& state) noexcept
{
    state = EngineState {};
}

void deactivate(EngineState& state) noexcept
{
    state = EngineState {};
}

BlockResult processBlock(EngineState& state,
                         const Controls& rawControls,
                         const TransportSnapshot& transport,
                         const std::uint32_t nframes,
                         const double sampleRate,
                         const InputMidiEvent* midiEvents,
                         const std::uint32_t midiEventCount) noexcept
{
    BlockResult result;
    state.controls = clampControls(rawControls);
    const double bpm = clampValue(transport.bpm, 1.0, 999.0);
    const double safeRate = std::max(1.0, sampleRate);
    const double quartersPerFrame = bpm / (60.0 * safeRate);
    const double blockStart = absoluteQuarter(transport);
    const double blockEnd = blockStart + static_cast<double>(nframes) * quartersPerFrame;

    const bool discontinuity = state.havePosition &&
        (blockStart + 0.001 < state.previousBlockEndQuarter ||
         std::abs(blockStart - state.previousBlockEndQuarter) > std::max(0.25, 8.0 * rateInQuarterNotes(state.controls.rate)));

    if (!transport.valid || !transport.playing || nframes == 0) {
        noteOff(state, result, 0);
        state.held.fill(false);
        state.heldVelocity.fill(0);
        state.captured.fill(false);
        state.capturedVelocity.fill(0);
        state.capturePriming = !std::any_of(state.latched.begin(), state.latched.end(), [](const bool value) {
            return value;
        });
        state.wasPlaying = false;
        state.havePosition = false;
        state.captureSegment = -1;
        resetTraversal(state);
        result.activeNote = state.activeNote;
        return result;
    }

    if (!state.wasPlaying || discontinuity) {
        noteOff(state, result, 0);
        state.captureSegment = -1;
        state.captured.fill(false);
        state.capturedVelocity.fill(0);
        resetTraversal(state);
    }
    state.wasPlaying = true;
    state.havePosition = true;

    updateCaptureSegment(state, transport, blockStart);
    const double stepSize = rateInQuarterNotes(state.controls.rate);
    double nextStep = std::ceil((blockStart - kEpsilon) / stepSize) * stepSize;
    if (nextStep < blockStart - kEpsilon)
        nextStep += stepSize;
    std::uint32_t inputIndex = 0;
    const std::uint32_t safeInputCount = std::min<std::uint32_t>(midiEventCount, kMaxInputEvents);

    while (true) {
        const double inputQuarter = inputIndex < safeInputCount
            ? blockStart + static_cast<double>(std::min(midiEvents[inputIndex].frame, nframes - 1)) * quartersPerFrame
            : std::numeric_limits<double>::infinity();
        const double offQuarter = state.noteOffPending ? state.noteOffQuarter : std::numeric_limits<double>::infinity();
        const double stepQuarter = nextStep < blockEnd - kEpsilon ? nextStep : std::numeric_limits<double>::infinity();
        const double actionQuarter = std::min({inputQuarter, offQuarter, stepQuarter});
        if (!std::isfinite(actionQuarter) || actionQuarter >= blockEnd - kEpsilon)
            break;

        updateCaptureSegment(state, transport, actionQuarter);
        const std::uint32_t frame = frameAt(actionQuarter, blockStart, quartersPerFrame, nframes);

        if (offQuarter <= actionQuarter + kEpsilon)
            noteOff(state, result, frame);

        while (inputIndex < safeInputCount) {
            const double eventQuarter = blockStart +
                static_cast<double>(std::min(midiEvents[inputIndex].frame, nframes - 1)) * quartersPerFrame;
            if (eventQuarter > actionQuarter + kEpsilon)
                break;
            handleInput(state, result, midiEvents[inputIndex]);
            ++inputIndex;
        }

        if (stepQuarter <= actionQuarter + kEpsilon) {
            std::array<int, kMaxMaterialNotes> material {};
            int velocity = 96;
            const int count = buildMaterial(state, material, velocity);
            const std::uint64_t currentFingerprint = fingerprint(material, count);
            if (currentFingerprint != state.materialFingerprint) {
                state.materialFingerprint = currentFingerprint;
                state.traversalStep = 0;
            }
            if (count > 0) {
                noteOff(state, result, frame);
                const int index = traversalIndex(count, static_cast<Order>(state.controls.order), state.traversalStep++);
                const int note = material[static_cast<std::size_t>(index)];
                const int channel = outputChannel(state);
                append(result, frame, static_cast<std::uint8_t>(0x90 | (channel - 1)),
                       static_cast<std::uint8_t>(note), static_cast<std::uint8_t>(velocity));
                state.activeNote = note;
                state.activeChannel = channel;
                state.noteOffPending = true;
                state.noteOffQuarter = actionQuarter + stepSize * state.controls.gate;
            } else {
                noteOff(state, result, frame);
            }
            result.materialCount = count;
            nextStep += stepSize;
        }
    }

    if (state.controls.mode == MODE_SCALE) {
        std::array<int, kMaxMaterialNotes> material {};
        int velocity = 96;
        result.materialCount = buildMaterial(state, material, velocity);
    } else {
        result.materialCount = static_cast<int>(std::count(state.latched.begin(), state.latched.end(), true));
    }
    state.previousBlockEndQuarter = blockEnd;
    result.activeNote = state.activeNote;
    return result;
}

}  // namespace downspout::arpgen
