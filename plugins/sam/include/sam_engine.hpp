#pragma once

#include "sam_params.hpp"
#include "sam_speech.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace downspout::sam {

// Host transport at the start of a block. bar and barBeat are zero-based.
struct Transport {
    bool valid = false;
    bool playing = false;
    double bar = 0.0;
    double barBeat = 0.0;
    double beatsPerBar = 4.0;
    double bpm = 120.0;
};

struct MidiEvent {
    std::uint32_t frame = 0;
    std::uint8_t size = 0;
    std::uint8_t data[3] {};
};

// The speech engine. Two ways to be played:
//
//   Keys       each note-on speaks the next word of the text (or, with Advance set to
//              Key = word, the word its key selects), sung at the pitch of the key.
//   Transport  the words are laid out one per step from the start bar of the host
//              timeline; each is spoken as the playhead reaches its step.
//
// Everything below the "main thread" section runs on the audio thread and does not
// allocate or wait: a word is rendered by SAM straight into a preallocated voice
// (tens of microseconds), and the text is swapped in with try_lock.
class Engine {
public:
    static constexpr std::size_t kVoices = 8;

    Engine();

    // ── Main thread ────────────────────────────────────────────────────────
    void setText(const std::string& text);
    [[nodiscard]] std::string text() const;
    void setParameter(std::uint32_t index, float value);
    [[nodiscard]] float getParameter(std::uint32_t index) const;

    // ── Audio thread ───────────────────────────────────────────────────────
    // Clears what is playing and where the playhead was. Parameters and text stay.
    void activate(double sampleRate);
    void process(const Transport& transport, const MidiEvent* midi, std::uint32_t midiCount,
                 std::uint32_t frames, float* outL, float* outR);

    // Number of voices currently sounding (for tests and the status display).
    [[nodiscard]] std::size_t activeVoices() const noexcept;

private:
    struct Voice {
        std::vector<std::uint8_t> data;
        std::size_t length = 0;
        double position = 0.0;  // in source samples
        double rate = 1.0;      // source samples per output sample, before bend
        float gain = 1.0f;
        float dc = 0.0f;
        float lastOut = 0.0f;
        float releaseGain = 1.0f;
        float releaseStep = 0.0f;
        std::uint32_t fadeIn = 0;
        std::uint64_t age = 0;
        int note = -1;
        int channel = 0;
        bool active = false;
        bool releasing = false;
    };

    struct Snapshot {
        int mode = 0;
        VoiceSettings voice;
        int midiCh = 17;
        int advance = 0;
        int transpose = 0;
        float releaseMs = 60.0f;
        int step = 2;
        int startBar = 1;
        bool loop = true;
        float toneHz = 8000.0f;
        float level = 0.7f;
    };

    struct Event {
        enum class Kind : std::uint8_t { noteOn, noteOff, allOff, stopAll, word, bend };
        std::uint32_t frame = 0;
        Kind kind = Kind::noteOn;
        int a = 0;       // note, slot, or bend value
        int b = 0;       // velocity or channel
        double seek = 0.0;  // seconds into the word, for a word entered mid-slot
    };

    void pollScript();
    [[nodiscard]] Snapshot snapshot() const;
    void addEvent(const Event& event);
    void collectMidi(const Snapshot& s, const MidiEvent* midi, std::uint32_t count, std::uint32_t frames);
    void scheduleTransport(const Snapshot& s, const Transport& t, std::uint32_t frames);
    void apply(const Snapshot& s, const Event& event);
    void renderRange(const Snapshot& s, std::uint32_t begin, std::uint32_t end, float* outL, float* outR);

    void startWord(const Snapshot& s, std::size_t slot, bool sing, double pitchRatio, float gain, int note,
                   int channel, double seekSeconds);
    [[nodiscard]] Voice& takeVoice();
    void releaseVoices(float milliseconds, int note, int channel, bool matchNote);
    [[nodiscard]] float tick(Voice& voice);

    // main-thread-writable, audio-thread-readable
    std::array<std::atomic<float>, kParameterCount> params_ {};
    std::atomic<bool> speakRequested_ {false};

    mutable std::mutex scriptMutex_;
    std::unique_ptr<Script> pendingScript_;
    std::string pendingText_;
    bool scriptDirty_ = false;

    // audio thread only
    std::unique_ptr<Script> script_;
    std::array<Voice, kVoices> voices_;
    std::array<Event, 512> events_ {};
    std::size_t eventCount_ = 0;
    double sampleRate_ = 48000.0;
    std::uint64_t voiceClock_ = 0;
    float stealTail_ = 0.0f;
    float tone1_ = 0.0f;
    float tone2_ = 0.0f;
    float bend_ = 1.0f;
    std::size_t nextSlot_ = 0;
    int lastMode_ = -1;
    bool hostWasPlaying_ = false;

    // transport scheduler
    bool wasLive_ = false;
    bool audition_ = false;
    double auditionBeat_ = 0.0;
    double expectedBeat_ = 0.0;
    std::int64_t lastTriggered_ = -1;
    std::int64_t currentSlot_ = -1;

    std::atomic<int> outWord_ {0};
};

}  // namespace downspout::sam
