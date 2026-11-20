#include "melevolve_core.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace downspout::melevolve {
namespace {

constexpr double kQuarterPerStep = 0.25;
constexpr double kTicksPerStep = static_cast<double>(kMidiClockPpq) / 4.0;
constexpr double kEpsilon = 1.0e-8;

constexpr const char* kScaleNames[] = {
    "Major", "Natural minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
    "Harmonic minor", "Major pentatonic", "Minor pentatonic", "Blues", "Whole tone", "Chromatic"
};
constexpr const char* kScaleSymbols[] = {
    "major", "minor", "dorian", "phrygian", "lydian", "mixolydian",
    "harmonicMinor", "majorPentatonic", "minorPentatonic", "blues", "wholeTone", "chromatic"
};
constexpr const char* kRootNames[] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};
constexpr const char* kTimeSignatureNames[] = {"4/4", "3/4", "6/8", "5/4", "7/8"};

constexpr std::array<std::array<std::uint8_t, 12>, static_cast<int>(ScaleId::count)> kScaleSteps {{
    {{0, 2, 4, 5, 7, 9, 11, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {{0, 1, 3, 5, 7, 8, 10, 0, 0, 0, 0, 0}},
    {{0, 2, 4, 6, 7, 9, 11, 0, 0, 0, 0, 0}},
    {{0, 2, 4, 5, 7, 9, 10, 0, 0, 0, 0, 0}},
    {{0, 2, 3, 5, 7, 8, 11, 0, 0, 0, 0, 0}},
    {{0, 2, 4, 7, 9, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 3, 5, 7, 10, 0, 0, 0, 0, 0, 0, 0}},
    {{0, 3, 5, 6, 7, 10, 0, 0, 0, 0, 0, 0}},
    {{0, 2, 4, 6, 8, 10, 0, 0, 0, 0, 0, 0}},
    {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}}
}};
constexpr std::array<int, static_cast<int>(ScaleId::count)> kDegreeCounts {7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 12};
constexpr std::array<int, kTimeSignatureCount> kNumerators {4, 3, 6, 5, 7};
constexpr std::array<int, kTimeSignatureCount> kDenominators {4, 4, 8, 4, 8};
constexpr int kVelocityLevels[] = {60, 80, 100, 120};

int clampInt(const int value, const int lo, const int hi) noexcept
{
    return std::max(lo, std::min(hi, value));
}

double finiteOr(const double value, const double fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

int scaleIndex(const ScaleId scale) noexcept
{
    return clampInt(static_cast<int>(scale), 0, static_cast<int>(ScaleId::count) - 1);
}

int defaultOctave(const Mode mode) noexcept
{
    return mode == Mode::bass ? 2 : 4;
}

double quartersPerFrame(const double bpm, const double sampleRate) noexcept
{
    return std::clamp(bpm, 1.0, 999.0) / (60.0 * std::max(1.0, sampleRate));
}

double nominalTickFrames(const double bpm, const double sampleRate) noexcept
{
    return (60.0 * std::max(1.0, sampleRate)) / (std::clamp(bpm, 1.0, 999.0) * static_cast<double>(kMidiClockPpq));
}

double barLengthQuarters(const Transport& transport) noexcept
{
    return transport.beatType > 0.0
        ? std::max(0.25, transport.beatsPerBar * 4.0 / transport.beatType)
        : 4.0;
}

double absoluteQuarter(const Transport& transport) noexcept
{
    const double beatUnit = transport.beatType > 0.0 ? 4.0 / transport.beatType : 1.0;
    return transport.bar * barLengthQuarters(transport) + transport.barBeat * beatUnit;
}

void queueEvent(Engine& engine, const double time, const int order,
                const std::uint8_t status, const std::uint8_t d1, const std::uint8_t d2)
{
    Engine::QueueEvent event;
    event.time = time;
    event.order = order;
    event.data = {status, d1, d2, 0};
    event.size = 3;
    engine.queue.push_back(event);
}

template <typename FrameFor>
void flushQueue(Engine& engine, MidiBlock& out, const double until, FrameFor frameFor)
{
    while (!engine.queue.empty()) {
        auto earliest = engine.queue.begin();
        for (auto it = engine.queue.begin() + 1; it != engine.queue.end(); ++it) {
            if (it->time < earliest->time - kEpsilon
                || (std::fabs(it->time - earliest->time) <= kEpsilon && it->order < earliest->order))
                earliest = it;
        }
        if (earliest->time > until + kEpsilon)
            break;
        const Engine::QueueEvent event = *earliest;
        engine.queue.erase(earliest);
        out.push(frameFor(event.time), event.data, event.size);
        if ((event.data[0] & 0xf0) == 0x80 && event.size >= 2) {
            const int pitch = event.data[1];
            engine.activePitches.erase(std::remove(engine.activePitches.begin(), engine.activePitches.end(), pitch),
                                       engine.activePitches.end());
        }
    }
}

void emitPanic(Engine& engine, MidiBlock& out, const std::uint32_t frame, const int channel)
{
    const std::uint8_t status = static_cast<std::uint8_t>(0x80 | (channel - 1));
    for (const int pitch : engine.activePitches)
        out.push(frame, status, static_cast<std::uint8_t>(pitch), 0);
    out.push(frame, static_cast<std::uint8_t>(0xb0 | (channel - 1)), 123, 0);
    engine.activePitches.clear();
    engine.queue.clear();
}

void finishPlayback(Session& session, Engine& engine)
{
    session.playingRow = -1;
    engine.playingRow = -1;
    engine.running = false;
    engine.queue.clear();
    engine.positionSteps = 0.0;
    engine.clockTick = 0;
}

void emitStop(Session& session, Engine& engine, MidiBlock& out, const std::uint32_t frame)
{
    emitPanic(engine, out, frame, session.settings.midiChannel);
    if (engine.settingsSendClock && engine.source != ClockSource::midi)
        out.push(frame, {0xfc, 0, 0, 0}, 1);
    finishPlayback(session, engine);
}

void emitClockStart(Engine& engine, MidiBlock& out, const std::uint32_t frame)
{
    if (!engine.pendingClockStart)
        return;
    out.push(frame, {0xfa, 0, 0, 0}, 1);
    engine.pendingClockStart = false;
}

void prepareNotes(const Session& session, Engine& engine)
{
    const int index = engine.playingRow >= 0 && engine.playingRow < static_cast<int>(session.population.rows.size())
        ? session.population.rows[static_cast<std::size_t>(engine.playingRow)]
        : -1;
    if (index < 0 || index >= static_cast<int>(session.population.entries.size())) {
        engine.notes.clear();
        engine.noteIndex.clear();
        return;
    }
    engine.notes = decode(session.population.entries[static_cast<std::size_t>(index)].genome, session.settings);
    const int length = genomeLength(session.settings);
    engine.noteIndex.assign(static_cast<std::size_t>(std::max(1, length)), {});
    for (std::size_t i = 0; i < engine.notes.size(); ++i) {
        const int start = clampInt(engine.notes[i].startStep, 0, length - 1);
        engine.noteIndex[static_cast<std::size_t>(start)].push_back(static_cast<int>(i));
    }
}

void beginTimeline(Session& session, Engine& engine, const double startQuarter)
{
    engine.running = true;
    engine.startQuarter = startQuarter;
    engine.startFrame = engine.frameCursor;
    engine.nextStep = 0.0;
    engine.positionSteps = 0.0;
    engine.clockTick = 0;
    engine.midiTickCount = 0;
    engine.lastScheduledStep = -1;
    engine.haveMidiTick = false;
    engine.lastTickFrame = 0;
    engine.tickIntervalFrames = 0.0;
    engine.lastHostQuarter = startQuarter;
    engine.queue.clear();
    engine.activePitches.clear();
    prepareNotes(session, engine);
    engine.pendingClockStart = engine.settingsSendClock && engine.source != ClockSource::midi;
}

template <typename FrameFor>
void scheduleNotes(Session& session, Engine& engine, MidiBlock& out,
                   const double blockEnd, FrameFor frameFor)
{
    if (!engine.running || engine.playingRow < 0)
        return;
    const int length = genomeLength(session.settings);
    if (length <= 0)
        return;
    const std::uint8_t onStatus = static_cast<std::uint8_t>(0x90 | (session.settings.midiChannel - 1));

    while (true) {
        const double boundary = engine.startQuarter + engine.nextStep * kQuarterPerStep;
        if (boundary >= blockEnd - kEpsilon)
            break;
        const std::uint32_t frame = frameFor(boundary);
        if (!session.settings.loop && engine.nextStep >= length) {
            emitStop(session, engine, out, frame);
            return;
        }
        const int step = session.settings.loop
            ? static_cast<int>(engine.nextStep) % length
            : static_cast<int>(engine.nextStep);
        const auto& indices = engine.noteIndex[static_cast<std::size_t>(step)];
        for (const int noteIndex : indices) {
            const Note& note = engine.notes[static_cast<std::size_t>(noteIndex)];
            const int held = std::min(note.lengthSteps, length - step);
            const double offStep = engine.nextStep + static_cast<double>(held) * (1.0 - kGateGap);
            const double offQuarter = engine.startQuarter + offStep * kQuarterPerStep;
            out.push(frame, onStatus, static_cast<std::uint8_t>(note.midi), static_cast<std::uint8_t>(note.velocity));
            engine.lastNote = note.midi;
            engine.lastVelocity = note.velocity;
            if (std::find(engine.activePitches.begin(), engine.activePitches.end(), note.midi) == engine.activePitches.end())
                engine.activePitches.push_back(note.midi);
            queueEvent(engine, offQuarter, 0, static_cast<std::uint8_t>(0x80 | (session.settings.midiChannel - 1)),
                       static_cast<std::uint8_t>(note.midi), 0);
        }
        ++engine.nextStep;
        engine.positionSteps = engine.nextStep;
    }
}

template <typename FrameFor>
void scheduleClockOut(Engine& engine, MidiBlock& out, const double blockStart, const double blockEnd,
                      FrameFor frameFor)
{
    if (!engine.running || !engine.settingsSendClock || engine.source == ClockSource::midi)
        return;
    const double tickQuarter = 1.0 / static_cast<double>(kMidiClockPpq);
    const double first = std::ceil(blockStart / tickQuarter - kEpsilon) * tickQuarter;
    for (double quarter = first; quarter < blockEnd - kEpsilon; quarter += tickQuarter) {
        out.push(frameFor(quarter), {0xf8, 0, 0, 0}, 1);
        const int tick = static_cast<int>(std::floor(quarter * static_cast<double>(kMidiClockPpq) + kEpsilon));
        engine.clockTick = ((tick % kMidiClockPpq) + kMidiClockPpq) % kMidiClockPpq;
    }
}

void processInternal(Session& session, Engine& engine, MidiBlock& out,
                     const std::uint32_t frames, const double sampleRate)
{
    const double qpf = quartersPerFrame(session.settings.tempo, sampleRate);
    const double start = static_cast<double>(engine.frameCursor - engine.startFrame) * qpf;
    const double end = start + static_cast<double>(frames) * qpf;
    engine.effectiveBpm = session.settings.tempo;
    const auto frameFor = [&](const double quarter) {
        const double offset = (quarter - start) / qpf;
        return static_cast<std::uint32_t>(std::clamp(std::llround(offset), 0LL, static_cast<long long>(frames - 1)));
    };
    scheduleNotes(session, engine, out, end, frameFor);
    if (!engine.running)
        return;
    scheduleClockOut(engine, out, start, end, frameFor);
    flushQueue(engine, out, end, frameFor);
    engine.clockTick = static_cast<int>(std::floor(start * static_cast<double>(kMidiClockPpq))) % kMidiClockPpq;
}

void processHost(Session& session, Engine& engine, MidiBlock& out, const Transport& host,
                 const std::uint32_t frames, const double sampleRate)
{
    engine.hostBpm = finiteOr(host.bpm, 120.0);
    if (!host.valid || !host.playing) {
        if (engine.running)
            emitStop(session, engine, out, 0);
        engine.haveHostPosition = false;
        engine.hostArmed = true;
        return;
    }

    const double qpf = quartersPerFrame(engine.hostBpm, sampleRate);
    const double expectedAdvance = static_cast<double>(frames) * qpf;
    const double start = absoluteQuarter(host);
    const double advance = start - engine.lastHostQuarter;
    if (engine.running && engine.haveHostPosition
        && std::fabs(advance - expectedAdvance) > std::max(0.01, expectedAdvance * 0.05)) {
        emitStop(session, engine, out, 0);
        engine.hostArmed = true;
    }
    engine.haveHostPosition = true;
    engine.lastHostQuarter = start;

    if (engine.hostArmed && engine.selectedRow >= 0 && !engine.running) {
        engine.playingRow = engine.selectedRow;
        beginTimeline(session, engine, start);
        emitClockStart(engine, out, 0);
        engine.hostArmed = false;
    }
    if (!engine.running)
        return;

    const double end = start + static_cast<double>(frames) * qpf;
    engine.effectiveBpm = engine.hostBpm;
    const auto frameFor = [&](const double quarter) {
        const double offset = (quarter - start) / qpf;
        return static_cast<std::uint32_t>(std::clamp(std::llround(offset), 0LL, static_cast<long long>(frames - 1)));
    };
    scheduleNotes(session, engine, out, end, frameFor);
    if (!engine.running)
        return;
    scheduleClockOut(engine, out, start, end, frameFor);
    flushQueue(engine, out, end, frameFor);
    engine.clockTick = static_cast<int>(std::floor(start * static_cast<double>(kMidiClockPpq))) % kMidiClockPpq;
}

void processMidiTick(Session& session, Engine& engine, MidiBlock& out,
                     const MidiEvent& event, const std::uint32_t frames)
{
    if (!engine.running || engine.playingRow < 0)
        return;
    if (engine.tickIntervalFrames <= 0.0)
        return;
    if (engine.haveMidiTick && event.frame > engine.lastTickFrame) {
        const double measured = static_cast<double>(event.frame - engine.lastTickFrame);
        if (measured > 0.0)
            engine.tickIntervalFrames = measured;
    }
    engine.lastTickFrame = event.frame;
    engine.haveMidiTick = true;
    ++engine.midiTickCount;
    engine.clockTick = static_cast<int>(engine.midiTickCount % static_cast<std::uint32_t>(kMidiClockPpq));

    const double currentTick = static_cast<double>(engine.midiTickCount);
    const int currentStep = static_cast<int>(std::floor(currentTick / kTicksPerStep + kEpsilon));
    const int length = genomeLength(session.settings);
    const std::uint8_t onStatus = static_cast<std::uint8_t>(0x90 | (session.settings.midiChannel - 1));

    while (engine.lastScheduledStep + 1 <= currentStep) {
        const int absoluteStep = engine.lastScheduledStep + 1;
        const double stepTick = static_cast<double>(absoluteStep) * kTicksPerStep;
        if (!session.settings.loop && absoluteStep >= length) {
            const std::uint32_t frame = static_cast<std::uint32_t>(std::clamp(
                std::llround(static_cast<double>(event.frame) - (currentTick - stepTick) * engine.tickIntervalFrames),
                0LL, static_cast<long long>(frames - 1)));
            emitStop(session, engine, out, frame);
            return;
        }
        const double stepFrame = static_cast<double>(event.frame) - (currentTick - stepTick) * engine.tickIntervalFrames;
        const std::uint32_t frame = static_cast<std::uint32_t>(std::clamp(std::llround(stepFrame), 0LL,
                                                                           static_cast<long long>(frames - 1)));
        const int step = session.settings.loop ? absoluteStep % length : absoluteStep;
        const auto& indices = engine.noteIndex[static_cast<std::size_t>(step)];
        for (const int noteIndex : indices) {
            const Note& note = engine.notes[static_cast<std::size_t>(noteIndex)];
            const int held = std::min(note.lengthSteps, length - step);
            const double offTick = stepTick + static_cast<double>(held) * kTicksPerStep * (1.0 - kGateGap);
            out.push(frame, onStatus, static_cast<std::uint8_t>(note.midi), static_cast<std::uint8_t>(note.velocity));
            engine.lastNote = note.midi;
            engine.lastVelocity = note.velocity;
            if (std::find(engine.activePitches.begin(), engine.activePitches.end(), note.midi) == engine.activePitches.end())
                engine.activePitches.push_back(note.midi);
            queueEvent(engine, offTick, 0, static_cast<std::uint8_t>(0x80 | (session.settings.midiChannel - 1)),
                       static_cast<std::uint8_t>(note.midi), 0);
        }
        engine.lastScheduledStep = absoluteStep;
    }

    const auto frameFor = [&](const double tick) {
        const double offset = (currentTick - tick) * engine.tickIntervalFrames;
        const double result = static_cast<double>(event.frame) - offset;
        return static_cast<std::uint32_t>(std::clamp(std::llround(result), 0LL, static_cast<long long>(frames - 1)));
    };
    flushQueue(engine, out, currentTick, frameFor);
    engine.positionSteps = currentTick / kTicksPerStep;
}

void processInput(Session& session, Engine& engine, MidiBlock& out,
                  const MidiEvent* events, const std::uint32_t count,
                  const std::uint32_t frames, const double sampleRate)
{
    if (events == nullptr || count == 0 || engine.source != ClockSource::midi)
        return;

    std::array<MidiEvent, 512> sorted {};
    const std::uint32_t n = std::min(count, static_cast<std::uint32_t>(sorted.size()));
    for (std::uint32_t i = 0; i < n; ++i)
        sorted[static_cast<std::size_t>(i)] = events[i];
    std::stable_sort(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(n),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.frame < b.frame; });

    for (std::uint32_t i = 0; i < n; ++i) {
        MidiEvent event = sorted[static_cast<std::size_t>(i)];
        if (event.size == 0)
            continue;
        event.frame = std::min(event.frame, frames - 1);
        const std::uint8_t status = event.data[0];
        const double nominal = nominalTickFrames(session.settings.tempo, sampleRate);

        if (status == 0xfa) {
            if (engine.running)
                emitStop(session, engine, out, event.frame);
            engine.playingRow = engine.selectedRow;
            beginTimeline(session, engine, 0.0);
            engine.pendingClockStart = false;
            engine.tickIntervalFrames = nominal;
            engine.lastTickFrame = event.frame;
        } else if (status == 0xfb) {
            if (!engine.running && engine.selectedRow >= 0) {
                engine.playingRow = engine.selectedRow;
                engine.running = true;
                engine.pendingClockStart = false;
                engine.haveMidiTick = false;
                engine.tickIntervalFrames = nominal;
                engine.lastTickFrame = event.frame;
                prepareNotes(session, engine);
            }
        } else if (status == 0xfc) {
            if (engine.running)
                emitStop(session, engine, out, event.frame);
        } else if (status == 0xf8) {
            if (!engine.haveMidiTick && engine.tickIntervalFrames <= 0.0)
                engine.tickIntervalFrames = nominal;
            const double before = engine.effectiveBpm;
            processMidiTick(session, engine, out, event, frames);
            if (engine.haveMidiTick && engine.tickIntervalFrames > 0.0)
                engine.effectiveBpm = 2.5 * sampleRate / engine.tickIntervalFrames;
            else
                engine.effectiveBpm = before;
        }
    }
}

std::string hexEncode(const std::vector<std::uint8_t>& bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(bytes.size() * 2);
    for (const std::uint8_t byte : bytes) {
        text.push_back(kHex[(byte >> 4) & 0xf]);
        text.push_back(kHex[byte & 0xf]);
    }
    return text;
}

bool hexDecode(const std::string_view text, std::vector<std::uint8_t>& out)
{
    if (text.size() % 2 != 0)
        return false;
    out.clear();
    out.reserve(text.size() / 2);
    const auto nibble = [](const char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int hi = nibble(text[i]);
        const int lo = nibble(text[i + 1]);
        if (hi < 0 || lo < 0)
            return false;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

template <typename T>
bool parseNumber(const std::string_view text, T& value)
{
    if (text.empty())
        return false;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    return result.ec == std::errc() && result.ptr == text.data() + text.size();
}

bool parseDouble(const std::string_view text, double& value)
{
    if (text.empty())
        return false;
    std::string copy(text);
    char* end = nullptr;
    const double parsed = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size())
        return false;
    value = parsed;
    return std::isfinite(parsed);
}

std::vector<std::string_view> split(const std::string_view text, const char separator)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return parts;
}

bool readGenome(const std::vector<std::string_view>& parts, Genome& genome)
{
    if (parts.size() < 4)
        return false;
    genome.id = std::string(parts[0]);
    if (!hexDecode(parts[1], genome.kind)
        || !hexDecode(parts[2], genome.degree)
        || !hexDecode(parts[3], genome.velocity))
        return false;
    const std::size_t length = genome.kind.size();
    if (length == 0 || length > 4096 || genome.degree.size() != length || genome.velocity.size() != length)
        return false;
    return true;
}

}  // namespace

