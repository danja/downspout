#include "counterpointer_engine.hpp"

#include "counterpointer_transport.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace downspout::counterpointer {
namespace {

struct Rng {
    std::uint32_t state = 0x12345678u;

    void seed(const std::uint32_t value)
    {
        state = value == 0 ? 0x12345678u : value;
    }

    std::uint32_t next()
    {
        std::uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }

    float nextFloat()
    {
        return static_cast<float>(next() & 0x00ffffffu) / static_cast<float>(0x01000000u);
    }
};

struct ScaleDef {
    const int* intervals = nullptr;
    int count = 0;
};

static const int kScaleChromatic[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const int kScaleMajor[] = {0, 2, 4, 5, 7, 9, 11};
static const int kScaleNatMinor[] = {0, 2, 3, 5, 7, 8, 10};
static const int kScaleHarmMinor[] = {0, 2, 3, 5, 7, 8, 11};
static const int kScalePentMajor[] = {0, 2, 4, 7, 9};
static const int kScalePentMinor[] = {0, 3, 5, 7, 10};
static const int kScaleBlues[] = {0, 3, 5, 6, 7, 10};
static const int kScaleDorian[] = {0, 2, 3, 5, 7, 9, 10};
static const int kScaleMixolydian[] = {0, 2, 4, 5, 7, 9, 10};
static const int kScaleLydian[] = {0, 2, 4, 6, 7, 9, 11};
static const int kScaleMelodicMinor[] = {0, 2, 3, 5, 7, 9, 11};
static const int kScaleWholeTone[] = {0, 2, 4, 6, 8, 10};
static const int kScaleAltered[] = {0, 1, 3, 4, 6, 8, 10};
static const int kScaleHalfWholeDiminished[] = {0, 1, 3, 4, 6, 7, 9, 10};
static const int kScaleWholeHalfDiminished[] = {0, 2, 3, 5, 6, 8, 9, 11};
static const int kScaleBebopDominant[] = {0, 2, 4, 5, 7, 9, 10, 11};
static const int kScaleBebopMajor[] = {0, 2, 4, 5, 7, 8, 9, 11};
static const int kScaleBebopMinor[] = {0, 2, 3, 4, 5, 7, 9, 10};
static const int kScalePhrygian[] = {0, 1, 3, 5, 7, 8, 10};
static const int kScaleLocrian[] = {0, 1, 3, 5, 6, 8, 10};
static const int kScalePhrygianDominant[] = {0, 1, 4, 5, 7, 8, 10};

static const ScaleDef kScales[SCALE_COUNT] = {
    {kScaleChromatic, 12},
    {kScaleMajor, 7},
    {kScaleNatMinor, 7},
    {kScaleHarmMinor, 7},
    {kScalePentMajor, 5},
    {kScalePentMinor, 5},
    {kScaleBlues, 6},
    {kScaleDorian, 7},
    {kScaleMixolydian, 7},
    {kScaleLydian, 7},
    {kScaleMelodicMinor, 7},
    {kScaleWholeTone, 6},
    {kScaleAltered, 7},
    {kScaleHalfWholeDiminished, 8},
    {kScaleWholeHalfDiminished, 8},
    {kScaleBebopDominant, 8},
    {kScaleBebopMajor, 8},
    {kScaleBebopMinor, 8},
    {kScalePhrygian, 7},
    {kScaleLocrian, 7},
    {kScalePhrygianDominant, 7}
};

int wrap12(const int value)
{
    int out = value % 12;
    if (out < 0)
        out += 12;
    return out;
}

std::uint32_t mix_u32(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

std::uint32_t controls_seed(const Controls& controls)
{
    std::uint32_t seed = 0xC0217A3Du;
    seed ^= static_cast<std::uint32_t>(controls.key & 0xff);
    seed ^= static_cast<std::uint32_t>(controls.scale & 0xff) << 8;
    seed ^= static_cast<std::uint32_t>(controls.cycle_bars & 0xff) << 16;
    seed ^= static_cast<std::uint32_t>(controls.granularity & 0xff) << 24;
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.follow * 1000.0f)));
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.counter * 1000.0f)) << 1);
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.short_random * 1000.0f)) << 2);
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.long_random * 1000.0f)) << 3);
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.density * 1000.0f)) << 4);
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.embellish * 1000.0f)) << 5);
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.regularity * 1000.0f)) << 6);
    seed ^= mix_u32(static_cast<std::uint32_t>(std::lround(controls.color * 1000.0f)) << 7);
    return seed;
}

bool is_jazz_scale(const int scale)
{
    switch (scale)
    {
    case SCALE_DORIAN:
    case SCALE_MIXOLYDIAN:
    case SCALE_LYDIAN:
    case SCALE_MELODIC_MINOR:
    case SCALE_PHRYGIAN:
    case SCALE_LOCRIAN:
    case SCALE_PHRYGIAN_DOMINANT:
    case SCALE_WHOLE_TONE:
    case SCALE_ALTERED:
    case SCALE_HALF_WHOLE_DIMINISHED:
    case SCALE_WHOLE_HALF_DIMINISHED:
    case SCALE_BEBOP_DOMINANT:
    case SCALE_BEBOP_MAJOR:
    case SCALE_BEBOP_MINOR:
        return true;
    default:
        return false;
    }
}

float color_amount(const Controls& controls)
{
    const float color = clampf(controls.color, 0.0f, 1.0f);
    return is_jazz_scale(controls.scale) ? color : color * 0.45f;
}

