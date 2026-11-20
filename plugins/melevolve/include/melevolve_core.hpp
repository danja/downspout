#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace downspout::melevolve {

// Melogen's step grid: one step is a sixteenth note. The GA works on scale degrees;
// decode() is the only place that turns them into MIDI notes.
enum class StepKind : std::uint8_t { rest = 0, note = 1, hold = 2 };

enum class Mode : std::uint8_t { melody = 0, bass = 1 };

// Melogen's twelve scales, in the order shown by its Settings tab. This is a local
// subset of docs/scales.md, not a new canonical ordering.
enum class ScaleId : std::uint8_t {
    major = 0,
    minor = 1,
    dorian = 2,
    phrygian = 3,
    lydian = 4,
    mixolydian = 5,
    harmonicMinor = 6,
    majorPentatonic = 7,
    minorPentatonic = 8,
    blues = 9,
    wholeTone = 10,
    chromatic = 11,
    count = 12
};

enum class ClockSource : std::uint8_t { internal = 0, midi = 1, host = 2 };

inline constexpr int kRows = 5;
inline constexpr int kTimeSignatureCount = 5;
inline constexpr double kGateGap = 0.05;  // note-offs arrive this fraction of a step early
inline constexpr int kMidiClockPpq = 24;  // 24 ticks per quarter note

struct Genome {
    std::string id;
    std::vector<std::uint8_t> kind;
    std::vector<std::uint8_t> degree;
    std::vector<std::uint8_t> velocity;
    std::vector<std::string> parents;
    std::string origin = "random";
};

// mulberry32: every random choice in the GA goes through this generator, so a run
// reproduces exactly from its saved state.
struct Rng {
    std::uint32_t state = 1;

    explicit Rng(std::uint32_t seed = 1) noexcept : state(seed) {}

    double next() noexcept;
    int intBelow(int n) noexcept;
    int range(int lo, int hi) noexcept;
    bool chance(double probability) noexcept;

    template <typename T>
    T pick(const std::vector<T>& items) noexcept
    {
        if (items.empty())
            return T {};
        return items[static_cast<std::size_t>(next() * static_cast<double>(items.size()))];
    }

    int weightedIndex(const std::vector<double>& weights) noexcept;
    std::vector<std::pair<int, double>> weighted(const std::vector<std::pair<int, double>>& table) noexcept;
    std::vector<int> shuffled(const std::vector<int>& items) noexcept;
    Rng forked() noexcept;
};

struct GaParams {
    int N = 20;
    int K = kRows;
    int E = 2;
    double immigrantFraction = 0.1;
    int tournament = 3;
    double sigma = 0.18;
    double gamma = 0.7;
    double lambda = 0.5;
    double kappa = 0.6;
    double beta = 0.5;
    int archiveMax = 200;
    double eliteEstimateMin = 0.3;
    std::array<double, 3> distanceWeights {0.4, 0.35, 0.25};  // rhythm, pitch, contour
    double pointRateFactor = 0.25;
    double segmentRateFactor = 0.5;
    std::array<double, 4> crossoverWeights {0.4, 0.3, 0.2, 0.1};  // bar 1pt, 2pt, uniform, beat
    int dedupTries = 3;
};

struct Settings {
    Mode mode = Mode::melody;
    int root = 0;
    ScaleId scale = ScaleId::major;
    int octave = 4;
    int bars = 4;
    int timeSignature = 0;  // index into kTimeSignatureNames
    double tempo = 120.0;
    double crossoverRate = 0.7;
    double mutationRate = 0.1;
    int populationSize = 20;
    bool loop = false;
    ClockSource clockSource = ClockSource::internal;
    int midiChannel = 1;     // 1-16, converted to a zero-based status nibble at output
    bool sendClock = false;  // generate 24 PPQ clock when internal or host driven
};

Settings defaultSettings();
Settings validateSettings(const Settings& input);

struct Context {
    Rng* rng = nullptr;
    Settings settings {};
    GaParams params {};
    int perBar = 16;
    int beatSteps = 4;
    int length = 64;
    int D = 14;
};

Context makeContext(const Settings& settings, Rng& rng, const GaParams& overrides = {});

// ---- Scales and timing ----------------------------------------------------------------------------------------
const char* scaleName(ScaleId scale) noexcept;
const char* scaleSymbol(ScaleId scale) noexcept;
const char* rootName(int root) noexcept;
const char* modeName(Mode mode) noexcept;
const char* clockSourceName(ClockSource source) noexcept;
const char* timeSignatureName(int index) noexcept;
std::vector<int> scaleSteps(ScaleId scale);
int degreeRange(ScaleId scale, Mode mode) noexcept;
int degreeToMidi(int degree, const Settings& settings) noexcept;
bool isChordTone(int degree, ScaleId scale) noexcept;

