#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Portable core for the offline drum-break re-quantiser. Nothing here includes
// DPF or touches host state: the wrapper owns audio capture, MIDI flushing and
// parameter transport.

namespace downspout::energise {

enum class DrumClass : std::uint8_t {
    Kick = 0,
    Snare,
    Clap,
    Rim,
    ClosedHat,
    OpenHat,
    Ride,
    Crash,
    LowTom,
    MidTom,
    HighTom,
    Percussion,
    Unknown,
    Count
};

inline constexpr std::size_t kClassCount = static_cast<std::size_t>(DrumClass::Count);

inline constexpr int kMinCaptureSeconds = 1;
inline constexpr int kMaxCaptureSeconds = 8;
inline constexpr int kDefaultCaptureSeconds = 4;

// Capture is armed in whole bars from the next host bar line. Four bars is the
// largest useful request at the default 120 BPM; the core still clips every
// buffer to the hard eight-second state-size limit.
inline constexpr int kMinCaptureBars = 1;
inline constexpr int kMaxCaptureBars = 4;
inline constexpr int kDefaultCaptureBars = 2;

inline constexpr std::size_t kMaxEvents = 512;

inline constexpr std::array<const char*, kClassCount> kClassNames = {{
    "Kick", "Snare", "Clap", "Rim", "Closed Hat", "Open Hat", "Ride", "Crash",
    "Low Tom", "Mid Tom", "High Tom", "Perc", "Unknown",
}};

struct DrumEvent {
    std::uint32_t id = 0;
    double sourceTime = 0.0;
    double quantisedTime = 0.0;
    double sourceDuration = 0.0;
    DrumClass primary = DrumClass::Unknown;
    std::array<float, kClassCount> scores {};
    float velocity = 0.0f;
    float confidence = 0.0f;
    bool ghost = false;
    bool muted = false;
    float pan = 0.0f;
};

struct TempoSegment {
    double startSec = 0.0;
    double bpm = 120.0;
};

struct GrooveMap {
    double bpm = 120.0;
    double phaseSec = 0.0;
    int gridDivisionsPerBeat = 4;
    float swing = 0.0f;
    std::vector<TempoSegment> drift {};
    float confidence = 0.0f;
};

// Host transport snapshot. DPF-specific TimePosition conversion stays in the
// wrapper; the portable core only needs bar/beat/tick and tempo.
struct HostPosition {
    bool valid = false;
    bool playing = false;
    std::int32_t bar = 1;
    std::int32_t beat = 1;
    double tick = 0.0;
    double ticksPerBeat = 1920.0;
    float beatsPerBar = 4.0f;
    float beatType = 4.0f;
    double bpm = 120.0;
};

struct RenderSettings {
    float quantiseStrength = 0.70f;
    float ghostStrength = 0.35f;
    float swing = 0.0f;
    float humanise = 0.0f;
    float driftAllowance = 0.25f;
    std::array<float, kClassCount> classOffsetsMs {};
    std::array<float, kClassCount> classGains = [] {
        std::array<float, kClassCount> gains {};
        gains.fill(1.0f);
        return gains;
    }();
    float transientLevel = 0.60f;
    float bodyLevel = 0.80f;
    float residualLevel = 0.35f;
    float saturation = 0.25f;
    float variation = 0.20f;
    float outputTrim = 0.70f;
};

struct CaptureBuffer {
    int sampleRate = 48000;
    std::vector<float> left {};
    std::vector<float> right {};
};

struct RenderedBuffer {
    int sampleRate = 48000;
    std::vector<float> left {};
    std::vector<float> right {};
};

struct MidiMessage {
    std::uint32_t frame = 0;
    std::uint8_t status = 0x99;  // note on, channel 10
    std::uint8_t note = 36;
    std::uint8_t velocity = 0;
};

enum class ProcessorState : std::uint8_t {
    Idle = 0,
    Captured,
    Analysed,
    Quantised,
    Rendered,
    Error
};

inline constexpr std::array<const char*, 6> kStateNames = {{
    "Idle", "Captured", "Analysed", "Quantised", "Rendered", "Error",
}};

// One advance of the request counters. Each counter is an ordinary integer
// parameter because DPF's VST3 bridge suppresses UI writes to trigger params.
struct RequestActions {
    bool capture = false;
    bool analyse = false;
    bool render = false;
    bool abSwap = false;
    bool undo = false;
    bool redo = false;
    bool exportMidi = false;
    bool demo = false;
    bool eventEdit = false;
};

[[nodiscard]] float clampValue(const float v, const float lo, const float hi) noexcept;
[[nodiscard]] int clampInt(const int v, const int lo, const int hi) noexcept;
[[nodiscard]] const char* className(const DrumClass cls) noexcept;
[[nodiscard]] DrumClass classFromIndex(const int index) noexcept;
[[nodiscard]] std::uint8_t midiNoteForClass(const DrumClass cls) noexcept;
[[nodiscard]] double gridStepSeconds(const GrooveMap& groove) noexcept;
[[nodiscard]] double beatSeconds(const HostPosition& position) noexcept;
[[nodiscard]] double barSeconds(const HostPosition& position) noexcept;
[[nodiscard]] double beatInBar(const HostPosition& position) noexcept;
[[nodiscard]] double secondsToNextBar(const HostPosition& position) noexcept;
[[nodiscard]] int effectiveCaptureBars(const HostPosition& position, const int requestedBars) noexcept;
[[nodiscard]] std::size_t captureFramesForBars(const int sampleRate,
                                               const HostPosition& position,
                                               const int requestedBars) noexcept;
[[nodiscard]] GrooveMap grooveFromHost(const HostPosition& position,
                                       const int gridDivisions,
                                       const float swing);

// The processor owns the captured PCM, editable event list, groove map and
// rendered buffer. Parameter reads include live status outputs and a downsampled
// waveform/event shadow for the UI, since DPF has no DSP -> UI state callback.
class Processor {
public:
    Processor();