Controls effective_controls_for_generation(const Controls& controls)
{
    Controls out = controls;
    const float irregularity = 1.0f - clampf(controls.regularity, 0.0f, 1.0f);
    const float color = color_amount(controls);

    out.short_random = clampf(controls.short_random * (0.18f + irregularity * 1.25f) + irregularity * 0.28f + color * 0.18f, 0.0f, 1.0f);
    out.long_random = clampf(controls.long_random * (0.10f + irregularity * 1.15f) + color * 0.08f, 0.0f, 1.0f);
    out.syncopation = clampf(controls.syncopation * (0.35f + irregularity * 0.95f) + irregularity * 0.18f + color * 0.12f, 0.0f, 1.0f);
    out.rhythm_follow = clampf(controls.rhythm_follow * (0.55f + controls.regularity * 0.45f), 0.0f, 1.0f);
    out.consonance = clampf(controls.consonance * (0.65f + controls.regularity * 0.35f) * (1.0f - color * 0.32f), 0.0f, 1.0f);
    out.embellish = clampf(controls.embellish + color * 0.24f, 0.0f, 1.0f);
    out.span = clampf(controls.span + color * 0.18f, 0.0f, 1.0f);
    out.regularity = clampf(controls.regularity - color * 0.14f, 0.0f, 1.0f);
    return out;
}

bool scale_contains_pc(const int scaleIndex, const int key, const int notePc)
{
    const ScaleDef& scale = kScales[clampi(scaleIndex, 0, SCALE_COUNT - 1)];
    const int rel = wrap12(notePc - key);
    for (int i = 0; i < scale.count; ++i)
    {
        if (scale.intervals[i] == rel)
            return true;
    }
    return false;
}

int nearest_scale_note(const Controls& controls, const int target, const int minNote, const int maxNote)
{
    int best = clampi(target, minNote, maxNote);
    int bestDistance = 1024;
    for (int note = minNote; note <= maxNote; ++note)
    {
        if (!scale_contains_pc(controls.scale, controls.key, note))
            continue;
        const int distance = std::abs(note - target);
        if (distance < bestDistance)
        {
            best = note;
            bestDistance = distance;
        }
    }
    return best;
}

bool chromatic_approach_allowed(const Controls& controls, const int note, const int source)
{
    const float color = color_amount(controls);
    if (color <= 0.0001f) {
        return false;
    }
    if (scale_contains_pc(controls.scale, controls.key, note - 1) ||
        scale_contains_pc(controls.scale, controls.key, note + 1)) {
        int distance = std::abs(wrap12(note - source));
        if (distance > 6) {
            distance = 12 - distance;
        }
        return distance <= 2 || distance == 5 || distance == 6;
    }
    return false;
}

int register_center(const int reg)
{
    switch (reg)
    {
    case REGISTER_LOW:
        return 52;
    case REGISTER_HIGH:
        return 76;
    case REGISTER_MID:
    default:
        return 64;
    }
}

int bass_register_center(const Controls& controls)
{
    return clampi(register_center(controls.reg) - 12, 36, 64);
}

double timing_center(const SegmentCapture& capture)
{
    double best = -1.0;
    int bestIndex = 0;
    for (int i = 0; i < kTimingBins; ++i)
    {
        if (capture.timing_bins[static_cast<std::size_t>(i)] > best)
        {
            best = capture.timing_bins[static_cast<std::size_t>(i)];
            bestIndex = i;
        }
    }

    if (best <= 0.0001)
        return 0.0;
    return (static_cast<double>(bestIndex) + 0.5) / static_cast<double>(kTimingBins);
}

double answer_position(const double inputOnset, const Controls& controls)
{
    const double shifted = inputOnset + 0.5 + static_cast<double>(controls.syncopation) * 0.18;
    return clampd(shifted - std::floor(shifted), 0.0, 0.94);
}

double embellish_position(const double inputOnset, const int hitIndex, const Controls& controls)
{
    const double base = hitIndex == 1 ? 0.25 : 0.75;
    const double pull = static_cast<double>(controls.regularity) * 0.22;
    const double answer = answer_position(inputOnset, controls);
    const double pos = base * (0.65 + pull) + answer * (0.35 - pull);
    return clampd(pos, 0.04, 0.92);
}

int interval_class(const int a, const int b)
{
    int distance = std::abs(wrap12(a - b));
    if (distance > 6)
        distance = 12 - distance;
    return distance;
}

double consonance_score(const int candidate, const int source, const float consonance)
{
    const int interval = interval_class(candidate, source);
    double score = 0.0;
    if (interval == 0)
        score = -0.4;
    else if (interval == 3 || interval == 4 || interval == 5 || interval == 7 || interval == 8 || interval == 9)
        score = 1.0;
    else if (interval == 2)
        score = 0.15;
    else
        score = -0.65;
    return score * static_cast<double>(consonance);
}

bool strict_fugue_mode(const Controls& controls)
{
    return controls.regularity >= 0.88f &&
           controls.counter >= 0.72f &&
           controls.short_random <= 0.18f &&
           controls.long_random <= 0.12f;
}

void appendMidi(BlockResult& result,
                const std::uint32_t frame,
                const std::uint8_t size,
                const std::uint8_t b0,
                const std::uint8_t b1 = 0,
                const std::uint8_t b2 = 0,
                const std::uint8_t b3 = 0)
{
    if (result.eventCount >= kMaxScheduledMidiEvents)
        return;

    ScheduledMidiEvent& event = result.events[static_cast<std::size_t>(result.eventCount++)];
    event.frame = frame;
    event.size = size;
    event.data[0] = b0;
    event.data[1] = b1;
    event.data[2] = b2;
    event.data[3] = b3;
}

void appendMidi(BlockResult& result, const InputMidiEvent& event)
{
    if (result.eventCount >= kMaxScheduledMidiEvents)
        return;
    result.events[static_cast<std::size_t>(result.eventCount++)] = event;
}

int resolve_output_channel(const EngineState& state, const int configuredChannel)
{
    if (configuredChannel == 0)
        return clampi(state.lastInputChannel, 1, 16);
    return clampi(configuredChannel, 1, 16);
}