// ---- RNG -----------------------------------------------------------------------------------------------------------
double Rng::next() noexcept
{
    state = state + 0x6d2b79f5u;
    std::uint32_t t = state;
    t = static_cast<std::uint32_t>(static_cast<std::uint64_t>(t ^ (t >> 15)) * static_cast<std::uint64_t>(t | 1));
    t ^= t + static_cast<std::uint32_t>(static_cast<std::uint64_t>(t ^ (t >> 7)) * static_cast<std::uint64_t>(t | 61));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

int Rng::intBelow(const int n) noexcept
{
    return n > 0 ? static_cast<int>(next() * static_cast<double>(n)) : 0;
}

int Rng::range(const int lo, const int hi) noexcept
{
    return hi <= lo ? lo : lo + intBelow(hi - lo + 1);
}

bool Rng::chance(const double probability) noexcept
{
    return next() < probability;
}

int Rng::weightedIndex(const std::vector<double>& weights) noexcept
{
    double total = 0.0;
    for (const double weight : weights)
        total += weight;
    if (!(total > 0.0))
        return 0;
    double target = next() * total;
    for (std::size_t i = 0; i < weights.size(); ++i) {
        target -= weights[i];
        if (target < 0.0)
            return static_cast<int>(i);
    }
    return static_cast<int>(weights.size()) - 1;
}

std::vector<std::pair<int, double>> Rng::weighted(const std::vector<std::pair<int, double>>& table) noexcept
{
    if (table.empty())
        return {};
    std::vector<double> weights;
    weights.reserve(table.size());
    for (const auto& item : table)
        weights.push_back(item.second);
    const int index = weightedIndex(weights);
    return {table[static_cast<std::size_t>(clampInt(index, 0, static_cast<int>(table.size()) - 1))]};
}

std::vector<int> Rng::shuffled(const std::vector<int>& items) noexcept
{
    std::vector<int> out = items;
    for (int i = static_cast<int>(out.size()) - 1; i > 0; --i) {
        const int j = intBelow(i + 1);
        std::swap(out[static_cast<std::size_t>(i)], out[static_cast<std::size_t>(j)]);
    }
    return out;
}

Rng Rng::forked() noexcept
{
    return Rng(static_cast<std::uint32_t>(next() * 4294967296.0));
}

// ---- Settings ------------------------------------------------------------------------------------------------------
Settings defaultSettings()
{
    return Settings {};
}

Settings validateSettings(const Settings& input)
{
    Settings out;
    out.mode = input.mode == Mode::bass ? Mode::bass : Mode::melody;
    out.root = clampInt(static_cast<int>(std::lround(finiteOr(input.root, 0.0))), 0, 11);
    out.scale = static_cast<ScaleId>(clampInt(static_cast<int>(input.scale), 0, static_cast<int>(ScaleId::count) - 1));
    out.octave = clampInt(static_cast<int>(std::lround(finiteOr(input.octave, defaultOctave(out.mode)))), 0, 7);
    out.bars = clampInt(static_cast<int>(std::lround(finiteOr(input.bars, 4.0))), 1, 32);
    out.timeSignature = clampInt(static_cast<int>(std::lround(finiteOr(input.timeSignature, 0.0))), 0, kTimeSignatureCount - 1);
    out.tempo = static_cast<double>(clampInt(static_cast<int>(std::lround(finiteOr(input.tempo, 120.0))), 40, 240));
    out.crossoverRate = std::clamp(finiteOr(input.crossoverRate, 0.7), 0.0, 1.0);
    out.crossoverRate = std::round(out.crossoverRate * 100.0) / 100.0;
    out.mutationRate = std::clamp(finiteOr(input.mutationRate, 0.1), 0.0, 1.0);
    out.mutationRate = std::round(out.mutationRate * 100.0) / 100.0;
    out.populationSize = clampInt(static_cast<int>(std::lround(finiteOr(input.populationSize, 20.0))), 10, 50);
    out.loop = input.loop;
    out.clockSource = static_cast<ClockSource>(clampInt(static_cast<int>(input.clockSource), 0, 2));
    out.midiChannel = clampInt(static_cast<int>(std::lround(finiteOr(input.midiChannel, 1.0))), 1, 16);
    out.sendClock = input.sendClock;
    return out;
}

// ---- Scales and timing ---------------------------------------------------------------------------------------------
const char* scaleName(const ScaleId scale) noexcept { return kScaleNames[scaleIndex(scale)]; }
const char* scaleSymbol(const ScaleId scale) noexcept { return kScaleSymbols[scaleIndex(scale)]; }
const char* rootName(const int root) noexcept { return kRootNames[clampInt(root, 0, 11)]; }
const char* modeName(const Mode mode) noexcept { return mode == Mode::bass ? "Bass" : "Melody"; }

const char* clockSourceName(const ClockSource source) noexcept
{
    switch (source) {
    case ClockSource::midi: return "MIDI Clock";
    case ClockSource::host: return "Host BBT";
    default: return "Internal";
    }
}

const char* timeSignatureName(const int index) noexcept
{
    return kTimeSignatureNames[clampInt(index, 0, kTimeSignatureCount - 1)];
}

std::vector<int> scaleSteps(const ScaleId scale)
{
    const int index = scaleIndex(scale);
    const int count = kDegreeCounts[static_cast<std::size_t>(index)];
    const auto& row = kScaleSteps[static_cast<std::size_t>(index)];
    return std::vector<int>(row.begin(), row.begin() + count);
}

int degreeRange(const ScaleId scale, const Mode mode) noexcept
{
    const int count = kDegreeCounts[static_cast<std::size_t>(scaleIndex(scale))];
    const double span = mode == Mode::bass ? 1.5 : 2.0;
    return static_cast<int>(std::ceil(static_cast<double>(count) * span));
}

int degreeToMidi(const int degree, const Settings& settings) noexcept
{
    const auto steps = scaleSteps(settings.scale);
    const int count = static_cast<int>(steps.size());
    const int octaveOffset = degree / count;
    const int within = degree - octaveOffset * count;
    const int midi = 12 * (settings.octave + 1) + settings.root + 12 * octaveOffset + steps[static_cast<std::size_t>(within)];
    return clampInt(midi, 0, 127);
}

bool isChordTone(const int degree, const ScaleId scale) noexcept
{
    const int count = kDegreeCounts[static_cast<std::size_t>(scaleIndex(scale))];
    if (count < 5)
        return degree % count == 0;
    const int within = ((degree % count) + count) % count;
    return within == 0 || within == 2 || within == 4;
}

bool parseTimeSignature(const int index, int& numerator, int& denominator) noexcept
{
    if (index < 0 || index >= kTimeSignatureCount)
        return false;
    numerator = kNumerators[static_cast<std::size_t>(index)];
    denominator = kDenominators[static_cast<std::size_t>(index)];
    return true;
}

int stepsPerBar(const int timeSignature) noexcept
{
    int numerator = 4, denominator = 4;
    if (!parseTimeSignature(timeSignature, numerator, denominator))
        return 16;
    return numerator * 16 / denominator;
}

int stepsPerBeat(const int timeSignature) noexcept
{
    int numerator = 4, denominator = 4;
    if (!parseTimeSignature(timeSignature, numerator, denominator))
        return 4;
    if (denominator == 8 && numerator >= 6 && numerator % 3 == 0)
        return 6;
    return 16 / denominator;
}

double stepSeconds(const double tempo) noexcept
{
    return 60.0 / std::max(1.0, tempo) / 4.0;
}

int stepStrength(const int stepInBar, const int timeSignature) noexcept
{
    if (stepInBar == 0)
        return 3;
    if (stepInBar % stepsPerBeat(timeSignature) == 0)
        return 2;
    return stepInBar % 2 == 0 ? 1 : 0;
}

int genomeLength(const Settings& settings) noexcept
{
    return settings.bars * stepsPerBar(settings.timeSignature);
}

Context makeContext(const Settings& settings, Rng& rng, const GaParams& overrides)
{
    Context ctx;
    ctx.rng = &rng;
    ctx.settings = validateSettings(settings);
    ctx.params = overrides;
    ctx.params.N = ctx.settings.populationSize;
    ctx.perBar = stepsPerBar(ctx.settings.timeSignature);
    ctx.beatSteps = stepsPerBeat(ctx.settings.timeSignature);
    ctx.length = ctx.settings.bars * ctx.perBar;
    ctx.D = degreeRange(ctx.settings.scale, ctx.settings.mode);
    return ctx;
}

// ---- Genome and GA -----------------------------------------------------------------------------------------------
Genome createGenome(const int length, std::string id, std::vector<std::string> parents, std::string origin)
{
    Genome genome;
    genome.id = std::move(id);
    genome.kind.assign(static_cast<std::size_t>(std::max(0, length)), static_cast<std::uint8_t>(StepKind::rest));
    genome.degree.assign(genome.kind.size(), 0);
    genome.velocity.assign(genome.kind.size(), 0);
    genome.parents = std::move(parents);
    genome.origin = std::move(origin);
    return genome;
}

Genome cloneGenome(const Genome& genome)
{
    return genome;
}

bool sameMusic(const Genome& a, const Genome& b) noexcept
{
    if (a.kind.size() != b.kind.size())
        return false;
    for (std::size_t i = 0; i < a.kind.size(); ++i) {
        if (a.kind[i] != b.kind[i])
            return false;
        if (a.kind[i] == static_cast<std::uint8_t>(StepKind::note)
            && (a.degree[i] != b.degree[i] || a.velocity[i] != b.velocity[i]))
            return false;
    }
    return true;
}

void copySteps(Genome& target, const int to, const Genome& source, const int from, const int count)
{
    for (int i = 0; i < count; ++i) {
        const int src = from + i;
        const int dst = to + i;
        if (src < 0 || src >= static_cast<int>(source.kind.size())
            || dst < 0 || dst >= static_cast<int>(target.kind.size()))
            continue;
        target.kind[static_cast<std::size_t>(dst)] = source.kind[static_cast<std::size_t>(src)];
        target.degree[static_cast<std::size_t>(dst)] = source.degree[static_cast<std::size_t>(src)];
        target.velocity[static_cast<std::size_t>(dst)] = source.velocity[static_cast<std::size_t>(src)];
    }
}

Genome resizeGenome(const Genome& genome, const int bars, const int perBar)
{
    const int length = bars * perBar;
    if (length == static_cast<int>(genome.kind.size()))
        return cloneGenome(genome);
    Genome out = createGenome(length, genome.id, genome.parents, genome.origin);
    const int have = perBar > 0 ? static_cast<int>(genome.kind.size()) / perBar : 0;
    if (have == 0)
        return out;
    for (int bar = 0; bar < bars; ++bar)
        copySteps(out, bar * perBar, genome, (bar % have) * perBar, perBar);
    return out;
}

namespace {

int reflectValue(int value, const int D) noexcept
{
    if (value < 0) value = -value;
    if (value > D - 1) value = 2 * (D - 1) - value;
    return clampInt(value, 0, D - 1);
}

std::vector<int> chordTones(const Context& ctx)
{
    std::vector<int> tones;
    for (int d = 0; d < ctx.D; ++d)
        if (isChordTone(d, ctx.settings.scale)) tones.push_back(d);
    return tones;
}

int startDegree(Context& ctx)
{
    const auto tones = chordTones(ctx);
    const double limit = std::ceil(static_cast<double>(ctx.D) * 0.7);
    std::vector<int> low;
    for (const int tone : tones)
        if (static_cast<double>(tone) <= limit) low.push_back(tone);
    if (low.empty())
        return 0;
    return ctx.rng->pick(low);
}

int nextDegree(const int degree, Context& ctx)
{
    const int direction = ctx.rng->chance(0.5) ? 1 : -1;
    if (ctx.settings.mode == Mode::bass) {
        const double r = ctx.rng->next();
        if (r < 0.5)
            return ctx.rng->pick(chordTones(ctx));
        const int size = r < 0.8 ? 1 : ctx.rng->range(2, 4);
        return reflectValue(degree + direction * size, ctx.D);
    }
    const int leap = ctx.rng->range(3, 5);
    const auto choice = ctx.rng->weighted({{1, 0.6}, {2, 0.25}, {leap, 0.15}});
    const int size = choice.empty() ? 1 : choice[0].first;
    return reflectValue(degree + direction * size, ctx.D);
}

Genome generateSection(Context& ctx, const int nBars)
{
    Genome section = createGenome(nBars * ctx.perBar);
    int degree = startDegree(ctx);
    const double melodyOnset[] = {0.28, 0.5, 0.75, 0.95};
    const double bassOnset[] = {0.08, 0.3, 0.6, 0.95};
    const bool bass = ctx.settings.mode == Mode::bass;
    bool sounding = false;
    for (int i = 0; i < static_cast<int>(section.kind.size()); ++i) {
        const int strength = stepStrength(i % ctx.perBar, ctx.settings.timeSignature);
        const double onset = bass ? bassOnset[strength] : melodyOnset[strength];
        if (ctx.rng->chance(onset)) {
            degree = nextDegree(degree, ctx);
            section.kind[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(StepKind::note);
            section.degree[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(degree);
            section.velocity[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
                strength >= 2 ? ctx.rng->range(2, 3) : ctx.rng->range(1, 2));
            sounding = true;
        } else if (sounding && ctx.rng->chance(bass ? 0.7 : 0.6)) {
            section.kind[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(StepKind::hold);
            section.degree[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(degree);
        } else {
            section.kind[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(StepKind::rest);
            sounding = false;
        }
    }
    return section;
}

int nearestNote(const Genome& genome, const int index) noexcept
{
    if (index >= 0 && index < static_cast<int>(genome.kind.size())
        && genome.kind[static_cast<std::size_t>(index)] == static_cast<std::uint8_t>(StepKind::note))
        return index;
    for (int j = index - 1; j >= 0; --j)
        if (genome.kind[static_cast<std::size_t>(j)] == static_cast<std::uint8_t>(StepKind::note)) return j;
    for (int j = index + 1; j < static_cast<int>(genome.kind.size()); ++j)
        if (genome.kind[static_cast<std::size_t>(j)] == static_cast<std::uint8_t>(StepKind::note)) return j;
    return -1;
}

bool nudgeNote(Genome& genome, const int index, const int delta, const int D) noexcept
{
    const int n = nearestNote(genome, index);
    if (n < 0)
        return false;
    auto& degree = genome.degree[static_cast<std::size_t>(n)];
    degree = static_cast<std::uint8_t>(clampInt(static_cast<int>(degree) + delta, 0, D - 1));
    return true;
}

bool transposeRange(Genome& genome, const int from, const int count, int delta, const int D) noexcept
{
    int lo = std::numeric_limits<int>::max();
    int hi = -1;
    for (int i = from; i < from + count; ++i) {
        if (i < 0 || i >= static_cast<int>(genome.kind.size())
            || genome.kind[static_cast<std::size_t>(i)] != static_cast<std::uint8_t>(StepKind::note))
            continue;
        const int degree = genome.degree[static_cast<std::size_t>(i)];
        lo = std::min(lo, degree);
        hi = std::max(hi, degree);
    }
    if (hi < 0)
        return false;
    delta = clampInt(delta, -lo, D - 1 - hi);
    if (delta == 0)
        return false;
    for (int i = std::max(0, from); i < std::min(static_cast<int>(genome.kind.size()), from + count); ++i) {
        auto& degree = genome.degree[static_cast<std::size_t>(i)];
        degree = static_cast<std::uint8_t>(clampInt(static_cast<int>(degree) + delta, 0, D - 1));
    }
    return true;
}

void vary(Genome& genome, const int from, const int count, Context& ctx)
{
    if (ctx.rng->chance(0.5)) {
        nudgeNote(genome, from + ctx.rng->intBelow(count), ctx.rng->chance(0.5) ? 1 : -1, ctx.D);
    } else {
        transposeRange(genome, from, count, (ctx.rng->chance(0.5) ? 1 : -1) * ctx.rng->range(1, 2), ctx.D);
    }
}

void addCadence(Genome& genome, Context& ctx)
{
    const int length = static_cast<int>(genome.kind.size());
    const int barStart = length - ctx.perBar;
    int last = -1;
    for (int i = length - 1; i >= barStart; --i) {
        if (genome.kind[static_cast<std::size_t>(i)] == static_cast<std::uint8_t>(StepKind::note)) {
            last = i;
            break;
        }
    }
    if (last < 0) {
        last = std::max(0, barStart);
        genome.kind[static_cast<std::size_t>(last)] = static_cast<std::uint8_t>(StepKind::note);
    }
    std::vector<int> tones;
    for (const int tone : {0, 2, 4})
        if (tone < ctx.D) tones.push_back(tone);
    genome.degree[static_cast<std::size_t>(last)] = static_cast<std::uint8_t>(tones.empty() ? 0 : ctx.rng->pick(tones));
    genome.velocity[static_cast<std::size_t>(last)] = 2;
    for (int i = last + 1; i < length; ++i)
        genome.kind[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(StepKind::hold);
}

int sign(Rng& rng) noexcept { return rng.chance(0.5) ? 1 : -1; }

void toggleRest(Genome& genome, const int i) noexcept
{
    if (i < 0 || i >= static_cast<int>(genome.kind.size())) return;
    auto& kind = genome.kind[static_cast<std::size_t>(i)];
    kind = kind == static_cast<std::uint8_t>(StepKind::rest)
        ? static_cast<std::uint8_t>(StepKind::note) : static_cast<std::uint8_t>(StepKind::rest);
}

void toggleOnset(Genome& genome, const int i, Context& ctx)
{
    if (i < 0 || i >= static_cast<int>(genome.kind.size())) return;
    auto& kind = genome.kind[static_cast<std::size_t>(i)];
    if (kind == static_cast<std::uint8_t>(StepKind::note)) {
        kind = static_cast<std::uint8_t>(StepKind::hold);
    } else if (kind == static_cast<std::uint8_t>(StepKind::hold)) {
        const int owner = nearestNote(genome, i);
        const int base = owner >= 0 && owner < i
            ? genome.degree[static_cast<std::size_t>(owner)] : ctx.rng->intBelow(ctx.D);
        kind = static_cast<std::uint8_t>(StepKind::note);
        genome.degree[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(clampInt(base + ctx.rng->range(-2, 2), 0, ctx.D - 1));
        genome.velocity[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(ctx.rng->range(0, 2));
    } else {
        toggleRest(genome, i);
    }
}

void changeVelocity(Genome& genome, const int i, Context& ctx)
{
    const int n = nearestNote(genome, i);
    if (n >= 0) {
        auto& velocity = genome.velocity[static_cast<std::size_t>(n)];
        velocity = static_cast<std::uint8_t>(clampInt(static_cast<int>(velocity) + sign(*ctx.rng), 0, 3));
    }
}

void pointOperator(const int op, Genome& genome, const int i, Context& ctx)
{
    switch (op) {
    case 0: nudgeNote(genome, i, sign(*ctx.rng), ctx.D); break;
    case 1: nudgeNote(genome, i, sign(*ctx.rng) * ctx.rng->range(2, 5), ctx.D); break;
    case 2: toggleOnset(genome, i, ctx); break;
    case 3: toggleRest(genome, i); break;
    case 4: changeVelocity(genome, i, ctx); break;
    default: break;
    }
}

int nearestNoteIn(const Genome& genome, const int start, const int count) noexcept
{
    for (int i = start; i < start + count; ++i)
        if (i >= 0 && i < static_cast<int>(genome.kind.size())
            && genome.kind[static_cast<std::size_t>(i)] == static_cast<std::uint8_t>(StepKind::note))
            return i;
    return -1;
}

void copyBar(Genome& genome, const int bar, Context& ctx)
{
    if (ctx.settings.bars < 2) return;
    int from = ctx.rng->intBelow(ctx.settings.bars - 1);
    if (from >= bar) ++from;
    copySteps(genome, bar * ctx.perBar, genome, from * ctx.perBar, ctx.perBar);
}

void invertBar(Genome& genome, const int bar, Context& ctx)
{
    const int start = bar * ctx.perBar;
    const int first = nearestNoteIn(genome, start, ctx.perBar);
    if (first < 0) return;
    const int axis = genome.degree[static_cast<std::size_t>(first)];
    for (int i = start; i < start + ctx.perBar && i < static_cast<int>(genome.kind.size()); ++i) {
        auto& degree = genome.degree[static_cast<std::size_t>(i)];
        degree = static_cast<std::uint8_t>(clampInt(2 * axis - static_cast<int>(degree), 0, ctx.D - 1));
    }
}

void retrogradeBar(Genome& genome, const int bar, Context& ctx)
{
    const int start = bar * ctx.perBar;
    std::vector<int> slots;
    for (int i = start; i < start + ctx.perBar && i < static_cast<int>(genome.kind.size()); ++i)
        if (genome.kind[static_cast<std::size_t>(i)] == static_cast<std::uint8_t>(StepKind::note)) slots.push_back(i);
    std::vector<int> degrees, velocities;
    for (const int slot : slots) {
        degrees.push_back(genome.degree[static_cast<std::size_t>(slot)]);
        velocities.push_back(genome.velocity[static_cast<std::size_t>(slot)]);
    }
    std::reverse(degrees.begin(), degrees.end());
    std::reverse(velocities.begin(), velocities.end());
    for (std::size_t k = 0; k < slots.size(); ++k) {
        genome.degree[static_cast<std::size_t>(slots[k])] = static_cast<std::uint8_t>(degrees[k]);
        genome.velocity[static_cast<std::size_t>(slots[k])] = static_cast<std::uint8_t>(velocities[k]);
    }
}

void shiftBar(Genome& genome, const int bar, Context& ctx)
{
    const int start = bar * ctx.perBar;
    const int n = ctx.perBar;
    const int by = sign(*ctx.rng) * ctx.rng->range(1, 2);
    std::vector<std::uint8_t> kind(n), degree(n), velocity(n);
    for (int i = 0; i < n; ++i) {
        const int index = start + i;
        if (index >= static_cast<int>(genome.kind.size())) break;
        kind[static_cast<std::size_t>(i)] = genome.kind[static_cast<std::size_t>(index)];
        degree[static_cast<std::size_t>(i)] = genome.degree[static_cast<std::size_t>(index)];
        velocity[static_cast<std::size_t>(i)] = genome.velocity[static_cast<std::size_t>(index)];
    }
    for (int i = 0; i < n; ++i) {
        const int to = start + ((i + by + n) % n);
        if (to < 0 || to >= static_cast<int>(genome.kind.size())) continue;
        genome.kind[static_cast<std::size_t>(to)] = kind[static_cast<std::size_t>(i)];
        genome.degree[static_cast<std::size_t>(to)] = degree[static_cast<std::size_t>(i)];
        genome.velocity[static_cast<std::size_t>(to)] = velocity[static_cast<std::size_t>(i)];
    }
}

void segmentOperator(const int op, Genome& genome, const int bar, Context& ctx)
{
    switch (op) {
    case 0: transposeRange(genome, bar * ctx.perBar, ctx.perBar, sign(*ctx.rng) * ctx.rng->range(1, 3), ctx.D); break;
    case 1: copyBar(genome, bar, ctx); break;
    case 2: invertBar(genome, bar, ctx); break;
    case 3: retrogradeBar(genome, bar, ctx); break;
    case 4: shiftBar(genome, bar, ctx); break;
    default: break;
    }
}

}  // namespace

Genome generateGenome(Context& ctx, const std::string& id, const std::string& origin)
{
    Genome genome = createGenome(ctx.length, id, {}, origin);
    const int motifBars = std::min(ctx.settings.bars, ctx.rng->chance(0.5) ? 1 : 2);
    static constexpr const char* kForms[] = {"AAAA", "AABA", "ABAB", "AABB", "ABAC"};
    const std::string form = ctx.rng->pick(std::vector<std::string>(std::begin(kForms), std::end(kForms)));

    std::vector<char> letters;
    for (std::size_t i = 0; i < form.size(); ++i) {
        const char letter = form[i];
        if (std::find(letters.begin(), letters.end(), letter) == letters.end()) letters.push_back(letter);
    }
    std::vector<Genome> sections;
    sections.reserve(letters.size());
    for (std::size_t i = 0; i < letters.size(); ++i)
        sections.push_back(generateSection(ctx, motifBars));

    std::vector<char> seen;
    const int sectionCount = (ctx.settings.bars + motifBars - 1) / motifBars;
    for (int s = 0; s < sectionCount; ++s) {
        const char letter = form[static_cast<std::size_t>(s % static_cast<int>(form.size()))];
        const int firstBar = s * motifBars;
        const int count = std::min(motifBars, ctx.settings.bars - firstBar);
        const auto it = std::find(letters.begin(), letters.end(), letter);
        const Genome& source = sections[static_cast<std::size_t>(std::distance(letters.begin(), it))];
        copySteps(genome, firstBar * ctx.perBar, source, 0, count * ctx.perBar);
        if (std::find(seen.begin(), seen.end(), letter) != seen.end() && ctx.rng->chance(0.5))
            vary(genome, firstBar * ctx.perBar, count * ctx.perBar, ctx);
        seen.push_back(letter);
    }
    if (ctx.rng->chance(0.7))
        addCadence(genome, ctx);
    return genome;
}

std::vector<std::int16_t> pitchTrack(const Genome& genome)
{
    std::vector<std::int16_t> track(genome.kind.size(), -1);
    int current = -1;
    for (std::size_t i = 0; i < genome.kind.size(); ++i) {
        const auto kind = static_cast<StepKind>(genome.kind[i]);
        if (kind == StepKind::note)
            current = genome.degree[i];
        else if (kind != StepKind::hold)
            current = -1;
        track[i] = static_cast<std::int16_t>(current);
    }
    return track;
}

namespace {

double onsetDistance(const Genome& a, const Genome& b, const int length) noexcept
{
    int differing = 0;
    for (int i = 0; i < length; ++i) {
        const bool an = a.kind[static_cast<std::size_t>(i)] == static_cast<std::uint8_t>(StepKind::note);
        const bool bn = b.kind[static_cast<std::size_t>(i)] == static_cast<std::uint8_t>(StepKind::note);
        if (an != bn) ++differing;
    }
    return length > 0 ? static_cast<double>(differing) / static_cast<double>(length) : 0.0;
}

double pitchDistance(const std::vector<std::int16_t>& a, const std::vector<std::int16_t>& b,
                     const int length, const int D) noexcept
{
    double sum = 0.0;
    int both = 0;
    bool anyA = false, anyB = false;
    for (int i = 0; i < length; ++i) {
        const int pa = a[static_cast<std::size_t>(i)];
        const int pb = b[static_cast<std::size_t>(i)];
        if (pa >= 0) anyA = true;
        if (pb >= 0) anyB = true;
        if (pa >= 0 && pb >= 0) {
            sum += std::fabs(pa - pb) / static_cast<double>(std::max(1, D));
            ++both;
        }
    }
    if (both == 0)
        return (anyA || anyB) ? 1.0 : 0.0;
    return std::min(1.0, sum / static_cast<double>(both));
}

std::vector<int> notePitches(const Genome& genome)
{
    std::vector<int> out;
    for (std::size_t i = 0; i < genome.kind.size(); ++i)
        if (genome.kind[i] == static_cast<std::uint8_t>(StepKind::note)) out.push_back(genome.degree[i]);
    return out;
}

std::vector<int> contour(const std::vector<int>& pitches)
{
    constexpr int kPoints = 16;
    std::vector<int> sampled;
    sampled.reserve(kPoints);
    const int n = static_cast<int>(pitches.size());
    for (int i = 0; i < kPoints; ++i) {
        const int index = static_cast<int>(std::lround(static_cast<double>(i) * static_cast<double>(n - 1)
                                                       / static_cast<double>(kPoints - 1)));
        sampled.push_back(pitches[static_cast<std::size_t>(clampInt(index, 0, n - 1))]);
    }
    std::vector<int> directions;
    directions.reserve(kPoints - 1);
    for (int i = 1; i < kPoints; ++i)
        directions.push_back((sampled[static_cast<std::size_t>(i)] > sampled[static_cast<std::size_t>(i - 1)])
            - (sampled[static_cast<std::size_t>(i)] < sampled[static_cast<std::size_t>(i - 1)]));
    return directions;
}

double contourDistance(const Genome& a, const Genome& b) noexcept
{
    const auto pa = notePitches(a);
    const auto pb = notePitches(b);
    if (pa.size() < 2 && pb.size() < 2) return 0.0;
    if (pa.size() < 2 || pb.size() < 2) return 1.0;
    const auto ca = contour(pa);
    const auto cb = contour(pb);
    int differing = 0;
    for (std::size_t i = 0; i < ca.size(); ++i)
        if (ca[i] != cb[i]) ++differing;
    return static_cast<double>(differing) / static_cast<double>(ca.size());
}

}  // namespace

double distance(const Genome& a, const Genome& b, const Context& ctx) noexcept
{
    const int length = std::min(a.kind.size(), b.kind.size());
    const auto& w = ctx.params.distanceWeights;
    return w[0] * onsetDistance(a, b, length)
        + w[1] * pitchDistance(pitchTrack(a), pitchTrack(b), length, ctx.D)
        + w[2] * contourDistance(a, b);
}

Estimate estimateFitness(const Genome& genome, const std::vector<Evidence>& evidence,
                         const double prior, const Context& ctx)
{
    double weightSum = 0.0, weighted = 0.0;
    for (const Evidence& item : evidence) {
        if (item.genome == nullptr || item.genome->id == genome.id)
            continue;
        const double d = distance(genome, *item.genome, ctx) / ctx.params.sigma;
        const double weight = std::exp(-d * d) * std::pow(ctx.params.gamma, static_cast<double>(item.age));
        weightSum += weight;
        weighted += weight * item.rating;
    }
    const double denominator = weightSum + ctx.params.lambda;
    return {(weighted + ctx.params.lambda * prior) / denominator, ctx.params.lambda / denominator};
}

void addToArchive(std::vector<ArchiveEntry>& archive, const Genome& genome, const double rating,
                  const int generation, const int max) noexcept
{
    archive.erase(std::remove_if(archive.begin(), archive.end(), [&](const ArchiveEntry& entry) {
                      return entry.genome.id == genome.id;
                  }), archive.end());
    archive.push_back({genome, rating, generation});
    if (static_cast<int>(archive.size()) > max)
        archive.erase(archive.begin(), archive.begin() + (static_cast<int>(archive.size()) - max));
}

bool hasRating(const Entry& entry) noexcept
{
    return entry.rating > -1.5;
}

bool isCandidate(const Entry& entry) noexcept
{
    return !entry.shown && !(entry.carried && hasRating(entry));
}

std::vector<Evidence> buildEvidence(const std::vector<Entry>& current,
                                    const std::vector<ArchiveEntry>& archive, const int generation)
{
    std::vector<Evidence> evidence;
    std::vector<std::string> seen;
    for (const Entry& entry : current) {
        if (!hasRating(entry)) continue;
        seen.push_back(entry.genome.id);
        evidence.push_back({&entry.genome, entry.rating, 0});
    }
    for (const ArchiveEntry& entry : archive) {
        if (std::find(seen.begin(), seen.end(), entry.genome.id) != seen.end()) continue;
        evidence.push_back({&entry.genome, entry.rating, std::max(0, generation - entry.generation)});
    }
    return evidence;
}

std::vector<int> eligible(const std::vector<Entry>& entries)
{
    // Melogen excludes only a -1 vote; unrated entries are still parent candidates.
    std::vector<int> out;
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (std::fabs(entries[i].rating + 1.0) > 0.5) out.push_back(static_cast<int>(i));
    return out;
}

std::vector<int> pickParents(const std::vector<int>& pool, const std::vector<double>& fitness,
                             Rng& rng, const int size)
{
    const auto tournament = [&]() {
        std::vector<int> drawn;
        for (int k = 0; k < size && !pool.empty(); ++k)
            drawn.push_back(rng.pick(pool));
        if (drawn.empty()) return 0;
        double best = -std::numeric_limits<double>::infinity();
        for (const int index : drawn) best = std::max(best, fitness[static_cast<std::size_t>(index)]);
        std::vector<int> winners;
        for (const int index : drawn)
            if (fitness[static_cast<std::size_t>(index)] == best) winners.push_back(index);
        return rng.pick(winners);
    };
    const int a = tournament();
    int b = tournament();
    for (int tries = 0; b == a && pool.size() > 1 && tries < 8; ++tries)
        b = tournament();
    return {a, b};
}

std::vector<int> chooseToShow(const std::vector<Candidate>& candidates, const int count, const Context& ctx)
{
    std::vector<Candidate> remaining = candidates;
    std::vector<int> chosen;
    while (static_cast<int>(chosen.size()) < count && !remaining.empty()) {
        std::size_t bestAt = 0;
        double bestScore = -std::numeric_limits<double>::infinity();
        for (std::size_t at = 0; at < remaining.size(); ++at) {
            const Candidate& candidate = remaining[at];
            double score = candidate.estimate.f + ctx.params.kappa * candidate.estimate.u;
            if (!chosen.empty() && candidate.genome != nullptr) {
                double nearest = std::numeric_limits<double>::infinity();
                for (const int otherIndex : chosen) {
                    for (const Candidate& other : candidates) {
                        if (other.index == otherIndex && other.genome != nullptr)
                            nearest = std::min(nearest, distance(*candidate.genome, *other.genome, ctx));
                    }
                }
                score += ctx.params.beta * nearest;
            }
            if (score > bestScore) {
                bestScore = score;
                bestAt = at;
            }
        }
        chosen.push_back(remaining[bestAt].index);
        remaining.erase(remaining.begin() + static_cast<std::ptrdiff_t>(bestAt));
    }
    return chosen;
}

namespace {

int barCount(const Context& ctx) noexcept
{
    return ctx.perBar > 0 ? ctx.length / ctx.perBar : 0;
}

Genome fromSegments(const Genome& a, const Genome& b,
                    const std::vector<std::array<int, 3>>& segments)
{
    Genome child = createGenome(static_cast<int>(a.kind.size()));
    for (const auto& segment : segments)
        copySteps(child, segment[0], segment[2] == 0 ? a : b, segment[0], segment[1] - segment[0]);
    return child;
}

Genome barOnePoint(const Genome& a, const Genome& b, Context& ctx)
{
    const int n = barCount(ctx);
    if (n < 2)
        return fromSegments(a, b, {{0, static_cast<int>(a.kind.size()), 0}});
    const int cut = ctx.rng->range(1, n - 1) * ctx.perBar;
    return fromSegments(a, b, {{0, cut, 0}, {cut, static_cast<int>(a.kind.size()), 1}});
}

Genome barTwoPoint(const Genome& a, const Genome& b, Context& ctx)
{
    const int n = barCount(ctx);
    if (n < 3) return barOnePoint(a, b, ctx);
    const int first = ctx.rng->range(1, n - 2);
    const int second = ctx.rng->range(first + 1, n - 1);
    return fromSegments(a, b, {{0, first * ctx.perBar, 0},
                               {first * ctx.perBar, second * ctx.perBar, 1},
                               {second * ctx.perBar, static_cast<int>(a.kind.size()), 0}});
}

Genome barUniform(const Genome& a, const Genome& b, Context& ctx)
{
    std::vector<std::array<int, 3>> segments;
    const int n = barCount(ctx);
    for (int bar = 0; bar < n; ++bar)
        segments.push_back({bar * ctx.perBar, (bar + 1) * ctx.perBar, ctx.rng->chance(0.5) ? 0 : 1});
    return fromSegments(a, b, segments);
}

Genome beatOnePoint(const Genome& a, const Genome& b, Context& ctx)
{
    const int beats = ctx.beatSteps > 0 ? static_cast<int>(a.kind.size()) / ctx.beatSteps : 0;
    if (beats < 2) return barOnePoint(a, b, ctx);
    const int cut = ctx.rng->range(1, beats - 1) * ctx.beatSteps;
    return fromSegments(a, b, {{0, cut, 0}, {cut, static_cast<int>(a.kind.size()), 1}});
}

Entry newEntry(Genome genome, const double rating = -2.0, const bool carried = false, const double prior = 0.0)
{
    Entry entry;
    entry.genome = std::move(genome);
    entry.rating = rating;
    entry.voted = hasRating(entry);
    entry.carried = carried;
    entry.prior = prior;
    return entry;
}

std::vector<int> chooseElites(const std::vector<Entry>& entries, const std::vector<double>& fitness, Context& ctx)
{
    std::vector<int> liked;
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (entries[i].rating > 0.5) liked.push_back(static_cast<int>(i));
    if (!liked.empty()) {
        auto shuffled = ctx.rng->shuffled(liked);
        if (static_cast<int>(shuffled.size()) > ctx.params.E)
            shuffled.resize(static_cast<std::size_t>(ctx.params.E));
        return shuffled;
    }
    int best = -1;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!hasRating(entries[i]) && (best < 0 || fitness[i] > fitness[static_cast<std::size_t>(best)]))
            best = static_cast<int>(i);
    }
    if (best >= 0 && fitness[static_cast<std::size_t>(best)] > ctx.params.eliteEstimateMin)
        return {best};
    return {};
}

bool duplicates(const Genome& genome, const std::vector<Entry>& among)
{
    return std::any_of(among.begin(), among.end(), [&](const Entry& entry) { return sameMusic(entry.genome, genome); });
}

}  // namespace

Genome crossover(const Genome& a, const Genome& b, Context& ctx)
{
    const auto& w = ctx.params.crossoverWeights;
    const int op = ctx.rng->weightedIndex({w[0], w[1], w[2], w[3]});
    Genome child;
    switch (op) {
    case 0: child = barOnePoint(a, b, ctx); break;
    case 1: child = barTwoPoint(a, b, ctx); break;
    case 2: child = barUniform(a, b, ctx); break;
    default: child = beatOnePoint(a, b, ctx); break;
    }
    child.parents = {a.id, b.id};
    child.origin = "crossover";
    return child;
}

Genome mutate(const Genome& genome, const double rate, Context& ctx)
{
    Genome child = cloneGenome(genome);
    if (rate <= 0.0)
        return child;
    const int bars = ctx.perBar > 0 ? static_cast<int>(child.kind.size()) / ctx.perBar : 0;
    const double segmentRate = rate * ctx.params.segmentRateFactor;
    for (int bar = 0; bar < bars; ++bar) {
        if (ctx.rng->chance(segmentRate)) {
            const int op = ctx.rng->weightedIndex({0.30, 0.25, 0.15, 0.10, 0.20});
            segmentOperator(op, child, bar, ctx);
        }
    }
    const double pointRate = rate * ctx.params.pointRateFactor;
    for (int i = 0; i < static_cast<int>(child.kind.size()); ++i) {
        if (ctx.rng->chance(pointRate)) {
            const int op = ctx.rng->weightedIndex({0.35, 0.10, 0.20, 0.15, 0.20});
            pointOperator(op, child, i, ctx);
        }
    }
    return child;
}

std::vector<Entry> evolveGeneration(const std::vector<Entry>& entries, const std::vector<double>& fitness,
                                    const int generation, Context& ctx)
{
    const int N = ctx.params.N;
    std::vector<Entry> out;
    const auto label = [&]() { return "g" + std::to_string(generation) + "-" + std::to_string(out.size()); };

    auto elites = chooseElites(entries, fitness, ctx);
    if (static_cast<int>(elites.size()) > N) elites.resize(static_cast<std::size_t>(N));
    for (const int index : elites) {
        const Entry& source = entries[static_cast<std::size_t>(index)];
        Genome genome = cloneGenome(source.genome);
        genome.origin = "elite";
        const bool carried = source.rating > 0.5;
        out.push_back(newEntry(std::move(genome), carried ? 1.0 : -2.0, carried, fitness[static_cast<std::size_t>(index)]));
    }

    const std::vector<int> pool = eligible(entries);
    int immigrants = std::max(1, static_cast<int>(std::lround(ctx.params.immigrantFraction * static_cast<double>(N))));
    if (pool.size() < 2) immigrants = std::max(0, N - static_cast<int>(out.size()));
    const int offspring = std::max(0, N - static_cast<int>(out.size()) - immigrants);
    for (int k = 0; k < offspring; ++k) {
        const auto parents = pickParents(pool, fitness, *ctx.rng, ctx.params.tournament);
        const int ia = parents[0], ib = parents[1];
        Genome child = ctx.rng->chance(ctx.settings.crossoverRate)
            ? crossover(entries[static_cast<std::size_t>(ia)].genome, entries[static_cast<std::size_t>(ib)].genome, ctx)
            : cloneGenome(entries[static_cast<std::size_t>(ia)].genome);
        if (child.origin != "crossover") {
            child.parents = {entries[static_cast<std::size_t>(ia)].genome.id};
            child.origin = "mutation";
        }
        child = mutate(child, ctx.settings.mutationRate, ctx);
        for (int tries = 0; tries < ctx.params.dedupTries && duplicates(child, out); ++tries)
            child = mutate(child, std::max(ctx.settings.mutationRate, 0.1), ctx);
        child.id = label();
        const double prior = child.origin == "crossover"
            ? (fitness[static_cast<std::size_t>(ia)] + fitness[static_cast<std::size_t>(ib)]) * 0.5
            : fitness[static_cast<std::size_t>(ia)];
        out.push_back(newEntry(std::move(child), -2.0, false, prior));
    }
    while (static_cast<int>(out.size()) < N)
        out.push_back(newEntry(generateGenome(ctx, label(), "immigrant")));
    return out;
}

// ---- Population -----------------------------------------------------------------------------------------------
Population Population::create(Context& ctx)
{
    Population pop;
    for (int i = 0; i < ctx.params.N; ++i) {
        const std::string id = "g0-" + std::to_string(i);
        pop.entries.push_back(newEntry(generateGenome(ctx, id)));
    }
    pop.showNext(ctx);
    return pop;
}

void Population::rate(const int index, const double rating) noexcept
{
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return;
    Entry& entry = entries[static_cast<std::size_t>(index)];
    entry.rating = rating;
    entry.voted = hasRating(entry);
    entry.carried = false;
}

int Population::ratedCount() const noexcept
{
    int count = 0;
    for (const Entry& entry : entries)
        if (hasRating(entry) && !entry.carried) ++count;
    return count;
}

bool Population::canEvolve() const noexcept
{
    return std::any_of(entries.begin(), entries.end(), [](const Entry& entry) { return entry.voted; });
}

bool Population::canShowMore() const noexcept
{
    return std::any_of(entries.begin(), entries.end(), isCandidate);
}

std::vector<Evidence> Population::evidence() const
{
    return buildEvidence(entries, archive, generation);
}

std::vector<Estimate> Population::estimates(const Context& ctx) const
{
    const auto evidence = this->evidence();
    std::vector<Estimate> out;
    out.reserve(entries.size());
    for (const Entry& entry : entries) {
        if (hasRating(entry))
            out.push_back({entry.rating, 0.0});
        else
            out.push_back(estimateFitness(entry.genome, evidence, entry.prior, ctx));
    }
    return out;
}

std::vector<int> Population::showNext(Context& ctx)
{
    const auto estimates = this->estimates(ctx);
    std::vector<int> picked;
    if (round == 0) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].carried && !entries[i].shown) {
                picked.push_back(static_cast<int>(i));
                entries[i].shown = true;
                break;
            }
        }
    }
    std::vector<Candidate> candidates;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (!isCandidate(entries[i])) continue;
        candidates.push_back({static_cast<int>(i), &entries[i].genome, estimates[i]});
    }
    for (const int index : chooseToShow(candidates, ctx.params.K - static_cast<int>(picked.size()), ctx)) {
        picked.push_back(index);
        entries[static_cast<std::size_t>(index)].shown = true;
    }
    rows = ctx.rng->shuffled(picked);
    if (!rows.empty()) ++round;
    return rows;
}

std::vector<int> Population::evolve(Context& ctx)
{
    for (const Entry& entry : entries)
        if (hasRating(entry))
            addToArchive(archive, entry.genome, entry.rating, generation, ctx.params.archiveMax);
    const auto estimates = this->estimates(ctx);
    std::vector<double> fitness;
    fitness.reserve(estimates.size());
    for (const auto& estimate : estimates) fitness.push_back(estimate.f);
    ++generation;
    entries = evolveGeneration(entries, fitness, generation, ctx);
    rows.clear();
    round = 0;
    return showNext(ctx);
}

void Population::resize(const int bars, const int perBar)
{
    for (Entry& entry : entries)
        entry.genome = resizeGenome(entry.genome, bars, perBar);
    for (ArchiveEntry& entry : archive)
        entry.genome = resizeGenome(entry.genome, bars, perBar);
}

// ---- Decoding and MIDI -------------------------------------------------------------------------------------------
std::vector<Note> decode(const Genome& genome, const Settings& settings)
{
    std::vector<Note> notes;
    const int maxDegree = degreeRange(settings.scale, settings.mode) - 1;
    bool haveOpen = false;
    Note open;
    for (std::size_t i = 0; i < genome.kind.size(); ++i) {
        const auto kind = static_cast<StepKind>(genome.kind[i]);
        if (kind == StepKind::note) {
            const int degree = std::min(static_cast<int>(genome.degree[i]), maxDegree);
            open = {static_cast<int>(i), 1, degreeToMidi(degree, settings),
                    kVelocityLevels[clampInt(static_cast<int>(genome.velocity[i]), 0, 3)]};
            notes.push_back(open);
            haveOpen = true;
        } else if (kind == StepKind::hold && haveOpen) {
            ++open.lengthSteps;
            notes.back().lengthSteps = open.lengthSteps;
        } else {
            haveOpen = false;
        }
    }
    return notes;
}

void MidiBlock::push(const std::uint32_t frame, const std::uint8_t status,
                     const std::uint8_t d1, const std::uint8_t d2) noexcept
{
    push(frame, {status, d1, d2, 0}, 3);
}

void MidiBlock::push(const std::uint32_t frame, const std::array<std::uint8_t, 4>& bytes,
                     const std::uint8_t size) noexcept
{
    if (count >= events.size())
        return;
    events[static_cast<std::size_t>(count++)] = {frame, size, bytes};
}

void MidiBlock::sort() noexcept
{
    std::stable_sort(events.begin(), events.begin() + static_cast<std::ptrdiff_t>(count),
                     [](const MidiEvent& a, const MidiEvent& b) {
                         if (a.frame != b.frame) return a.frame < b.frame;
                         const int orderA = (a.data[0] & 0xf0) == 0x80 ? 0 : 1;
                         const int orderB = (b.data[0] & 0xf0) == 0x80 ? 0 : 1;
                         return orderA < orderB;
                     });
}

// ---- Actions and settings ---------------------------------------------------------------------------------------
void stopPlayback(Session& session, Engine& engine)
{
    engine.pendingStop = true;
    engine.pendingPlay = -1;
    (void)session;
}

void requestPlay(Session& session, Engine& engine, const int position)
{
    (void)session;
    if (position < 0 || position >= kRows)
        return;
    if (engine.pendingPlay == position) {
        engine.pendingPlay = -1;
        return;
    }
    if (engine.running && engine.playingRow == position) {
        engine.pendingPlay = -1;
        engine.pendingStop = true;
        return;
    }
    engine.pendingStop = false;
    engine.pendingPlay = position;
}

ParsedAction parseAction(const std::string_view text)
{
    const auto parts = split(text, ':');
    ParsedAction action;
    if (parts.empty())
        return action;
    if (parts[0] == "play" && parts.size() >= 2) {
        action.type = Action::play;
        parseNumber(parts[1], action.position);
    } else if (parts[0] == "stop") {
        action.type = Action::stop;
    } else if (parts[0] == "vote" && parts.size() >= 3) {
        action.type = Action::vote;
        parseNumber(parts[1], action.position);
        parseNumber(parts[2], action.rating);
    } else if (parts[0] == "more") {
        action.type = Action::more;
    } else if (parts[0] == "evolve") {
        action.type = Action::evolve;
    } else if (parts[0] == "new") {
        action.type = Action::newPopulation;
    } else if (parts[0] == "reset") {
        action.type = Action::resetAll;
    }
    return action;
}

void applyAction(Session& session, Engine& engine, const ParsedAction& action)
{
    switch (action.type) {
    case Action::play:
        requestPlay(session, engine, action.position);
        break;
    case Action::stop:
        stopPlayback(session, engine);
        break;
    case Action::vote: {
        if (action.position < 0 || action.position >= kRows
            || action.position >= static_cast<int>(session.population.rows.size()))
            break;
        const int index = session.population.rows[static_cast<std::size_t>(action.position)];
        const double current = session.population.entries[static_cast<std::size_t>(index)].rating;
        session.population.rate(index, std::fabs(current - static_cast<double>(action.rating)) < 0.5 ? -2.0 : action.rating);
        break;
    }
    case Action::more:
        if (session.population.canShowMore()) {
            stopPlayback(session, engine);
            Context ctx = makeContext(session.settings, session.rng);
            session.population.showNext(ctx);
        }
        break;
    case Action::evolve:
        if (session.population.canEvolve()) {
            stopPlayback(session, engine);
            Context ctx = makeContext(session.settings, session.rng);
            session.population.evolve(ctx);
        }
        break;
    case Action::newPopulation: {
        stopPlayback(session, engine);
        engine.selectedRow = -1;
        Context ctx = makeContext(session.settings, session.rng);
        session.population = Population::create(ctx);
        break;
    }
    case Action::resetAll: {
        stopPlayback(session, engine);
        engine.selectedRow = -1;
        session.settings = defaultSettings();
        session.rng = Rng(1);
        Context ctx = makeContext(session.settings, session.rng);
        session.population = Population::create(ctx);
        engine.source = ClockSource::internal;
        break;
    }
    default:
        break;
    }
}

void applySettings(Session& session, Engine& engine, const Settings& next)
{
    const Settings previous = session.settings;
    Settings validated = validateSettings(next);
    if (previous.mode != validated.mode && validated.octave == defaultOctave(previous.mode))
        validated.octave = defaultOctave(validated.mode);

    const bool timeSignatureChanged = previous.timeSignature != validated.timeSignature;
    const bool resized = previous.bars != validated.bars && !timeSignatureChanged;
    const bool affectsPlayback = previous.mode != validated.mode
        || previous.root != validated.root || previous.scale != validated.scale
        || previous.octave != validated.octave || previous.bars != validated.bars
        || previous.timeSignature != validated.timeSignature
        || std::fabs(previous.tempo - validated.tempo) > 0.5
        || previous.clockSource != validated.clockSource
        || previous.loop != validated.loop;
    if (affectsPlayback)
        stopPlayback(session, engine);
    session.settings = validated;
    engine.source = validated.clockSource;
    engine.settingsSendClock = validated.sendClock;

    Context ctx = makeContext(session.settings, session.rng);
    if (timeSignatureChanged) {
        session.population = Population::create(ctx);
    } else if (resized) {
        session.population.resize(validated.bars, stepsPerBar(validated.timeSignature));
    }
    if (!session.population.entries.empty()
        && static_cast<int>(session.population.entries[0].genome.kind.size()) != genomeLength(validated))
        session.population = Population::create(ctx);
}

// ---- Transport process -------------------------------------------------------------------------------------------
MidiBlock process(Session& session, Engine& engine, const Transport& host,
                  const MidiEvent* events, const std::uint32_t eventCount,
                  const std::uint32_t frames, const double sampleRate)
{
    MidiBlock out;
    if (frames == 0)
        return out;
    engine.source = session.settings.clockSource;
    engine.settingsSendClock = session.settings.sendClock;
    engine.effectiveBpm = session.settings.clockSource == ClockSource::host && host.valid
        ? host.bpm : engine.effectiveBpm;

    if (engine.pendingStop) {
        if (engine.running)
            emitStop(session, engine, out, 0);
        else
            finishPlayback(session, engine);
        engine.pendingStop = false;
    }

    if (engine.pendingPlay >= 0 && engine.pendingPlay < kRows) {
        session.playingRow = engine.pendingPlay;
        engine.playingRow = engine.pendingPlay;
        engine.selectedRow = engine.pendingPlay;
        engine.pendingPlay = -1;
        if (engine.source == ClockSource::midi) {
            engine.running = false;
            engine.pendingClockStart = false;
            engine.midiTickCount = 0;
            engine.lastScheduledStep = -1;
            engine.haveMidiTick = false;
            engine.tickIntervalFrames = 0.0;
            prepareNotes(session, engine);
        } else if (engine.source == ClockSource::host) {
            if (host.valid && host.playing) {
                beginTimeline(session, engine, absoluteQuarter(host));
                emitClockStart(engine, out, 0);
                engine.hostArmed = false;
            } else {
                engine.running = false;
                engine.pendingClockStart = false;
                engine.hostArmed = true;
                prepareNotes(session, engine);
            }
        } else {
            beginTimeline(session, engine, 0.0);
            emitClockStart(engine, out, 0);
        }
    }

    processInput(session, engine, out, events, eventCount, frames, sampleRate);

    if (engine.source == ClockSource::internal)
        processInternal(session, engine, out, frames, sampleRate);
    else if (engine.source == ClockSource::host)
        processHost(session, engine, out, host, frames, sampleRate);

    engine.frameCursor += frames;
    out.sort();
    return out;
}

StatusView statusView(const Session& session, const Engine& engine)
{
    StatusView view;
    view.playing = engine.playingRow;
    view.generation = session.population.generation;
    view.round = session.population.round;
    view.totalRounds = std::max(1, (static_cast<int>(session.population.entries.size()) + kRows - 1) / kRows);
    view.rated = session.population.ratedCount();
    view.populationSize = static_cast<int>(session.population.entries.size());
    view.canMore = session.population.canShowMore();
    view.canEvolve = session.population.canEvolve();
    view.duration = static_cast<double>(genomeLength(session.settings)) * stepSeconds(session.settings.tempo);
    view.clockSource = session.settings.clockSource;
    view.clockBpm = session.settings.clockSource == ClockSource::midi ? engine.effectiveBpm
        : session.settings.clockSource == ClockSource::host ? engine.hostBpm : session.settings.tempo;
    view.clockRunning = engine.running;
    view.clockTick = engine.clockTick;
    view.lastNote = engine.lastNote;
    view.lastVelocity = engine.lastVelocity;
    view.channel = session.settings.midiChannel;
    view.sendClock = session.settings.sendClock;
    const int length = genomeLength(session.settings);
    view.progress = engine.running && length > 0
        ? std::clamp(engine.positionSteps / static_cast<double>(length), 0.0, 1.0) : 0.0;
    for (int position = 0; position < kRows; ++position) {
        RowView row;
        row.position = position;
        row.playing = engine.playingRow == position && engine.running;
        if (position < static_cast<int>(session.population.rows.size())) {
            const int index = session.population.rows[static_cast<std::size_t>(position)];
            if (index >= 0 && index < static_cast<int>(session.population.entries.size())) {
                const Entry& entry = session.population.entries[static_cast<std::size_t>(index)];
                row.id = entry.genome.id;
                row.origin = entry.genome.origin;
                row.carried = entry.carried;
                row.rating = static_cast<int>(std::lround(entry.rating));
            }
        }
        view.rows[static_cast<std::size_t>(position)] = row;
    }
    return view;
}

std::string serializeStatus(const StatusView& view)
{
    std::string text = "version=1\n";
    const auto number = [&text](const char* key, const auto& value) {
        text += key;
        text += '=';
        text += std::to_string(value);
        text += '\n';
    };
    number("playing", view.playing);
    number("generation", view.generation);
    number("round", view.round);
    number("totalRounds", view.totalRounds);
    number("rated", view.rated);
    number("population", view.populationSize);
    number("canMore", view.canMore ? 1 : 0);
    number("canEvolve", view.canEvolve ? 1 : 0);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", view.duration);
    text += "duration=";
    text += buffer;
    text += '\n';
    number("clock", static_cast<int>(view.clockSource));
    std::snprintf(buffer, sizeof(buffer), "%.2f", view.clockBpm);
    text += "bpm=";
    text += buffer;
    text += '\n';
    number("running", view.clockRunning ? 1 : 0);
    number("tick", view.clockTick);
    number("note", view.lastNote);
    number("velocity", view.lastVelocity);
    std::snprintf(buffer, sizeof(buffer), "%.4f", view.progress);
    text += "progress=";
    text += buffer;
    text += '\n';
    number("channel", view.channel);
    number("sendClock", view.sendClock ? 1 : 0);
    for (const RowView& row : view.rows) {
        text += "row=" + std::to_string(row.position) + '|' + row.id + '|' + row.origin + '|'
            + std::to_string(row.rating) + '|' + (row.carried ? "1" : "0") + '|' + (row.playing ? "1" : "0") + '\n';
    }
    return text;
}

// ---- State text --------------------------------------------------------------------------------------------------
std::string serializeSession(const Session& session)
{
    const Settings& s = session.settings;
    char settingsBuffer[256];
    std::snprintf(settingsBuffer, sizeof(settingsBuffer),
                  "%d,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%d,%d,%d,%d,%d",
                  static_cast<int>(s.mode), s.root, static_cast<int>(s.scale), s.octave, s.bars,
                  s.timeSignature, s.tempo, s.crossoverRate, s.mutationRate, s.populationSize,
                  s.loop ? 1 : 0, static_cast<int>(s.clockSource), s.midiChannel, s.sendClock ? 1 : 0);
    std::string text = "version=1\nsettings=" + std::string(settingsBuffer)
        + "\nrng=" + std::to_string(session.rng.state)
        + "\ngen=" + std::to_string(session.population.generation)
        + "\nround=" + std::to_string(session.population.round)
        + "\nrows=";
    for (std::size_t i = 0; i < session.population.rows.size(); ++i) {
        if (i > 0) text += ',';
        text += std::to_string(session.population.rows[i]);
    }
    text += '\n';
    const auto genomeFields = [](const Genome& genome) {
        return hexEncode(genome.kind) + '|' + hexEncode(genome.degree) + '|' + hexEncode(genome.velocity);
    };
    for (const Entry& entry : session.population.entries) {
        char prior[32];
        std::snprintf(prior, sizeof(prior), "%.6f", entry.prior);
        std::string parents;
        for (std::size_t i = 0; i < entry.genome.parents.size(); ++i) {
            if (i > 0) parents += ',';
            parents += entry.genome.parents[i];
        }
        text += "e=" + entry.genome.id + '|' + genomeFields(entry.genome) + '|'
            + std::to_string(static_cast<int>(std::lround(entry.rating))) + '|'
            + (entry.voted ? '1' : '0') + '|' + (entry.carried ? '1' : '0') + '|' + (entry.shown ? '1' : '0') + '|'
            + prior + '|' + parents + '|' + entry.genome.origin + '\n';
    }
    for (const ArchiveEntry& entry : session.population.archive) {
        text += "a=" + entry.genome.id + '|' + genomeFields(entry.genome) + '|'
            + std::to_string(static_cast<int>(std::lround(entry.rating))) + '|'
            + std::to_string(entry.generation) + '|' + entry.genome.origin + '\n';
    }
    return text;
}

std::optional<Session> deserializeSession(const std::string_view text)
{
    Session session;
    bool sawVersion = false, sawSettings = false, sawRng = false, sawGen = false, sawRound = false, sawRows = false;
    std::vector<int> rowIndices;
    std::vector<Entry> entries;
    std::vector<ArchiveEntry> archive;
    std::size_t expectedLength = 0;

    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, equals);
        const std::string_view value = line.substr(equals + 1);

        if (key == "version") {
            if (value != "1") return std::nullopt;
            sawVersion = true;
        } else if (key == "settings") {
            const auto fields = split(value, ',');
            if (fields.size() != 14) return std::nullopt;
            Settings parsed;
            int mode = 0, scale = 0, clockSource = 0, loop = 0, sendClock = 0;
            if (!parseNumber(fields[0], mode) || !parseNumber(fields[1], parsed.root)
                || !parseNumber(fields[2], scale) || !parseNumber(fields[3], parsed.octave)
                || !parseNumber(fields[4], parsed.bars) || !parseNumber(fields[5], parsed.timeSignature)
                || !parseDouble(fields[6], parsed.tempo) || !parseDouble(fields[7], parsed.crossoverRate)
                || !parseDouble(fields[8], parsed.mutationRate) || !parseNumber(fields[9], parsed.populationSize)
                || !parseNumber(fields[10], loop) || !parseNumber(fields[11], clockSource)
                || !parseNumber(fields[12], parsed.midiChannel) || !parseNumber(fields[13], sendClock))
                return std::nullopt;
            parsed.mode = mode == 1 ? Mode::bass : Mode::melody;
            parsed.scale = static_cast<ScaleId>(scale);
            parsed.clockSource = static_cast<ClockSource>(clockSource);
            parsed.loop = loop != 0;
            parsed.sendClock = sendClock != 0;
            session.settings = validateSettings(parsed);
            sawSettings = true;
        } else if (key == "rng") {
            unsigned long long parsed = 0;
            if (!parseNumber(value, parsed) || parsed > 0xffffffffull) return std::nullopt;
            session.rng.state = static_cast<std::uint32_t>(parsed);
            sawRng = true;
        } else if (key == "gen") {
            if (!parseNumber(value, session.population.generation)) return std::nullopt;
            sawGen = true;
        } else if (key == "round") {
            if (!parseNumber(value, session.population.round)) return std::nullopt;
            sawRound = true;
        } else if (key == "rows") {
            sawRows = true;
            if (value.empty()) continue;
            for (const std::string_view token : split(value, ',')) {
                int index = -1;
                if (!parseNumber(token, index)) return std::nullopt;
                rowIndices.push_back(index);
            }
        } else if (key == "e" || key == "a") {
            const auto parts = split(value, '|');
            Genome genome;
            if (!readGenome(parts, genome)) return std::nullopt;
            if (expectedLength == 0) expectedLength = genome.kind.size();
            if (genome.kind.size() != expectedLength) return std::nullopt;
            if (key == "e") {
                if (parts.size() < 11) return std::nullopt;
                Entry entry;
                entry.genome = std::move(genome);
                int voted = 0, carried = 0, shown = 0;
                if (!parseNumber(parts[4], entry.rating)
                    || !parseNumber(parts[5], voted)
                    || !parseNumber(parts[6], carried)
                    || !parseNumber(parts[7], shown)
                    || !parseDouble(parts[8], entry.prior))
                    return std::nullopt;
                entry.voted = voted != 0 || hasRating(entry);
                entry.carried = carried != 0;
                entry.shown = shown != 0;
                for (const std::string_view parent : split(parts[9], ','))
                    if (!parent.empty()) entry.genome.parents.emplace_back(parent);
                entry.genome.origin = std::string(parts[10]);
                entries.push_back(std::move(entry));
            } else {
                if (parts.size() < 7) return std::nullopt;
                ArchiveEntry entry;
                entry.genome = std::move(genome);
                if (!parseNumber(parts[4], entry.rating) || !parseNumber(parts[5], entry.generation))
                    return std::nullopt;
                entry.genome.origin = std::string(parts[6]);
                archive.push_back(std::move(entry));
            }
        } else {
            return std::nullopt;
        }
    }

    if (!sawVersion || !sawSettings || !sawRng || !sawGen || !sawRound || !sawRows || entries.empty())
        return std::nullopt;
    for (const int index : rowIndices)
        if (index < 0 || index >= static_cast<int>(entries.size())) return std::nullopt;
    session.population.entries = std::move(entries);
    session.population.archive = std::move(archive);
    session.population.rows = std::move(rowIndices);
    return session;
}