bool parseTimeSignature(int index, int& numerator, int& denominator) noexcept;
int stepsPerBar(int timeSignature) noexcept;
int stepsPerBeat(int timeSignature) noexcept;
double stepSeconds(double tempo) noexcept;
int stepStrength(int stepInBar, int timeSignature) noexcept;
int genomeLength(const Settings& settings) noexcept;

// ---- Genome and GA --------------------------------------------------------------------------------------------
Genome createGenome(int length, std::string id = {}, std::vector<std::string> parents = {}, std::string origin = "random");
Genome cloneGenome(const Genome& genome);
bool sameMusic(const Genome& a, const Genome& b) noexcept;
void copySteps(Genome& target, int to, const Genome& source, int from, int count);
Genome resizeGenome(const Genome& genome, int bars, int perBar);
Genome generateGenome(Context& ctx, const std::string& id, const std::string& origin = "random");

std::vector<std::int16_t> pitchTrack(const Genome& genome);
double distance(const Genome& a, const Genome& b, const Context& ctx) noexcept;

struct Evidence {
    const Genome* genome = nullptr;
    double rating = 0.0;
    int age = 0;
};
struct Estimate {
    double f = 0.0;
    double u = 0.0;
};
Estimate estimateFitness(const Genome& genome, const std::vector<Evidence>& evidence, double prior, const Context& ctx);

struct ArchiveEntry {
    Genome genome;
    double rating = 0.0;
    int generation = 0;
};
void addToArchive(std::vector<ArchiveEntry>& archive, const Genome& genome, double rating, int generation, int max) noexcept;

struct Entry {
    Genome genome;
    double rating = -2.0;       // -2 unrated, -1 dislike, 0 neutral, 1 like
    bool voted = false;
    bool carried = false;
    bool shown = false;
    double prior = 0.0;
};
bool hasRating(const Entry& entry) noexcept;
bool isCandidate(const Entry& entry) noexcept;
std::vector<Evidence> buildEvidence(const std::vector<Entry>& current,
                                    const std::vector<ArchiveEntry>& archive, int generation);
std::vector<int> eligible(const std::vector<Entry>& entries);
std::vector<int> pickParents(const std::vector<int>& pool, const std::vector<double>& fitness,
                             Rng& rng, int size);

struct Candidate {
    int index = 0;
    const Genome* genome = nullptr;
    Estimate estimate {};
};
std::vector<int> chooseToShow(const std::vector<Candidate>& candidates, int count, const Context& ctx);

Genome crossover(const Genome& a, const Genome& b, Context& ctx);
Genome mutate(const Genome& genome, double rate, Context& ctx);
std::vector<Entry> evolveGeneration(const std::vector<Entry>& entries, const std::vector<double>& fitness,
                                    int generation, Context& ctx);

class Population {
public:
    int generation = 0;
    int round = 0;
    std::vector<Entry> entries;
    std::vector<ArchiveEntry> archive;
    std::vector<int> rows;

    static Population create(Context& ctx);
    void rate(int index, double rating) noexcept;
    int ratedCount() const noexcept;
    bool canEvolve() const noexcept;
    bool canShowMore() const noexcept;
    std::vector<Evidence> evidence() const;
    std::vector<Estimate> estimates(const Context& ctx) const;
    std::vector<int> showNext(Context& ctx);
    std::vector<int> evolve(Context& ctx);
    void resize(int bars, int perBar);
};

// ---- Decoding and MIDI ----------------------------------------------------------------------------------------
struct Note {
    int startStep = 0;
    int lengthSteps = 1;
    int midi = 60;
    int velocity = 80;
};
std::vector<Note> decode(const Genome& genome, const Settings& settings);

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 3;
    std::array<std::uint8_t, 4> data {};
};

struct MidiBlock {
    std::array<MidiEvent, 512> events {};
    std::uint32_t count = 0;
    void push(std::uint32_t frame, std::uint8_t status, std::uint8_t d1, std::uint8_t d2) noexcept;
    void push(std::uint32_t frame, const std::array<std::uint8_t, 4>& bytes, std::uint8_t size) noexcept;
    void sort() noexcept;
};

// ---- Runtime state, scheduling and transport ------------------------------------------------------------------
struct Transport {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;             // zero-based
    double barBeat = 0.0;         // zero-based, including fractional ticks
    double beatsPerBar = 4.0;
    double beatType = 4.0;
    double bpm = 120.0;
};

struct RowView {
    int position = 0;
    std::string id;
    std::string origin;
    int rating = -2;
    bool carried = false;
    bool playing = false;
};

struct StatusView {
    int playing = -1;
    int generation = 0;
    int round = 0;
    int totalRounds = 4;
    int rated = 0;
    int populationSize = 20;
    bool canMore = false;
    bool canEvolve = false;
    double duration = 8.0;
    ClockSource clockSource = ClockSource::internal;
    double clockBpm = 120.0;
    bool clockRunning = false;
    int clockTick = 0;
    int lastNote = -1;       // last note-on emitted by the processor, -1 before playback
    int lastVelocity = 0;
    double progress = 0.0;  // 0..1 through the current row
    int channel = 1;
    bool sendClock = false;
    std::array<RowView, kRows> rows {};
};