void emit_note_on(BlockResult& result, const std::uint32_t frame, const int note, const int velocity, const int outputChannel)
{
    const std::uint8_t channel = static_cast<std::uint8_t>(clampi(outputChannel, 1, 16) - 1);
    appendMidi(result,
               frame,
               3,
               static_cast<std::uint8_t>(0x90 | channel),
               static_cast<std::uint8_t>(clampi(note, 0, 127)),
               static_cast<std::uint8_t>(clampi(velocity, 1, 127)));
}

void emit_note_off(BlockResult& result, const std::uint32_t frame, const int note, const int outputChannel)
{
    const std::uint8_t channel = static_cast<std::uint8_t>(clampi(outputChannel, 1, 16) - 1);
    appendMidi(result,
               frame,
               3,
               static_cast<std::uint8_t>(0x80 | channel),
               static_cast<std::uint8_t>(clampi(note, 0, 127)),
               0);
}

void silence_output(EngineState& state, BlockResult& result, const std::uint32_t frame)
{
    if (state.activeOutput)
        emit_note_off(result, frame, state.activeOutputNote, state.activeOutputChannel);
    state.activeOutput = false;
    state.noteOffPending = false;
    state.noteOnPending = false;
    state.pendingHitActive.fill(false);
}

void clear_capture(std::array<SegmentCapture, kMaxSegments>& capture)
{
    for (SegmentCapture& segment : capture)
        segment = SegmentCapture {};
}

void clear_phrase(PhraseState& phrase)
{
    phrase = PhraseState {};
}

void clear_held_notes(EngineState& state)
{
    state.heldNotes.fill(false);
    state.heldVelocity.fill(0);
}

bool capture_has_material(const std::array<SegmentCapture, kMaxSegments>& capture, const int segmentCount)
{
    for (int i = 0; i < segmentCount; ++i)
    {
        const SegmentCapture& segment = capture[static_cast<std::size_t>(i)];
        if (segment.onset_weight > 0.0001)
            return true;
        for (double value : segment.duration)
        {
            if (value > 0.0001)
                return true;
        }
    }
    return false;
}

void capture_interval(EngineState& state,
                      const double beatsPerBar,
                      const int segmentCount,
                      const double absStart,
                      const double absEnd)
{
    if (absEnd <= absStart + 1e-12 || segmentCount <= 0)
        return;

    const int segment = counterpointer_segment_index_for_time(state.controls,
                                                              beatsPerBar,
                                                              segmentCount,
                                                              absStart + kBeatEpsilon);
    for (int note = 0; note < 128; ++note)
    {
        if (!state.heldNotes[static_cast<std::size_t>(note)])
            continue;
        state.capture[static_cast<std::size_t>(segment)].duration[static_cast<std::size_t>(note % 12)] += absEnd - absStart;
    }
}

void capture_onset(EngineState& state,
                   const double beatsPerBar,
                   const int segmentCount,
                   const double absBeats,
                   const std::uint8_t note,
                   const std::uint8_t velocity)
{
    if (segmentCount <= 0)
        return;

    const int segment = counterpointer_segment_index_for_time(state.controls,
                                                              beatsPerBar,
                                                              segmentCount,
                                                              absBeats + kBeatEpsilon);
    const double segmentBeats = counterpointer_segment_beats_for_controls(state.controls, beatsPerBar);
    const double cyclePos = counterpointer_wrapped_cycle_position(absBeats, state.controls, beatsPerBar);
    const double segmentPos = std::fmod(cyclePos, segmentBeats);
    const double rel = clampd(segmentPos / segmentBeats, 0.0, 0.999999);
    int bin = static_cast<int>(std::floor(rel * static_cast<double>(kTimingBins)));
    bin = clampi(bin, 0, kTimingBins - 1);

    const double weight = 0.35 + (static_cast<double>(velocity) / 127.0) * 0.65;
    SegmentCapture& capture = state.capture[static_cast<std::size_t>(segment)];
    capture.onset_weight += weight;
    capture.note_sum += static_cast<double>(note) * weight;
    capture.velocity_sum += static_cast<double>(velocity) * weight;
    capture.timing_bins[static_cast<std::size_t>(bin)] += weight;
}

int source_note_for_segment(const std::array<SegmentCapture, kMaxSegments>& capture,
                            const int segment,
                            const int fallback)
{
    const SegmentCapture& direct = capture[static_cast<std::size_t>(segment)];
    if (direct.onset_weight > 0.0001)
        return clampi(static_cast<int>(std::lround(direct.note_sum / direct.onset_weight)), 0, 127);

    double best = 0.0;
    int bestPc = -1;
    for (int pc = 0; pc < 12; ++pc)
    {
        if (direct.duration[static_cast<std::size_t>(pc)] > best)
        {
            best = direct.duration[static_cast<std::size_t>(pc)];
            bestPc = pc;
        }
    }
    if (bestPc >= 0)
        return fallback + wrap12(bestPc - fallback);

    return fallback;
}

int source_velocity_for_segment(const SegmentCapture& capture)
{
    if (capture.onset_weight > 0.0001)
        return clampi(static_cast<int>(std::lround(capture.velocity_sum / capture.onset_weight)), 1, 127);
    return 92;
}