// ---- Host parameters ---------------------------------------------------------------------------------------------
const std::array<ParamSpec, kParameterCount> kParameterSpecs {{
    {"mode", "Line", 0, 1, 0, true},
    {"root", "Key", 0, 11, 0, true},
    {"scale", "Scale", 0, 11, 0, true},
    {"octave", "Octave", 0, 7, 4, true},
    {"bars", "Bars", 1, 32, 4, true},
    {"time_signature", "Time signature", 0, kTimeSignatureCount - 1, 0, true},
    {"tempo", "Tempo", 40, 240, 120, true},
    {"crossover_rate", "Crossover rate", 0, 1, 0.7f},
    {"mutation_rate", "Mutation rate", 0, 1, 0.1f},
    {"population_size", "Population", 10, 50, 20, true},
    {"loop", "Loop playback", 0, 1, 0, true},
    {"clock_source", "Clock source", 0, 2, 0, true},
    {"midi_channel", "MIDI channel", 1, 16, 1, true},
    {"send_clock", "Send MIDI clock", 0, 1, 0, true},
    {"status_playing", "Playing row", -1, kRows - 1, -1, true, true},
    {"status_generation", "Generation", 0, 9999, 0, true, true},
    {"status_round", "Round", 0, 99, 0, true, true},
    {"status_clock_bpm", "Clock BPM", 0, 300, 120, false, true},
    {"status_clock_tick", "Clock tick", 0, kMidiClockPpq - 1, 0, true, true},
    {"status_progress", "Playback progress", 0, 1, 0, false, true},
    {"status_clock_running", "Clock running", 0, 1, 0, true, true},
    {"status_last_note", "Last note", -1, 127, -1, true, true},
    {"status_last_velocity", "Last velocity", 0, 127, 0, true, true}
}};

