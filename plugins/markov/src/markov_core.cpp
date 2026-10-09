#include "markov_core.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace downspout::markov {
namespace {

using downspout::generative::mix64;
using downspout::generative::randomUnit;

float pv(const Params& p, const std::uint32_t i)
{
    return downspout::generative::clampParam(p[i], kParameterSpecs[i]);
}

int iv(const Params& p, const std::uint32_t i)
{
    return static_cast<int>(std::lround(pv(p, i)));
}

std::int64_t floorDiv(const std::int64_t a, const std::int64_t b) noexcept
{
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// ---- Scales: the canonical 24 of docs/scales.md, semitones above the root ------------------------------------
constexpr std::array<const char*, kScaleCount> kScaleNames {{
    "Chromatic", "Major", "Ionian", "Minor", "Harmonic Minor", "Melodic Minor", "Dorian", "Phrygian",
    "Lydian", "Mixolydian", "Locrian", "Phrygian Dominant", "Neapolitan Major", "Neapolitan Minor",
    "Pentatonic Major", "Pentatonic Minor", "Blues", "Whole Tone", "Altered", "Half-Whole Dim",
    "Whole-Half Dim", "Bebop Dominant", "Bebop Major", "Bebop Minor",
}};

constexpr std::array<std::array<std::uint8_t, 12>, kScaleCount> kIntervals {{
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {{0, 2, 4, 5, 7, 9, 11}},
    {{0, 2, 4, 5, 7, 9, 11}},
    {{0, 2, 3, 5, 7, 8, 10}},
    {{0, 2, 3, 5, 7, 8, 11}},
    {{0, 2, 3, 5, 7, 9, 11}},
    {{0, 2, 3, 5, 7, 9, 10}},
    {{0, 1, 3, 5, 7, 8, 10}},
    {{0, 2, 4, 6, 7, 9, 11}},
    {{0, 2, 4, 5, 7, 9, 10}},
    {{0, 1, 3, 5, 6, 8, 10}},
    {{0, 1, 4, 5, 7, 8, 10}},
    {{0, 1, 3, 5, 7, 9, 11}},
    {{0, 1, 3, 5, 7, 8, 11}},
    {{0, 2, 4, 7, 9}},
    {{0, 3, 5, 7, 10}},
    {{0, 3, 5, 6, 7, 10}},
    {{0, 2, 4, 6, 8, 10}},
    {{0, 1, 3, 4, 6, 8, 10}},
    {{0, 1, 3, 4, 6, 7, 9, 10}},
    {{0, 2, 3, 5, 6, 8, 9, 11}},
    {{0, 2, 4, 5, 7, 9, 10, 11}},
    {{0, 2, 4, 5, 7, 8, 9, 11}},
    {{0, 2, 3, 4, 5, 7, 9, 10}},
}};

constexpr std::array<std::uint8_t, kScaleCount> kDegreeCount {{
    12, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 7, 8, 8, 8, 8, 8,
}};

// ---- Grid (the same twelve divisions as Sprout) --------------------------------------------------------------
constexpr const char* kGridNames[kGridCount] = {"1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8",
                                                "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1 bar"};
constexpr double kGridQuarters[kGridCount - 1] = {0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5,
                                                   0.75, 2.0 / 3.0, 1.0, 1.5, 2.0};

// ---- Styles ------------------------------------------------------------------------------------------------
constexpr const char* kStyleNames[kStyleCount] = {"Stepwise", "Triadic", "Fifths",    "Pentatonic",
                                                  "Blues",    "Chromatic", "Tonic pull", "Uniform"};

// Weight of a move of `interval` semitones up (mod 12), for the transposition-invariant styles.
constexpr std::array<std::array<std::uint8_t, 12>, kStyleCount> kIntervalWeights {{
    {{2, 7, 8, 4, 3, 2, 1, 2, 3, 4, 8, 7}},  // Stepwise: seconds, with some thirds
    {{1, 1, 2, 7, 8, 3, 1, 8, 7, 3, 2, 1}},  // Triadic: thirds and fifths
    {{1, 1, 3, 2, 2, 8, 1, 8, 2, 2, 3, 1}},  // Fifths: fourths and fifths
    {{2, 1, 7, 6, 3, 5, 1, 5, 3, 6, 7, 1}},  // Pentatonic: seconds, minor thirds, fourths
    {{3, 2, 4, 8, 3, 6, 6, 5, 2, 4, 8, 1}},  // Blues: minor thirds, flat fifths, flat sevenths
    {{1, 8, 4, 2, 1, 1, 1, 1, 1, 2, 4, 8}},  // Chromatic: semitone crawl
    {{2, 7, 8, 4, 3, 2, 1, 2, 3, 4, 8, 7}},  // Tonic pull: stepwise, then pulled toward the tonic below
    {{4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4}},  // Uniform
}};

[[nodiscard]] std::uint8_t clampWeight(const int w) noexcept
{
    return static_cast<std::uint8_t>(std::clamp(w, 0, kMaxWeight));
}

// Learned counts: when any count reaches the top, halve every cell so the table keeps its proportions.
template <std::size_t N>
void bumpCount(std::array<std::uint16_t, N>& table, const std::size_t index)
{
    if (table[index] == 0xffffu) {
        for (auto& c : table) c = static_cast<std::uint16_t>(c >> 1);
    }
    ++table[index];
}

float chaosExponent(const float chaos) noexcept
{
    return std::pow(2.0f, 1.0f - 2.0f * chaos);
}

bool allowed(const std::uint16_t mask, const int state) noexcept
{
    return ((mask >> state) & 1u) != 0;
}

int pitchClassOf(const int note, const int rootPc) noexcept
{
    return ((note - rootPc) % 12 + 12) % 12;
}

void release(State& s, MidiBlock& out, const std::uint32_t frame)
{
    if (s.activeNote >= 0) {
        out.push(frame, downspout::generative::status(false, s.activeChannel), static_cast<std::uint8_t>(s.activeNote), 0);
        s.activeNote = -1;
    }
}

}  // namespace

// ---- Model and styles ----------------------------------------------------------------------------------------
const char* styleName(const int style) noexcept
{
    return kStyleNames[static_cast<std::size_t>(std::clamp(style, 0, kStyleCount - 1))];
}

void loadStyle(Model& model, const int style) noexcept
{
    const int s = std::clamp(style, 0, kStyleCount - 1);
    const auto& weights = kIntervalWeights[static_cast<std::size_t>(s)];
    for (int from = 0; from < kStates; ++from) {
        for (int to = 0; to < kStates; ++to) {
            int w = weights[static_cast<std::size_t>((to - from + 12) % 12)];
            if (s == 6) {
                // Tonic pull: stepwise motion, plus a lean toward the tonic, the fifth and the third, and the
                // classic resolutions (leading tone and fifth to the tonic, fourth to the third).
                static constexpr int kBonus[12] = {3, 0, 1, 0, 1, 0, 0, 2, 0, 0, 0, 0};
                w += kBonus[to];
                if (to == 0 && (from == 11 || from == 7 || from == 2)) w = kMaxWeight;
                if (to == 4 && from == 5) w = kMaxWeight - 1;
            }
            model.base[static_cast<std::size_t>(from * kStates + to)] = clampWeight(w);
        }
    }
}

Model defaultModel()
{
    Model model;
    loadStyle(model, 0);
    return model;
}

void clearBase(Model& model) noexcept
{
    model.base.fill(0);
}

void randomiseBase(Model& model, const std::uint64_t seed) noexcept
{
    for (int i = 0; i < kCells; ++i) {
        const float unit = randomUnit(seed, static_cast<std::uint64_t>(i) * 2);
        // About a third of the moves are never taken, which gives the matrix some character.
        model.base[static_cast<std::size_t>(i)] =
            unit < 0.34f ? 0 : clampWeight(1 + static_cast<int>(randomUnit(seed, static_cast<std::uint64_t>(i) * 2 + 1) * kMaxWeight));
    }
}

void bumpCell(Model& model, const int from, const int to) noexcept
{
    if (from < 0 || from >= kStates || to < 0 || to >= kStates) return;
    auto& w = model.base[static_cast<std::size_t>(from * kStates + to)];
    w = static_cast<std::uint8_t>((w + 1) % (kMaxWeight + 1));
}

int setCell(Model& model, const int from, const int to, const int weight) noexcept
{
    if (from < 0 || from >= kStates || to < 0 || to >= kStates) return -1;
    auto& w = model.base[static_cast<std::size_t>(from * kStates + to)];
    w = clampWeight(weight);
    return w;
}

void clearLearned(Model& model) noexcept
{
    model.learned1.fill(0);
    model.learned2.fill(0);
    model.learnedTotal = 0;
}

// ---- Scales and grid -------------------------------------------------------------------------------------------
const char* scaleName(const int scale) noexcept
{
    return kScaleNames[static_cast<std::size_t>(std::clamp(scale, 0, kScaleCount - 1))];
}

std::uint16_t scaleMask(const int scale) noexcept
{
    const auto index = static_cast<std::size_t>(std::clamp(scale, 0, kScaleCount - 1));
    std::uint16_t mask = 0;
    for (int i = 0; i < kDegreeCount[index]; ++i)
        mask = static_cast<std::uint16_t>(mask | (1u << kIntervals[index][static_cast<std::size_t>(i)]));
    return mask;
}

const char* gridName(const int grid) noexcept
{
    return kGridNames[std::clamp(grid, 0, kGridCount - 1)];
}

double gridQuarters(const int grid, const double barQuarters) noexcept
{
    const int g = std::clamp(grid, 0, kGridCount - 1);
    return g == kGridCount - 1 ? std::max(0.25, barQuarters) : kGridQuarters[g];
}

// ---- The chain -------------------------------------------------------------------------------------------------
Matrix effectiveMatrix(const Model& model, const Params& params)
{
    Matrix out {};
    const std::uint16_t mask = scaleMask(iv(params, kScale));
    const float mix = pv(params, kLearnedMix);
    const float exponent = chaosExponent(pv(params, kChaos));

    for (int from = 0; from < kStates; ++from) {
        std::array<float, kStates> row {};
        std::uint16_t maxCount = 0;
        for (int to = 0; to < kStates; ++to)
            maxCount = std::max(maxCount, model.learned1[static_cast<std::size_t>(from * kStates + to)]);

        for (int to = 0; to < kStates; ++to) {
            float weight = static_cast<float>(model.base[static_cast<std::size_t>(from * kStates + to)]);
            if (mix > 0.0f && maxCount > 0) {
                const float learned = static_cast<float>(kMaxWeight)
                                      * static_cast<float>(model.learned1[static_cast<std::size_t>(from * kStates + to)])
                                      / static_cast<float>(maxCount);
                weight = (1.0f - mix) * weight + mix * learned;
            }
            row[static_cast<std::size_t>(to)] = allowed(mask, to) ? weight : 0.0f;
        }

        float total = 0.0f;
        for (const float w : row) total += w;
        if (total <= 0.0f) {
            // Nothing is allowed from here (a cleared row, or only out-of-scale notes drawn): any note in the scale.
            for (int to = 0; to < kStates; ++to)
                row[static_cast<std::size_t>(to)] = allowed(mask, to) ? 1.0f : 0.0f;
        }
        for (int to = 0; to < kStates; ++to) {
            const float w = row[static_cast<std::size_t>(to)];
            out[static_cast<std::size_t>(from * kStates + to)] = w > 0.0f ? std::pow(w, exponent) : 0.0f;
        }
    }
    return out;
}

void nextRow(const Model& model, const Params& params, const Matrix& matrix, const int previous2, const int previous1,
             std::array<float, kStates>& row)
{
    const int from = std::clamp(previous1, 0, kStates - 1);
    for (int to = 0; to < kStates; ++to)
        row[static_cast<std::size_t>(to)] = matrix[static_cast<std::size_t>(from * kStates + to)];

    if (iv(params, kOrder) < 2 || previous2 < 0 || previous2 >= kStates) return;

    // Order 2: the learned second-order counts for (two back, one back), where there are any.
    const std::size_t base = static_cast<std::size_t>((previous2 * kStates + from) * kStates);
    std::uint16_t maxCount = 0;
    std::uint32_t total = 0;
    for (int to = 0; to < kStates; ++to) {
        const std::uint16_t c = model.learned2[base + static_cast<std::size_t>(to)];
        maxCount = std::max(maxCount, c);
        total += c;
    }
    if (total < 2) return;

    const std::uint16_t mask = scaleMask(iv(params, kScale));
    const float exponent = chaosExponent(pv(params, kChaos));
    std::array<float, kStates> second {};
    float sum = 0.0f;
    for (int to = 0; to < kStates; ++to) {
        const float c = static_cast<float>(model.learned2[base + static_cast<std::size_t>(to)]);
        const float w = allowed(mask, to) && c > 0.0f ? static_cast<float>(kMaxWeight) * c / static_cast<float>(maxCount) : 0.0f;
        second[static_cast<std::size_t>(to)] = w > 0.0f ? std::pow(w, exponent) : 0.0f;
        sum += second[static_cast<std::size_t>(to)];
    }
    if (sum > 0.0f) row = second;
}

int sampleRow(const std::array<float, kStates>& row, const float unit) noexcept
{
    float total = 0.0f;
    for (const float w : row) total += w;
    if (total <= 0.0f) return 0;
    const float target = std::clamp(unit, 0.0f, 0.999999f) * total;
    float cumulative = 0.0f;
    int last = 0;
    for (int i = 0; i < kStates; ++i) {
        const float w = row[static_cast<std::size_t>(i)];
        if (w <= 0.0f) continue;
        cumulative += w;
        last = i;
        if (target < cumulative) return i;
    }
    return last;
}

WalkNote walkNote(const Model& model, const Params& params, const Matrix& matrix, const std::uint64_t seed,
                  const std::int64_t phrase, const int k)
{
    const int rootPc = ((iv(params, kRoot) % 12) + 12) % 12;
    const int low = iv(params, kLow);
    const int octaves = iv(params, kOctaves);
    const int high = low + octaves * 12 - 1;
    const int centre = low + octaves * 6;
    const std::uint64_t phraseSeed = mix64(seed + mix64(static_cast<std::uint64_t>(phrase) + 1));

    int previous2 = -1;
    int previous1 = 0;  // each phrase starts as if the last note was the tonic
    int previousNote = -1;
    WalkNote result;
    std::array<float, kStates> row {};
    for (int i = 0; i <= std::max(0, k); ++i) {
        nextRow(model, params, matrix, previous2, previous1, row);
        const int state = sampleRow(row, randomUnit(phraseSeed, static_cast<std::uint64_t>(i)));

        // Place it in the register: the octave nearest the last note (or the middle of the range for the first).
        const int target = previousNote >= 0 ? previousNote : centre;
        const int absolutePc = (rootPc + state) % 12;
        int best = -1;
        int bestDistance = 1 << 20;
        for (int n = low; n <= high; ++n) {
            if (n % 12 != absolutePc) continue;
            const int distance = std::abs(n - target);
            if (distance < bestDistance) {
                best = n;
                bestDistance = distance;
            }
        }
        if (best < 0) best = std::clamp(centre, 0, 127);

        result = {state, std::clamp(best, 0, 127)};
        previous2 = previous1;
        previous1 = state;
        previousNote = result.note;
    }
    return result;
}

// ---- Playback ---------------------------------------------------------------------------------------------------
void reset(State& state) noexcept
{
    state = {};
}

bool handleMidi(State& s, Model& model, Params& p, const MidiEvent* events, const std::uint32_t count) noexcept
{
    bool learnedChanged = false;

    // Clear learned is a counter: the first value seen is adopted, and any later change wipes what was learned.
    const int clearSerial = iv(p, kClearLearned);
    if (s.clearSerial < 0) {
        s.clearSerial = clearSerial;
    } else if (clearSerial != s.clearSerial) {
        s.clearSerial = clearSerial;
        clearLearned(model);
        s.learnPrevious1 = s.learnPrevious2 = -1;
        learnedChanged = true;
    }
    const bool learning = pv(p, kLearn) >= 0.5f;
    if (!learning) s.learnPrevious1 = s.learnPrevious2 = -1;  // turning Learn off forgets the line
    if (events == nullptr) return learnedChanged;

    const int learnChannel = iv(p, kLearnChannel);
    const int rootPc = ((iv(p, kRoot) % 12) + 12) % 12;
    const int conductor = iv(p, kConductorCh);

    for (std::uint32_t i = 0; i < count; ++i) {
        const MidiEvent& e = events[i];
        if (e.size < 3) continue;
        const int kind = e.data[0] & 0xf0;
        const int channel = (e.data[0] & 0x0f) + 1;
        const int d1 = e.data[1] & 0x7f;
        const int d2 = e.data[2] & 0x7f;

        if (kind == 0x90 && d2 > 0 && learning && (learnChannel == 0 || channel == learnChannel)) {
            const int state = pitchClassOf(d1, rootPc);
            if (s.learnPrevious1 >= 0) {
                bumpCount(model.learned1, static_cast<std::size_t>(s.learnPrevious1 * kStates + state));
                if (s.learnPrevious2 >= 0)
                    bumpCount(model.learned2,
                              static_cast<std::size_t>((s.learnPrevious2 * kStates + s.learnPrevious1) * kStates + state));
                ++model.learnedTotal;
                learnedChanged = true;
            }
            s.learnPrevious2 = s.learnPrevious1;
            s.learnPrevious1 = state;
        } else if (kind == 0xb0 && conductor > 0 && channel == conductor) {
            const float unit = static_cast<float>(d2) / 127.0f;
            const auto spec = [&](const Param id) { return kParameterSpecs[id]; };
            switch (d1) {
            case 21: p[kDensity] = downspout::generative::clampParam(unit, spec(kDensity)); break;
            case 22:
                p[kVelocity] = downspout::generative::clampParam(
                    spec(kVelocity).minimum + unit * (spec(kVelocity).maximum - spec(kVelocity).minimum), spec(kVelocity));
                break;
            case 23: p[kChaos] = downspout::generative::clampParam(unit, spec(kChaos)); break;
            case 24:
                if (d2 == 127) s.restartPending = true;
                break;
            default: break;
            }
        }
    }
    return learnedChanged;
}

MidiBlock process(State& s, const Model& model, const Params& p, const Transport& t, const std::uint32_t frames,
                  const double sampleRate) noexcept
{
    MidiBlock out;
    if (!t.valid || !t.playing || frames == 0) {
        release(s, out, 0);
        s.havePosition = false;
        s.lastStep = -1;
        s.lastBar = -1;
        s.restartPending = false;
        s.restartStep = 0;
        s.statusState = -1;
        return out;
    }

    const double qpf = std::clamp(t.bpm, 1.0, 999.0) / (60.0 * std::max(1.0, sampleRate));
    const double start = downspout::generative::absoluteQuarter(t);
    const double end = start + frames * qpf;
    const double barQ = downspout::generative::barLengthQuarters(t);
    const double grid = gridQuarters(iv(p, kGrid), barQ);

    if (downspout::generative::isDiscontinuity(s.havePosition, s.previousEnd, start)) {
        release(s, out, 0);
        s.lastStep = -1;
        s.lastBar = -1;
        s.restartPending = false;
        s.restartStep = 0;
    }

    // A held note whose gate ends inside this block.
    if (s.activeNote >= 0 && s.offQuarter < end)
        release(s, out, downspout::generative::frameAt(s.offQuarter, start, qpf, frames));

    std::int64_t step = s.lastStep < 0 ? static_cast<std::int64_t>(std::ceil((start - 1e-8) / grid)) : s.lastStep + 1;
    double boundary = static_cast<double>(step) * grid;

    const Matrix matrix = effectiveMatrix(model, p);
    const std::int64_t phraseSteps = std::max<std::int64_t>(1, std::llround(iv(p, kPhraseBars) * barQ / grid));
    const int channel = iv(p, kChannel);
    const int baseVelocity = iv(p, kVelocity);
    const float density = pv(p, kDensity);
    const double gate = pv(p, kGate);
    const int seedParam = iv(p, kSeed);

    while (boundary < end - 1e-8) {
        const auto frame = downspout::generative::frameAt(boundary, start, qpf, frames);
        release(s, out, frame);

        // A Conductor reset waits for a bar line, then the phrase count starts again and the melody is re-rolled.
        const auto bar = static_cast<std::int64_t>(std::floor((boundary + 1e-8) / barQ));
        if (bar != s.lastBar) {
            s.lastBar = bar;
            if (s.restartPending) {
                s.restartStep = step;
                ++s.salt;
                s.restartPending = false;
            }
        }

        const std::int64_t relative = step - s.restartStep;
        const std::int64_t phrase = floorDiv(relative, phraseSteps);
        const int k = static_cast<int>(relative - phrase * phraseSteps);
        const std::uint64_t walkSeed =
            static_cast<std::uint64_t>(seedParam) * 7919ull + static_cast<std::uint64_t>(s.salt) * 104729ull;
        const WalkNote walk = walkNote(model, p, matrix, walkSeed, phrase, k);
        s.statusState = walk.state;

        if (randomUnit(static_cast<std::uint64_t>(seedParam) * 131ull + static_cast<std::uint64_t>(s.salt), static_cast<std::uint64_t>(step)) < density) {
            const bool onBeat = std::fabs(boundary - std::round(boundary)) < 1e-6;
            const int velocity = std::clamp(baseVelocity + (k == 0 ? 12 : 0) + (onBeat ? 6 : 0), 1, 127);
            out.push(frame, downspout::generative::status(true, channel), static_cast<std::uint8_t>(walk.note),
                     static_cast<std::uint8_t>(velocity));
            s.activeNote = walk.note;
            s.activeChannel = channel;
            s.offQuarter = boundary + grid * gate;
            if (s.offQuarter < end)
                release(s, out, downspout::generative::frameAt(s.offQuarter, start, qpf, frames));
        }

        s.lastStep = step;
        ++step;
        boundary = static_cast<double>(step) * grid;
    }

    s.havePosition = true;
    s.previousEnd = end;
    return out;
}

// ---- State text ---------------------------------------------------------------------------------------------------
// version=1
// base=<144 digits 0-8, row by row>
// l1=<index>:<count>,...   (nonzero cells only)
// l2=<index>:<count>,...
std::string serializeModel(const Model& model)
{
    std::string text = "version=1\nbase=";
    for (const std::uint8_t w : model.base)
        text += static_cast<char>('0' + std::min<int>(w, kMaxWeight));
    text += '\n';

    const auto sparse = [&text](const char* key, const auto& table) {
        text += key;
        text += '=';
        bool first = true;
        for (std::size_t i = 0; i < table.size(); ++i) {
            if (table[i] == 0) continue;
            if (!first) text += ',';
            first = false;
            text += std::to_string(i) + ':' + std::to_string(table[i]);
        }
        text += '\n';
    };
    sparse("l1", model.learned1);
    sparse("l2", model.learned2);
    return text;
}

std::optional<Model> deserializeModel(const std::string_view text)
{
    Model model;
    bool sawVersion = false;
    bool sawBase = false;

    const auto parseNumber = [](const std::string_view s, auto& value) {
        const auto result = std::from_chars(s.data(), s.data() + s.size(), value);
        return result.ec == std::errc() && result.ptr == s.data() + s.size();
    };
    const auto parseSparse = [&parseNumber](std::string_view list, auto& table) {
        table.fill(0);
        while (!list.empty()) {
            const std::size_t comma = list.find(',');
            const std::string_view item = list.substr(0, comma);
            const std::size_t colon = item.find(':');
            if (colon == std::string_view::npos) return false;
            std::size_t index = 0;
            unsigned int count = 0;
            if (!parseNumber(item.substr(0, colon), index) || !parseNumber(item.substr(colon + 1), count)) return false;
            if (index >= table.size() || count > 0xffffu) return false;
            table[index] = static_cast<std::uint16_t>(count);
            if (comma == std::string_view::npos) break;
            list.remove_prefix(comma + 1);
        }
        return true;
    };

    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t newline = text.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
        const std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty()) {
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos) return std::nullopt;
            const std::string_view key = line.substr(0, equals);
            const std::string_view value = line.substr(equals + 1);
            if (key == "version") {
                if (value != "1") return std::nullopt;
                sawVersion = true;
            } else if (key == "base") {
                if (value.size() != kCells) return std::nullopt;
                for (std::size_t i = 0; i < value.size(); ++i) {
                    if (value[i] < '0' || value[i] > '0' + kMaxWeight) return std::nullopt;
                    model.base[i] = static_cast<std::uint8_t>(value[i] - '0');
                }
                sawBase = true;
            } else if (key == "l1") {
                if (!parseSparse(value, model.learned1)) return std::nullopt;
            } else if (key == "l2") {
                if (!parseSparse(value, model.learned2)) return std::nullopt;
            } else {
                return std::nullopt;
            }
        }
        if (newline == std::string_view::npos) break;
    }
    if (!sawVersion || !sawBase) return std::nullopt;

    std::uint32_t total = 0;
    for (const std::uint16_t c : model.learned1) total += c;
    model.learnedTotal = total;
    return model;
}

}  // namespace downspout::markov