int choose_output_note(const Controls& controls,
                       const int source,
                       const int previousSource,
                       const int previousOutput,
                       const int segmentIndex,
                       Rng& rng)
{
    const int center = register_center(controls.reg);
    const int minNote = clampi(center - 18, 0, 127);
    const int maxNote = clampi(center + 18, 0, 127);
    const int sourceDelta = clampi(source - previousSource, -12, 12);
    const int maxLeap = 2 + static_cast<int>(std::lround(10.0f * controls.span));
    double bestScore = -std::numeric_limits<double>::infinity();
    int bestNote = nearest_scale_note(controls, center, minNote, maxNote);

    for (int note = minNote; note <= maxNote; ++note)
    {
        const bool inScale = scale_contains_pc(controls.scale, controls.key, note);
        const bool chromaticApproach = !inScale && chromatic_approach_allowed(controls, note, source);
        if (!inScale && !chromaticApproach)
            continue;

        const int outputDelta = clampi(note - previousOutput, -12, 12);
        double score = 0.0;
        score -= std::abs(note - center) * 0.035;
        score -= std::max(0, std::abs(note - previousOutput) - maxLeap) * 0.35;
        score += consonance_score(note, source, controls.consonance);
        if (chromaticApproach) {
            score += static_cast<double>(color_amount(controls)) * 0.32;
            score -= 0.26;
        }

        if (sourceDelta != 0)
        {
            if ((outputDelta > 0 && sourceDelta > 0) || (outputDelta < 0 && sourceDelta < 0))
                score += static_cast<double>(controls.follow) * 0.9;
            if ((outputDelta > 0 && sourceDelta < 0) || (outputDelta < 0 && sourceDelta > 0))
                score += static_cast<double>(controls.counter) * 1.25;
            score -= std::abs(std::abs(outputDelta) - std::abs(sourceDelta)) * static_cast<double>(controls.follow) * 0.08;
        }
        else
        {
            score += (outputDelta == 0 ? 0.25 : 0.0) * static_cast<double>(controls.follow);
            score += (std::abs(outputDelta) <= 2 ? 0.15 : 0.0) * static_cast<double>(controls.counter);
        }

        const double randomJitter = (static_cast<double>(rng.nextFloat()) - 0.5) *
                                    static_cast<double>(controls.short_random) * 1.8;
        score += randomJitter;
        score += static_cast<double>(color_amount(controls)) * static_cast<double>(std::abs(outputDelta) > 2 ? 0.10 : -0.02);
        score += static_cast<double>((segmentIndex % 2) == 0 ? 1 : -1) * static_cast<double>(controls.syncopation) * 0.04;

        if (score > bestScore)
        {
            bestScore = score;
            bestNote = note;
        }
    }

    return bestNote;
}

int choose_bass_descend_note(const Controls& controls,
                             const int source,
                             const int previousSource,
                             const int previousOutput,
                             const int segmentIndex,
                             const int segmentCount,
                             Rng& rng)
{
    const int center = bass_register_center(controls);
    const int minNote = clampi(center - 16, 24, 127);
    const int maxNote = clampi(center + 14, 0, 127);
    const float progress = segmentCount > 1
        ? static_cast<float>(segmentIndex) / static_cast<float>(segmentCount - 1)
        : 0.0f;
    const int descent = static_cast<int>(std::lround(progress * (7.0f + controls.span * 8.0f)));
    const int target = nearest_scale_note(controls, center + 7 - descent, minNote, maxNote);
    const int sourceDelta = clampi(source - previousSource, -12, 12);
    const int maxLeap = 3 + static_cast<int>(std::lround(7.0f * controls.span));

    double bestScore = -std::numeric_limits<double>::infinity();
    int bestNote = target;
    for (int note = minNote; note <= maxNote; ++note)
    {
        const bool inScale = scale_contains_pc(controls.scale, controls.key, note);
        const bool chromaticApproach = !inScale && chromatic_approach_allowed(controls, note, source);
        if (!inScale && !chromaticApproach)
            continue;

        const int outputDelta = clampi(note - previousOutput, -12, 12);
        double score = 0.0;
        score -= std::abs(note - target) * 0.18;
        score -= std::abs(note - center) * 0.018;
        score -= std::max(0, std::abs(note - previousOutput) - maxLeap) * 0.42;
        score += consonance_score(note, source, controls.consonance) * 1.10;

        if (sourceDelta > 0) {
            score += outputDelta < 0 ? 1.10 + static_cast<double>(controls.counter) * 1.20 : -0.55;
        } else if (sourceDelta < 0) {
            score += outputDelta <= 0 ? 0.42 : -0.16;
        } else {
            score += outputDelta <= 0 ? 0.32 : -0.20;
        }

        if (outputDelta == 0 && segmentIndex > 0)
            score -= 0.12;
        if (chromaticApproach)
            score += static_cast<double>(color_amount(controls)) * 0.24 - 0.22;

        score += (static_cast<double>(rng.nextFloat()) - 0.5) * static_cast<double>(controls.short_random) * 0.45;

        if (score > bestScore)
        {
            bestScore = score;
            bestNote = note;
        }
    }

    return bestNote;
}

void sync_primary_from_hits(PhraseStep& step)
{
    step.active = false;
    step.hitCount = clampi(step.hitCount, 0, kMaxHitsPerSegment);

    for (int i = 0; i < step.hitCount; ++i)
    {
        const PhraseHit& hit = step.hits[static_cast<std::size_t>(i)];
        if (!hit.active)
            continue;

        step.active = true;
        step.note = hit.note;
        step.velocity = hit.velocity;
        step.onset = hit.onset;
        step.gate = hit.gate;
        return;
    }
}