Params defaultParams()
{
    return paramsFromSettings(defaultSettings());
}

Settings settingsFromParams(const Params& params)
{
    Settings settings;
    settings.mode = params[static_cast<std::size_t>(kMode)] > 0.5f ? Mode::bass : Mode::melody;
    settings.root = static_cast<int>(std::lround(params[static_cast<std::size_t>(kRoot)]));
    settings.scale = static_cast<ScaleId>(static_cast<int>(std::lround(params[static_cast<std::size_t>(kScale)])));
    settings.octave = static_cast<int>(std::lround(params[static_cast<std::size_t>(kOctave)]));
    settings.bars = static_cast<int>(std::lround(params[static_cast<std::size_t>(kBars)]));
    settings.timeSignature = static_cast<int>(std::lround(params[static_cast<std::size_t>(kTimeSignature)]));
    settings.tempo = params[static_cast<std::size_t>(kTempo)];
    settings.crossoverRate = params[static_cast<std::size_t>(kCrossoverRate)];
    settings.mutationRate = params[static_cast<std::size_t>(kMutationRate)];
    settings.populationSize = static_cast<int>(std::lround(params[static_cast<std::size_t>(kPopulationSize)]));
    settings.loop = params[static_cast<std::size_t>(kLoop)] > 0.5f;
    settings.clockSource = static_cast<ClockSource>(static_cast<int>(std::lround(params[static_cast<std::size_t>(kClockSource)])));
    settings.midiChannel = static_cast<int>(std::lround(params[static_cast<std::size_t>(kMidiChannel)]));
    settings.sendClock = params[static_cast<std::size_t>(kSendClock)] > 0.5f;
    return validateSettings(settings);
}

