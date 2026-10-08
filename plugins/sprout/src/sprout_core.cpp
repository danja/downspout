#include "sprout_core.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::sprout {

namespace {

struct PresetDef {
    const char* name;
    const char* rules;
    const char* axiom;
    char lhs[3];
    const char* rhs[3];
};

// F and G are notes, f is a rest, + and - move the pitch, [ and ] branch. Every
// other symbol is a variable that expands but sounds nothing.
constexpr std::array<PresetDef, kPresetCount> kPresets {{
    {"Plant", "X; F -> FF; X -> F+[[X]-X]-F[-FX]+X", "X", {'F', 'X', 0}, {"FF", "F+[[X]-X]-F[-FX]+X", ""}},
    {"Koch", "F; F -> F+F-F-F+F", "F", {'F', 0, 0}, {"F+F-F-F+F", "", ""}},
    {"Dragon", "FX; X -> X+YF+; Y -> -FX-Y", "FX", {'X', 'Y', 0}, {"X+YF+", "-FX-Y", ""}},
    {"Sierpinski", "F-G-G; F -> F-G+F+G-F; G -> GG", "F-G-G", {'F', 'G', 0}, {"F-G+F+G-F", "GG", ""}},
    {"Cantor", "F; F -> FfF; f -> fff", "F", {'F', 'f', 0}, {"FfF", "fff", ""}},
    {"Levy", "F; F -> +F--F+", "F", {'F', 0, 0}, {"+F--F+", "", ""}},
    {"Tree", "F; F -> F[+F]F[-F]F", "F", {'F', 0, 0}, {"F[+F]F[-F]F", "", ""}},
}};

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

std::string expandOnce(const PresetDef& def, const std::string& in)
{
    std::string out;
    for (const char c : in) {
        const char* replacement = nullptr;
        for (int r = 0; r < 3; ++r)
            if (def.lhs[r] != 0 && def.lhs[r] == c) replacement = def.rhs[r];
        if (replacement != nullptr) out += replacement;
        else out += c;
    }
    return out;
}

Sequence interpret(const std::string& symbols, const int generation)
{
    Sequence seq;
    seq.generation = generation;
    int unit = 0;
    std::array<int, 33> stack {};
    int depth = 0;
    for (const char c : symbols) {
        switch (c) {
        case 'F':
        case 'G':
            seq.steps.push_back({static_cast<std::int16_t>(unit), static_cast<std::uint8_t>(depth), true});
            break;
        case 'f':
            seq.steps.push_back({static_cast<std::int16_t>(unit), static_cast<std::uint8_t>(depth), false});
            break;
        case '+': unit = std::min(unit + 1, 4096); break;
        case '-': unit = std::max(unit - 1, -4096); break;
        case '[':
            if (depth < 32) stack[static_cast<std::size_t>(depth++)] = unit;
            break;
        case ']':
            if (depth > 0) unit = stack[static_cast<std::size_t>(--depth)];
            break;
        default: break;  // variables
        }
    }
    return seq;
}

struct Tables {
    // [preset][generation] -> symbol string and sequence, for the generations that fit.
    std::array<std::vector<std::string>, kPresetCount> strings;
    std::array<std::vector<Sequence>, kPresetCount> sequences;

    Tables()
    {
        for (int p = 0; p < kPresetCount; ++p) {
            std::string current = kPresets[static_cast<std::size_t>(p)].axiom;
            for (int g = 0; g <= kMaxGeneration; ++g) {
                if (g > 0) current = expandOnce(kPresets[static_cast<std::size_t>(p)], current);
                if (current.size() > kMaxSymbols) break;
                auto seq = interpret(current, g);
                if (seq.steps.size() > kMaxSymbols) break;
                strings[static_cast<std::size_t>(p)].push_back(current);
                sequences[static_cast<std::size_t>(p)].push_back(std::move(seq));
            }
        }
    }
};

const Tables& tables()
{
    static const Tables t;
    return t;
}

float pv(const std::array<float, kParameterCount>& p, const std::uint32_t i)
{
    return downspout::generative::clampParam(p[i], kParameterSpecs[i]);
}

int iv(const std::array<float, kParameterCount>& p, const std::uint32_t i)
{
    return static_cast<int>(std::lround(pv(p, i)));
}

void release(State& s, MidiBlock& out, const std::uint32_t frame)
{
    if (s.activeNote >= 0) {
        out.push(frame, downspout::generative::status(false, s.activeChannel), static_cast<std::uint8_t>(s.activeNote), 0);
        s.activeNote = -1;
    }
}

}  // namespace

const char* presetName(const int preset) noexcept
{
    return kPresets[static_cast<std::size_t>(std::clamp(preset, 0, kPresetCount - 1))].name;
}

const char* presetRules(const int preset) noexcept
{
    return kPresets[static_cast<std::size_t>(std::clamp(preset, 0, kPresetCount - 1))].rules;
}

const char* scaleName(const int scale) noexcept
{
    return kScaleNames[static_cast<std::size_t>(std::clamp(scale, 0, kScaleCount - 1))];
}

void prepare() { (void)tables(); }

const Sequence& sequenceFor(const int preset, const int generation) noexcept
{
    const auto& list = tables().sequences[static_cast<std::size_t>(std::clamp(preset, 0, kPresetCount - 1))];
    return list[static_cast<std::size_t>(std::clamp(generation, 0, static_cast<int>(list.size()) - 1))];
}

std::string expansion(const int preset, const int generation)
{
    const auto& list = tables().strings[static_cast<std::size_t>(std::clamp(preset, 0, kPresetCount - 1))];
    return list[static_cast<std::size_t>(std::clamp(generation, 0, static_cast<int>(list.size()) - 1))];
}