PhraseHit make_hit(const Controls& controls,
                   const int source,
                   const int previousSource,
                   const int previousOutput,
                   const int segmentCount,
                   const int segmentIndex,
                   const int hitIndex,
                   const int velocity,
                   const double onset,
                   Rng& rng)
{
    PhraseHit hit {};
    hit.active = true;
    const int bias = hitIndex == 0 ? 0 : (hitIndex == 1 ? 2 : -2);
    const int note = controls.response_mode == RESPONSE_BASS_DESCEND
        ? choose_bass_descend_note(controls, source + bias, previousSource, previousOutput, segmentIndex, segmentCount, rng)
        : choose_output_note(controls, source + bias, previousSource, previousOutput, segmentIndex + hitIndex, rng);
    hit.note = static_cast<std::uint8_t>(note);
    hit.velocity = static_cast<std::uint8_t>(clampi(static_cast<int>(std::lround(84.0 +
                                                                                  static_cast<double>(velocity - 84) *
                                                                                      static_cast<double>(controls.velocity_follow))) -
                                                        hitIndex * 8,
                                                    1,
                                                    127));
    hit.onset = onset;
    hit.gate = clampd(static_cast<double>(controls.gate) *
                          (hitIndex == 0 ? 1.0 : 0.58) *
                          (0.92 - static_cast<double>(controls.syncopation) * 0.20),
                      0.08,
                      1.0);
    return hit;
}

PhraseHit make_fugue_hit(const Controls& controls,
                         const int source,
                         const int subjectRoot,
                         const int segmentIndex,
                         const int hitIndex,
                         const int velocity,
                         const double inputOnset)
{
    PhraseHit hit {};
    hit.active = true;

    const int dominantRoot = subjectRoot + 7;
    const int subjectInterval = source - subjectRoot;
    const bool invert = color_amount(controls) >= 0.55f;
    const int target = dominantRoot + (invert ? -subjectInterval : subjectInterval);
    const int center = register_center(controls.reg);
    const int minNote = clampi(center - 20, 0, 127);
    const int maxNote = clampi(center + 20, 0, 127);
    const int ornament = hitIndex == 0 ? 0 : (hitIndex == 1 ? -1 : 1);

    hit.note = static_cast<std::uint8_t>(nearest_scale_note(controls, target + ornament, minNote, maxNote));
    hit.velocity = static_cast<std::uint8_t>(clampi(static_cast<int>(std::lround(78.0 +
                                                                                  static_cast<double>(velocity - 78) *
                                                                                      static_cast<double>(controls.velocity_follow))) -
                                                        hitIndex * 9,
                                                    1,
                                                    127));
    hit.onset = hitIndex == 0
        ? clampd(inputOnset * 0.72 + 0.08, 0.0, 0.62)
        : clampd((hitIndex == 1 ? 0.48 : 0.74) + static_cast<double>(segmentIndex % 2) * 0.04, 0.04, 0.92);
    hit.gate = clampd(static_cast<double>(controls.gate) * (hitIndex == 0 ? 0.82 : 0.42), 0.10, 0.92);
    return hit;
}

bool build_phrase_from_capture(const std::array<SegmentCapture, kMaxSegments>& capture,
                               const int segmentCount,
                               const Controls& rawControls,
                               VariationState& variation,
                               PhraseState& out)
{
    if (segmentCount <= 0 || !capture_has_material(capture, segmentCount))
        return false;

    const Controls controls = effective_controls_for_generation(rawControls);
    const bool fugueMode = rawControls.response_mode == RESPONSE_COUNTERPOINT && strict_fugue_mode(rawControls);
    clear_phrase(out);
    out.version = kPhraseStateVersion;
    out.segmentCount = segmentCount;
    out.ready = true;

    Rng rng;
    rng.seed(controls_seed(controls) ^
             mix_u32(static_cast<std::uint32_t>(variation.completed_cycles)) ^
             mix_u32(static_cast<std::uint32_t>(variation.mutation_serial) * 2246822519u));

    int previousSource = 60 + controls.key;
    const int subjectRoot = source_note_for_segment(capture, 0, previousSource);
    int previousOutput = nearest_scale_note(controls, register_center(controls.reg), 0, 127);
    for (int i = 0; i < segmentCount; ++i)
    {
        const SegmentCapture& segment = capture[static_cast<std::size_t>(i)];
        const int source = source_note_for_segment(capture, i, previousSource);
        const int velocity = source_velocity_for_segment(segment);
        const double inputOnset = timing_center(segment);
        const double complementaryOnset = answer_position(inputOnset, controls);
        const double onset = clampd(inputOnset * static_cast<double>(controls.rhythm_follow) +
                                        complementaryOnset * (1.0 - static_cast<double>(controls.rhythm_follow)),
                                    0.0,
                                    0.94);

        const double activity = segment.onset_weight > 0.0001 ? 0.18 : 0.0;
        const double noteChance = clampd(static_cast<double>(controls.density) + activity -
                                             static_cast<double>(controls.counter) * 0.08,
                                         0.0,
                                         1.0);

        PhraseStep step {};
        step.hitCount = 0;
        if (fugueMode)
        {
            step.hits[static_cast<std::size_t>(step.hitCount++)] =
                make_fugue_hit(controls, source, subjectRoot, i, 0, velocity, inputOnset);
        }
        else if (rng.nextFloat() <= noteChance) {
            step.hits[static_cast<std::size_t>(step.hitCount++)] =
                make_hit(controls, source, previousSource, previousOutput, segmentCount, i, 0, velocity, onset, rng);
        }

        const double firstExtraChance =
            fugueMode
                ? clampd(static_cast<double>(controls.embellish) * 0.38, 0.0, 0.44)
                : (controls.embellish >= 0.999f
                ? 1.0
                : clampd(static_cast<double>(controls.embellish) *
                             (0.45 + static_cast<double>(controls.density) * 0.50),
                         0.0,
                         1.0));
        const double secondExtraChance = fugueMode
            ? 0.0
            : clampd(static_cast<double>(controls.embellish) *
                         static_cast<double>(controls.embellish) *
                         (0.18 + (1.0 - static_cast<double>(controls.regularity)) * 0.24),
                     0.0,
                     1.0);
        if (rng.nextFloat() <= firstExtraChance)
        {
            step.hits[static_cast<std::size_t>(step.hitCount++)] =
                fugueMode
                    ? make_fugue_hit(controls, source, subjectRoot, i, 1, velocity, inputOnset)
                    : make_hit(controls,
                               source,
                               previousSource,
                               previousOutput,
                               segmentCount,
                               i,
                               1,
                               velocity,
                               embellish_position(inputOnset, 1, controls),
                               rng);
        }
        if (step.hitCount < kMaxHitsPerSegment && rng.nextFloat() <= secondExtraChance)
        {
            step.hits[static_cast<std::size_t>(step.hitCount++)] =
                make_hit(controls,
                         source,
                         previousSource,
                         previousOutput,
                         segmentCount,
                         i,
                         2,
                         velocity,
                         embellish_position(inputOnset, 2, controls),
                         rng);
        }

        if (controls.long_random > 0.0001f && rng.nextFloat() < controls.long_random * 0.18f)
        {
            if (step.hitCount <= 0)
            {
                step.hits[0] = make_hit(controls, source, previousSource, previousOutput, segmentCount, i, 0, velocity, onset, rng);
                step.hitCount = 1;
            }
            else
            {
                step.hits[0].active = !step.hits[0].active;
            }
        }

        std::sort(step.hits.begin(), step.hits.begin() + step.hitCount, [](const PhraseHit& a, const PhraseHit& b) {
            return a.onset < b.onset;
        });
        sync_primary_from_hits(step);

        out.steps[static_cast<std::size_t>(i)] = step;
        previousSource = source;
        previousOutput = step.note;
    }

    return true;
}