Params paramsFromSettings(const Settings& settings)
{
    const Settings s = validateSettings(settings);
    Params params {};
    params[static_cast<std::size_t>(kMode)] = s.mode == Mode::bass ? 1.0f : 0.0f;
    params[static_cast<std::size_t>(kRoot)] = static_cast<float>(s.root);
    params[static_cast<std::size_t>(kScale)] = static_cast<float>(static_cast<int>(s.scale));
    params[static_cast<std::size_t>(kOctave)] = static_cast<float>(s.octave);
    params[static_cast<std::size_t>(kBars)] = static_cast<float>(s.bars);
    params[static_cast<std::size_t>(kTimeSignature)] = static_cast<float>(s.timeSignature);
    params[static_cast<std::size_t>(kTempo)] = static_cast<float>(s.tempo);
    params[static_cast<std::size_t>(kCrossoverRate)] = static_cast<float>(s.crossoverRate);
    params[static_cast<std::size_t>(kMutationRate)] = static_cast<float>(s.mutationRate);
    params[static_cast<std::size_t>(kPopulationSize)] = static_cast<float>(s.populationSize);
    params[static_cast<std::size_t>(kLoop)] = s.loop ? 1.0f : 0.0f;
    params[static_cast<std::size_t>(kClockSource)] = static_cast<float>(static_cast<int>(s.clockSource));
    params[static_cast<std::size_t>(kMidiChannel)] = static_cast<float>(s.midiChannel);
    params[static_cast<std::size_t>(kSendClock)] = s.sendClock ? 1.0f : 0.0f;
    params[static_cast<std::size_t>(kStatusPlaying)] = -1.0f;
    params[static_cast<std::size_t>(kStatusGeneration)] = 0.0f;
    params[static_cast<std::size_t>(kStatusRound)] = 0.0f;
    params[static_cast<std::size_t>(kStatusClockBpm)] = static_cast<float>(s.tempo);
    params[static_cast<std::size_t>(kStatusClockTick)] = 0.0f;
    params[static_cast<std::size_t>(kStatusProgress)] = 0.0f;
    return params;
}

}  // namespace downspout::melevolve