const std::uint8_t* scaleIntervals(const int scale, int& count) noexcept
{
    const auto index = static_cast<std::size_t>(std::clamp(scale, 0, kScaleCount - 1));
    count = kDegreeCount[index];
    return kIntervals[index].data();
}

int foldDegree(const int degree, const int range) noexcept
{
    const int r = std::max(1, range);
    const int period = 4 * r;
    int u = (degree + r) % period;
    if (u < 0) u += period;
    if (u > 2 * r) u = period - u;
    return u - r;
}

int degreeToNote(const int scale, const int root, const int degree) noexcept
{
    int count = 0;
    const std::uint8_t* intervals = scaleIntervals(scale, count);
    int octave = degree / count;
    int index = degree % count;
    if (index < 0) {
        index += count;
        --octave;
    }
    return std::clamp(root + 12 * octave + intervals[index], 0, 127);
}

int usableGeneration(const int preset, const int generations) noexcept
{
    return sequenceFor(preset, std::clamp(generations, 0, kMaxGeneration)).generation;
}

int generationAtBar(const int generations, const int growBars, const std::int64_t bar) noexcept
{
    const int top = std::clamp(generations, 1, kMaxGeneration);
    if (growBars <= 0) return top;
    const std::int64_t g = 1 + std::max<std::int64_t>(0, bar) / growBars;
    return static_cast<int>(std::min<std::int64_t>(g, top));
}

namespace {
constexpr const char* kGridNames[kGridCount] = {"1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8",
                                                "1/8.", "1/4T", "1/4", "1/4.", "1/2", "1 bar"};
constexpr double kGridQuarters[kGridCount - 1] = {0.125, 1.0 / 6.0, 0.25, 0.375, 1.0 / 3.0, 0.5,
                                                   0.75, 2.0 / 3.0, 1.0, 1.5, 2.0};
}  // namespace

const char* gridName(const int grid) noexcept { return kGridNames[std::clamp(grid, 0, kGridCount - 1)]; }

double gridQuarters(const int grid, const double barQuarters) noexcept
{
    const int g = std::clamp(grid, 0, kGridCount - 1);
    return g == kGridBar ? std::max(0.25, barQuarters) : kGridQuarters[g];
}

void reset(State& s) noexcept { s = {}; }

MidiBlock process(State& s, const std::array<float, kParameterCount>& p, const Transport& t, const std::uint32_t frames,
                  const double sampleRate) noexcept
{
    MidiBlock out;
    if (!t.valid || !t.playing || frames == 0) {
        release(s, out, 0);
        s.havePosition = false;
        s.lastStep = -1;
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
    }

    // A held note whose gate ends inside this block.
    if (s.activeNote >= 0 && s.offQuarter < end)
        release(s, out, downspout::generative::frameAt(s.offQuarter, start, qpf, frames));

    std::int64_t step = s.lastStep < 0 ? static_cast<std::int64_t>(std::ceil((start - 1e-8) / grid)) : s.lastStep + 1;
    double boundary = step * grid;

    const int preset = iv(p, kPreset);
    const int generations = iv(p, kGenerations);
    const int growBars = iv(p, kGrowBars);
    const int stepSize = iv(p, kStepSize);
    const int range = iv(p, kRange);
    const int scale = iv(p, kScale);
    const int root = iv(p, kRoot);
    const int channel = iv(p, kChannel);
    const int baseVelocity = iv(p, kVelocity);
    const float probability = pv(p, kProbability);
    const double gate = pv(p, kGate);
    const auto seed = static_cast<std::uint64_t>(iv(p, kSeed));

    while (boundary < end - 1e-8) {
        const auto frame = downspout::generative::frameAt(boundary, start, qpf, frames);
        release(s, out, frame);

        const auto bar = static_cast<std::int64_t>(std::floor((boundary + 1e-8) / barQ));
        const int wanted = generationAtBar(usableGeneration(preset, generations), growBars, bar);
        const Sequence& seq = sequenceFor(preset, wanted);
        const auto total = static_cast<std::int64_t>(seq.steps.size());
        if (total > 0) {
            // The pattern restarts each time a new generation begins.
            const std::int64_t startBar = growBars > 0 ? static_cast<std::int64_t>(wanted - 1) * growBars : 0;
            const auto startStep = static_cast<std::int64_t>(std::llround(static_cast<double>(startBar) * barQ / grid));
            std::int64_t pos = (step - startStep) % total;
            if (pos < 0) pos += total;
            const Step& cell = seq.steps[static_cast<std::size_t>(pos)];
            s.statusLength = static_cast<int>(total);
            s.statusGeneration = seq.generation;
            s.statusStep = static_cast<int>(pos);

            if (cell.note && downspout::generative::randomUnit(seed, static_cast<std::uint64_t>(step)) <= probability) {
                const int degree = foldDegree(cell.unit * stepSize, range);
                const int note = degreeToNote(scale, root, degree);
                const int velocity = std::clamp(baseVelocity - cell.depth * 10 + (pos == 0 ? 10 : 0), 1, 127);
                out.push(frame, downspout::generative::status(true, channel), static_cast<std::uint8_t>(note),
                         static_cast<std::uint8_t>(velocity));
                s.activeNote = note;
                s.activeChannel = channel;
                s.offQuarter = boundary + grid * gate;
                if (s.offQuarter < end)
                    release(s, out, downspout::generative::frameAt(s.offQuarter, start, qpf, frames));
            }
        }
        s.lastStep = step;
        ++step;
        boundary = step * grid;
    }

    s.havePosition = true;
    s.previousEnd = end;
    return out;
}

}  // namespace downspout::sprout