int cycles_between_mutations(const float longRandom)
{
    const float targetCycles = 1.0f + 7.0f * std::pow(1.0f - longRandom, 2.5f);
    return clampi(static_cast<int>(std::ceil(targetCycles - 1e-6f)), 1, 64);
}

void maybe_vary_phrase(EngineState& state)
{
    const Controls controls = effective_controls_for_generation(state.controls);
    const float amount = clampf(controls.long_random, 0.0f, 1.0f);
    if (amount <= 0.0001f || !state.playbackPhrase.ready)
        return;

    const int interval = cycles_between_mutations(amount);
    if ((state.variation.completed_cycles - state.variation.last_mutation_cycle) < interval)
        return;

    state.variation.last_mutation_cycle = state.variation.completed_cycles;
    if (state.variation.mutation_serial < std::numeric_limits<std::int32_t>::max())
        state.variation.mutation_serial += 1;

    Rng rng;
    rng.seed(controls_seed(state.controls) ^
             mix_u32(static_cast<std::uint32_t>(state.variation.completed_cycles)) ^
             mix_u32(static_cast<std::uint32_t>(state.variation.mutation_serial) * 3266489917u));

    for (int i = 0; i < state.playbackPhrase.segmentCount; ++i)
    {
        PhraseStep& step = state.playbackPhrase.steps[static_cast<std::size_t>(i)];
        if (step.hitCount <= 0)
            continue;

        if (rng.nextFloat() < amount * 0.25f)
            step.hits[0].active = !step.hits[0].active;
        for (int hitIndex = 0; hitIndex < step.hitCount; ++hitIndex)
        {
            PhraseHit& hit = step.hits[static_cast<std::size_t>(hitIndex)];
            if (hit.active && rng.nextFloat() < amount * 0.40f)
            {
                const int direction = rng.nextFloat() < 0.5f ? -1 : 1;
                hit.note = static_cast<std::uint8_t>(
                    nearest_scale_note(controls, static_cast<int>(hit.note) + direction * 2, 0, 127));
            }
            if (hit.active && rng.nextFloat() < amount * 0.25f)
                hit.onset = clampd(hit.onset + (static_cast<double>(rng.nextFloat()) - 0.5) * 0.18, 0.0, 0.94);
        }
        std::sort(step.hits.begin(), step.hits.begin() + step.hitCount, [](const PhraseHit& a, const PhraseHit& b) {
            return a.onset < b.onset;
        });
        sync_primary_from_hits(step);
    }
}

void schedule_segment(EngineState& state, const double segmentStartBeat, const double segmentBeats, const int segmentIndex)
{
    state.noteOnPending = false;
    if (!state.playbackPhrase.ready ||
        segmentIndex < 0 ||
        segmentIndex >= state.playbackPhrase.segmentCount)
    {
        return;
    }

    const PhraseStep& step = state.playbackPhrase.steps[static_cast<std::size_t>(segmentIndex)];
    if (!step.active)
        return;

    state.pendingStep = step;
    state.pendingHitActive.fill(false);
    state.noteOnPending = false;
    for (int i = 0; i < step.hitCount && i < kMaxHitsPerSegment; ++i)
    {
        const PhraseHit& hit = step.hits[static_cast<std::size_t>(i)];
        if (!hit.active)
            continue;

        state.pendingHits[static_cast<std::size_t>(i)] = hit;
        state.pendingHitBeat[static_cast<std::size_t>(i)] = segmentStartBeat + hit.onset * segmentBeats;
        state.pendingHitActive[static_cast<std::size_t>(i)] = true;
        state.pendingNoteOnBeat = state.pendingHitBeat[static_cast<std::size_t>(i)];
        state.noteOnPending = true;
    }
}