    void reset();
    // Clears playback counters only. Never discards the captured patch.
    void activate(const int sampleRate);

    void setParameter(const std::uint32_t index, const float value);
    [[nodiscard]] float parameter(const std::uint32_t index) const;

    // The wrapper supplies host BPM/phase at each bar-aligned capture start.
    // Analysis then locks to this grid instead of fitting an independent tempo.
    void setHostGrid(const double bpm, const double phaseSec);
    void setHostBbtValid(const bool valid) noexcept;

    // Call once per host block. Returns the one-shot actions requested by the UI.
    [[nodiscard]] RequestActions takeRequests();

    bool capture(const CaptureBuffer& buffer);
    bool makeDemo(const int sampleRate, const std::uint32_t seed);
    bool analyse();
    bool quantise();
    bool render();

    bool setEvent(const std::uint32_t id, const DrumEvent& event);
    bool selectEvent(const int index);
    void setAbSide(const bool rendered);
    void toggleMute(const DrumClass cls);
    void toggleSolo(const DrumClass cls);

    bool undo();
    bool redo();

    [[nodiscard]] ProcessorState state() const noexcept { return state_; }
    [[nodiscard]] float progress() const noexcept { return progress_; }
    [[nodiscard]] bool renderReady() const noexcept { return renderReady_; }
    [[nodiscard]] bool showingRender() const noexcept { return showingRender_; }
    [[nodiscard]] int selectedEvent() const noexcept { return selected_; }
    [[nodiscard]] double playheadSeconds() const noexcept { return playhead_; }
    void advancePlayhead(const double seconds);

    [[nodiscard]] const CaptureBuffer& captureBuffer() const noexcept { return capture_; }
    [[nodiscard]] const RenderedBuffer& renderBuffer() const noexcept { return rendered_; }
    [[nodiscard]] const std::vector<DrumEvent>& events() const noexcept { return events_; }
    [[nodiscard]] const GrooveMap& groove() const noexcept { return groove_; }
    [[nodiscard]] const RenderSettings& settings() const noexcept { return settings_; }

    [[nodiscard]] std::vector<MidiMessage> exportMidi() const;
    [[nodiscard]] std::string serialize() const;
    bool deserialize(const std::string& text);

private:
    struct Snapshot {
        std::vector<DrumEvent> events {};
        GrooveMap groove {};
        RenderSettings settings {};
        ProcessorState state = ProcessorState::Idle;
        int selected = -1;
        bool showingRender = false;
    };

    void pushUndo();
    void syncSettings();
    void loadClassParamsFromSettings();
    void applyClassParamsToSettings();
    void applyEventEdit();
    void updateWaveform();
    void invalidateRender();

    // Parameter table is defined in energise_params.hpp; keep the storage size
    // in sync with ParamId::count via the static_assert in that header.
    std::array<float, 165> params_ {};
    std::array<std::uint32_t, 9> lastRequests_ {};
    ProcessorState state_ = ProcessorState::Idle;
    float progress_ = 0.0f;
    bool renderReady_ = false;
    bool showingRender_ = false;
    int selected_ = -1;
    double playhead_ = 0.0;
    int sampleRate_ = 48000;

    CaptureBuffer capture_ {};
    RenderedBuffer rendered_ {};
    std::vector<DrumEvent> events_ {};
    GrooveMap groove_ {};
    GrooveMap hostGroove_ {};
    RenderSettings settings_ {};
    std::vector<Snapshot> undoStack_ {};
    std::vector<Snapshot> redoStack_ {};
    std::array<float, 48> waveform_ {};
};

}  // namespace downspout::energise