struct Session {
    Settings settings {};
    Rng rng {1};
    Population population;
    int playingRow = -1;  // runtime only, not saved with the session

    Session()
    {
        Context ctx = makeContext(settings, rng);
        population = Population::create(ctx);
    }
};

struct Engine {
    std::uint64_t frameCursor = 0;
    int pendingPlay = -1;
    bool pendingStop = false;

    int playingRow = -1;
    int selectedRow = -1;  // survives Stop so MIDI Continue / host restart know what to play
    bool running = false;
    ClockSource source = ClockSource::internal;
    bool settingsSendClock = false;
    double startQuarter = 0.0;
    std::uint64_t startFrame = 0;
    double nextStep = 0.0;         // absolute step index since playback began
    double positionSteps = 0.0;    // for the UI progress line
    double effectiveBpm = 120.0;
    double hostBpm = 120.0;
    int clockTick = 0;
    int lastNote = -1;
    int lastVelocity = 0;
    double lastHostQuarter = 0.0;
    bool haveHostPosition = false;
    bool hostArmed = true;  // start on the next host Transport play edge, not on every block

    std::uint32_t midiTickCount = 0;
    int lastScheduledStep = -1;
    std::uint32_t lastTickFrame = 0;
    double tickIntervalFrames = 0.0;
    bool haveMidiTick = false;

    bool pendingClockStart = false;

    struct QueueEvent {
        double time = 0.0;   // absolute quarters (internal/host) or absolute MIDI ticks
        int order = 0;       // 0 = off, 1 = on
        std::array<std::uint8_t, 4> data {};
        std::uint8_t size = 3;
    };
    std::vector<QueueEvent> queue;
    std::vector<int> activePitches;
    std::vector<Note> notes;
    std::vector<std::vector<int>> noteIndex;  // decoded note indices by start step
};

void stopPlayback(Session& session, Engine& engine);
void requestPlay(Session& session, Engine& engine, int position);

// Actions sent from the editor through the "action" state key.
enum class Action { vote, more, evolve, newPopulation, resetAll, play, stop, unknown };
struct ParsedAction {
    Action type = Action::unknown;
    int position = -1;
    int rating = 0;
};
ParsedAction parseAction(std::string_view text);
void applyAction(Session& session, Engine& engine, const ParsedAction& action);
void applySettings(Session& session, Engine& engine, const Settings& next);

// Process one audio block. `events` are incoming MIDI with block-relative frames.
MidiBlock process(Session& session, Engine& engine, const Transport& host,
                  const MidiEvent* events, std::uint32_t eventCount,
                  std::uint32_t frames, double sampleRate);

StatusView statusView(const Session& session, const Engine& engine);
std::string serializeStatus(const StatusView& view);

// ---- State text ---------------------------------------------------------------------------------------------------
// version=1
// settings=<mode>,<root>,<scale>,<octave>,<bars>,<timeSignature>,<tempo>,<crossover>,<mutation>,<population>,<loop>,<clock>,<channel>,<sendClock>
// rng=<uint32>
// gen=<int> round=<int>
// rows=<comma-separated indices>
// e=<id>|<kind hex>|<degree hex>|<velocity hex>|<rating>|<voted>|<carried>|<shown>|<prior>|<parents csv>|<origin>
// a=<id>|<kind hex>|<degree hex>|<velocity hex>|<rating>|<generation>|<origin>
std::string serializeSession(const Session& session);
std::optional<Session> deserializeSession(std::string_view text);

// ---- Host parameters ---------------------------------------------------------------------------------------------
enum Param : std::uint32_t {
    kMode,
    kRoot,
    kScale,
    kOctave,
    kBars,
    kTimeSignature,
    kTempo,
    kCrossoverRate,
    kMutationRate,
    kPopulationSize,
    kLoop,
    kClockSource,
    kMidiChannel,
    kSendClock,
    kStatusPlaying,
    kStatusGeneration,
    kStatusRound,
    kStatusClockBpm,
    kStatusClockTick,
    kStatusProgress,
    kStatusClockRunning,
    kStatusLastNote,
    kStatusLastVelocity,
    kParameterCount
};

struct ParamSpec {
    const char* symbol;
    const char* name;
    float minimum;
    float maximum;
    float defaultValue;
    bool integer = false;
    bool output = false;
};

using Params = std::array<float, kParameterCount>;
extern const std::array<ParamSpec, kParameterCount> kParameterSpecs;

Params defaultParams();
Settings settingsFromParams(const Params& params);
Params paramsFromSettings(const Settings& settings);

}  // namespace downspout::melevolve