void handle_boundary(EngineState& state,
                     BlockResult& result,
                     const std::uint32_t frame,
                     const double absBoundaryBeat,
                     const double beatsPerBar,
                     const int segmentCount)
{
    const double segmentBeats = counterpointer_segment_beats_for_controls(state.controls, beatsPerBar);
    const std::int64_t boundaryIndex = static_cast<std::int64_t>(std::llround(absBoundaryBeat / segmentBeats));
    const bool cycleBoundary = segmentCount > 0 ? ((boundaryIndex % segmentCount) == 0) : false;

    if (cycleBoundary)
    {
        if (!state.controls.freeze)
        {
            PhraseState built {};
            if (build_phrase_from_capture(state.capture, segmentCount, state.controls, state.variation, built))
            {
                state.basePhrase = built;
                state.playbackPhrase = built;
            }
        }

        clear_capture(state.capture);
        if (state.variation.completed_cycles < std::numeric_limits<std::int64_t>::max())
            state.variation.completed_cycles += 1;
        maybe_vary_phrase(state);
    }

    if (state.playbackPhrase.ready && state.playbackPhrase.segmentCount == segmentCount)
    {
        const int segment = counterpointer_segment_index_for_time(state.controls,
                                                                  beatsPerBar,
                                                                  segmentCount,
                                                                  absBoundaryBeat + kBeatEpsilon);
        schedule_segment(state, absBoundaryBeat, segmentBeats, segment);
    }
    else
    {
        silence_output(state, result, frame);
    }
}

bool is_note_message(const InputMidiEvent& event)
{
    if (event.size < 2)
        return false;
    const std::uint8_t type = event.data[0] & 0xf0;
    return (type == 0x80 || type == 0x90) && event.size >= 3;
}

void forward_input_event(const EngineState& state, BlockResult& result, const InputMidiEvent& event)
{
    if (state.controls.pass_input)
    {
        appendMidi(result, event);
    }
    else if (!is_note_message(event))
    {
        appendMidi(result, event);
    }
}

void process_timeline_until(EngineState& state,
                            BlockResult& result,
                            const std::uint32_t nframes,
                            const double absBeatsStart,
                            const double absBeatsEnd,
                            double targetBeat,
                            const double beatsPerBar,
                            const int segmentCount,
                            double& cursorBeats,
                            std::int64_t& boundaryIndex,
                            double& nextBoundary)
{
    if (targetBeat < cursorBeats)
        targetBeat = cursorBeats;

    const double segmentBeats = counterpointer_segment_beats_for_controls(state.controls, beatsPerBar);
    while (true)
    {
        const bool boundaryDue = nextBoundary <= targetBeat + kBeatEpsilon;
        bool noteOnDue = false;
        int noteOnIndex = -1;
        double noteOnBeat = targetBeat;
        for (int i = 0; i < kMaxHitsPerSegment; ++i)
        {
            if (!state.pendingHitActive[static_cast<std::size_t>(i)])
                continue;
            const double hitBeat = state.pendingHitBeat[static_cast<std::size_t>(i)];
            if (hitBeat <= targetBeat + kBeatEpsilon && (!noteOnDue || hitBeat < noteOnBeat))
            {
                noteOnDue = true;
                noteOnIndex = i;
                noteOnBeat = hitBeat;
            }
        }
        state.noteOnPending = noteOnDue;
        const bool noteOffDue = state.noteOffPending && state.pendingNoteOffBeat <= targetBeat + kBeatEpsilon;
        if (!boundaryDue && !noteOnDue && !noteOffDue)
            break;

        double markerBeat = targetBeat;
        enum class Marker { Boundary, NoteOn, NoteOff } marker = Marker::Boundary;
        if (boundaryDue &&
            (!noteOnDue || nextBoundary <= noteOnBeat + kBeatEpsilon) &&
            (!noteOffDue || nextBoundary <= state.pendingNoteOffBeat + kBeatEpsilon))
        {
            markerBeat = nextBoundary;
            marker = Marker::Boundary;
        }
        else if (noteOnDue && (!noteOffDue || noteOnBeat <= state.pendingNoteOffBeat + kBeatEpsilon))
        {
            markerBeat = noteOnBeat;
            marker = Marker::NoteOn;
        }
        else
        {
            markerBeat = state.pendingNoteOffBeat;
            marker = Marker::NoteOff;
        }

        if (markerBeat > cursorBeats + 1e-12)
            capture_interval(state, beatsPerBar, segmentCount, cursorBeats, markerBeat);

        const std::uint32_t frame = counterpointer_frame_for_beat(absBeatsStart, absBeatsEnd, nframes, markerBeat);
        if (marker == Marker::Boundary)
        {
            handle_boundary(state, result, frame, markerBeat, beatsPerBar, segmentCount);
            boundaryIndex += 1;
            nextBoundary = static_cast<double>(boundaryIndex) * segmentBeats;
        }
        else if (marker == Marker::NoteOn)
        {
            if (state.activeOutput)
                emit_note_off(result, frame, state.activeOutputNote, state.activeOutputChannel);
            const int channel = resolve_output_channel(state, state.controls.output_channel);
            const PhraseHit& hit = state.pendingHits[static_cast<std::size_t>(noteOnIndex)];
            emit_note_on(result, frame, hit.note, hit.velocity, channel);
            state.activeOutput = true;
            state.activeOutputNote = hit.note;
            state.activeOutputChannel = channel;
            state.pendingHitActive[static_cast<std::size_t>(noteOnIndex)] = false;
            state.noteOnPending = false;
            for (bool active : state.pendingHitActive)
                state.noteOnPending = state.noteOnPending || active;
            state.pendingNoteOffBeat = markerBeat + hit.gate * segmentBeats;
            state.noteOffPending = true;
        }
        else
        {
            silence_output(state, result, frame);
        }

        cursorBeats = markerBeat;
    }

    if (targetBeat > cursorBeats + 1e-12)
    {
        capture_interval(state, beatsPerBar, segmentCount, cursorBeats, targetBeat);
        cursorBeats = targetBeat;
    }
}

void reset_learned_state(EngineState& state)
{
    clear_capture(state.capture);
    clear_phrase(state.basePhrase);
    clear_phrase(state.playbackPhrase);
    state.variation = defaultVariationState();
}

}  // namespace

void activate(EngineState& state)
{
    clear_capture(state.capture);
    clear_held_notes(state);
    state.wasPlaying = false;
    state.lastAbsBeatsStart = 0.0;
    state.lastInputChannel = 1;
    state.activeOutput = false;
    state.activeOutputChannel = 1;
    state.noteOffPending = false;
    state.noteOnPending = false;
    state.pendingHitActive.fill(false);
    state.controlsInitialized = false;
}

void deactivate(EngineState& state)
{
    clear_held_notes(state);
    state.wasPlaying = false;
    state.activeOutput = false;
    state.noteOffPending = false;
    state.noteOnPending = false;
    state.pendingHitActive.fill(false);
}

BlockResult processBlock(EngineState& state,
                         const Controls& rawControls,
                         const TransportSnapshot& transport,
                         const std::uint32_t nframes,
                         const double sampleRate,
                         const InputMidiEvent* const midiEvents,
                         const std::uint32_t midiEventCount)
{
    BlockResult result {};
    state.controls = clampControls(rawControls);

    if (!state.controlsInitialized)
    {
        state.previousControls = state.controls;
        state.controlsInitialized = true;
    }

    const bool learnTriggered = state.controls.action_learn != state.previousControls.action_learn;
    const bool paramsChanged = !phraseControlsMatch(state.controls, state.previousControls);
    const bool longRandomChanged = std::fabs(state.controls.long_random - state.previousControls.long_random) >= 0.0001f;
    if (learnTriggered || paramsChanged)
    {
        silence_output(state, result, 0);
        reset_learned_state(state);
    }
    else if (longRandomChanged)
    {
        state.variation = defaultVariationState();
    }
    state.previousControls = state.controls;

    if (!transport.valid || !transport.playing || transport.bpm <= 0.0 || transport.beatsPerBar <= 0.0 || sampleRate <= 0.0)
    {
        if (state.wasPlaying || state.activeOutput || state.noteOffPending || state.noteOnPending)
            silence_output(state, result, 0);

        clear_held_notes(state);
        state.wasPlaying = false;
        for (std::uint32_t i = 0; i < midiEventCount; ++i)
            forward_input_event(state, result, midiEvents[i]);
        result.ready = state.playbackPhrase.ready;
        return result;
    }

    const int segmentCount = counterpointer_segment_count_for_controls(state.controls, transport.beatsPerBar);
    if ((state.playbackPhrase.ready && state.playbackPhrase.segmentCount != segmentCount) ||
        (state.basePhrase.ready && state.basePhrase.segmentCount != segmentCount))
    {
        silence_output(state, result, 0);
        reset_learned_state(state);
    }

    const double absBeatsStart = transport.bar * transport.beatsPerBar + transport.barBeat;
    const double absBeatsStep = (static_cast<double>(nframes) * transport.bpm) / (60.0 * sampleRate);
    const double absBeatsEnd = absBeatsStart + absBeatsStep;
    const double segmentBeats = counterpointer_segment_beats_for_controls(state.controls, transport.beatsPerBar);
    const bool restart = !state.wasPlaying || (absBeatsStart + kBeatEpsilon < state.lastAbsBeatsStart);
    if (restart)
    {
        silence_output(state, result, 0);
        state.noteOnPending = false;
        state.noteOffPending = false;
    }

    std::int64_t boundaryIndex = static_cast<std::int64_t>(std::ceil((absBeatsStart - kBeatEpsilon) / segmentBeats));
    double nextBoundary = static_cast<double>(boundaryIndex) * segmentBeats;
    double cursorBeats = absBeatsStart;

    for (std::uint32_t i = 0; i < midiEventCount; ++i)
    {
        const InputMidiEvent& event = midiEvents[i];
        if (event.size < 2)
            continue;

        if ((event.data[0] & 0xf0) != 0xf0)
            state.lastInputChannel = static_cast<int>((event.data[0] & 0x0f) + 1);

        const double eventBeats = absBeatsStart +
                                  (static_cast<double>(event.frame) / static_cast<double>(std::max(1u, nframes))) *
                                      absBeatsStep;
        process_timeline_until(state,
                               result,
                               nframes,
                               absBeatsStart,
                               absBeatsEnd,
                               eventBeats,
                               transport.beatsPerBar,
                               segmentCount,
                               cursorBeats,
                               boundaryIndex,
                               nextBoundary);

        forward_input_event(state, result, event);

        const std::uint8_t type = event.data[0] & 0xf0;
        if ((type == 0x90 || type == 0x80) && event.size >= 3)
        {
            const std::uint8_t note = event.data[1] & 0x7f;
            const std::uint8_t velocity = event.data[2] & 0x7f;
            const bool isNoteOn = type == 0x90 && velocity > 0;
            if (isNoteOn)
            {
                state.heldNotes[static_cast<std::size_t>(note)] = true;
                state.heldVelocity[static_cast<std::size_t>(note)] = velocity;
                capture_onset(state, transport.beatsPerBar, segmentCount, eventBeats, note, velocity);
            }
            else
            {
                state.heldNotes[static_cast<std::size_t>(note)] = false;
                state.heldVelocity[static_cast<std::size_t>(note)] = 0;
            }
        }
    }

    process_timeline_until(state,
                           result,
                           nframes,
                           absBeatsStart,
                           absBeatsEnd,
                           absBeatsEnd,
                           transport.beatsPerBar,
                           segmentCount,
                           cursorBeats,
                           boundaryIndex,
                           nextBoundary);

    state.wasPlaying = true;
    state.lastAbsBeatsStart = absBeatsStart;
    result.ready = state.playbackPhrase.ready;
    return result;
}

}  // namespace downspout::counterpointer
