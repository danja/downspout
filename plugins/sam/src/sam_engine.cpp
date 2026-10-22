#include "sam_engine.hpp"

#include <algorithm>
#include <cmath>

namespace downspout::sam {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFadeInSeconds = 0.002;
constexpr double kTailSourceSamples = 110.0;  // about 5 ms of SAM output
constexpr float kStopMilliseconds = 8.0f;
constexpr float kAllOffMilliseconds = 20.0f;
constexpr int kFirstKeyWord = 48;             // C3 speaks the first word in Key = word mode
constexpr int kLowestPitchByte = 3;
constexpr int kMaxEventsPerBlock = 64;        // word onsets per block, a guard for tiny steps

[[nodiscard]] float clampf(const float v, const float lo, const float hi) noexcept
{
    return v < lo ? lo : (v > hi ? hi : v);
}

[[nodiscard]] bool channelMatches(const int setting, const std::uint8_t status) noexcept
{
    if (setting <= 0)
        return false;
    return setting > 16 || (status & 0x0F) + 1 == setting;
}

}  // namespace

Engine::Engine()
{
    for (std::size_t i = 0; i < kParameterCount; ++i)
        params_[i].store(kParameterSpecs[i].defaultValue);
    for (Voice& voice : voices_)
        voice.data.assign(kMaxRenderSamples, 128);
    script_ = std::make_unique<Script>(parseScript(kDefaultText));
    pendingText_ = script_->text;
}

// ── Main thread ────────────────────────────────────────────────────────────

void Engine::setText(const std::string& text)
{
    auto fresh = std::make_unique<Script>(parseScript(text));
    std::unique_ptr<Script> retired;
    {
        std::lock_guard<std::mutex> lock(scriptMutex_);
        retired = std::move(pendingScript_);
        pendingText_ = fresh->text;
        pendingScript_ = std::move(fresh);
        scriptDirty_ = true;
    }
    // `retired` is freed here, on the calling thread, after the lock is released.
}

std::string Engine::text() const
{
    std::lock_guard<std::mutex> lock(scriptMutex_);
    return pendingText_;
}

void Engine::setParameter(const std::uint32_t index, const float value)
{
    if (index >= kParameterCount)
        return;
    const ParamSpec& spec = kParameterSpecs[index];
    if (spec.output)
        return;
    if (spec.trigger) {
        if (value > 0.5f)
            speakRequested_.store(true);
        return;
    }
    float v = clampf(value, spec.minimum, spec.maximum);
    if (spec.integer)
        v = std::round(v);
    params_[index].store(v);
}

float Engine::getParameter(const std::uint32_t index) const
{
    if (index >= kParameterCount)
        return 0.0f;
    if (index == paramIndex(ParamId::outWord))
        return static_cast<float>(outWord_.load());
    return params_[index].load();
}

// ── Audio thread ───────────────────────────────────────────────────────────

void Engine::activate(const double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (Voice& voice : voices_) {
        voice.active = false;
        voice.releasing = false;
        voice.lastOut = 0.0f;
    }
    eventCount_ = 0;
    voiceClock_ = 0;
    stealTail_ = tone1_ = tone2_ = 0.0f;
    bend_ = 1.0f;
    nextSlot_ = 0;
    lastMode_ = -1;
    hostWasPlaying_ = false;
    wasLive_ = false;
    audition_ = false;
    auditionBeat_ = 0.0;
    expectedBeat_ = 0.0;
    lastTriggered_ = -1;
    currentSlot_ = -1;
    speakRequested_.store(false);
    outWord_.store(0);
}

std::size_t Engine::activeVoices() const noexcept
{
    std::size_t count = 0;
    for (const Voice& voice : voices_)
        count += voice.active ? 1 : 0;
    return count;
}

void Engine::pollScript()
{
    std::unique_lock<std::mutex> lock(scriptMutex_, std::try_to_lock);
    if (lock.owns_lock() && scriptDirty_) {
        std::swap(script_, pendingScript_);
        scriptDirty_ = false;
    }
}

Engine::Snapshot Engine::snapshot() const
{
    const auto get = [this](const ParamId id) { return params_[paramIndex(id)].load(); };
    const auto geti = [&get](const ParamId id) { return static_cast<int>(std::lround(get(id))); };

    Snapshot s;
    s.mode = geti(ParamId::mode);
    s.voice.speed = geti(ParamId::speed);
    s.voice.pitch = geti(ParamId::pitch);
    s.voice.mouth = geti(ParamId::mouth);
    s.voice.throat = geti(ParamId::throat);
    s.voice.sing = geti(ParamId::monotone) != 0;
    s.voice.phonetic = geti(ParamId::phonetic) != 0;
    s.midiCh = geti(ParamId::midiCh);
    s.advance = geti(ParamId::advance);
    s.transpose = geti(ParamId::transpose);
    s.releaseMs = get(ParamId::release);
    s.step = std::clamp(geti(ParamId::step), 0, static_cast<int>(kStepBeats.size()) - 1);
    s.startBar = geti(ParamId::startBar);
    s.loop = geti(ParamId::loop) != 0;
    s.toneHz = get(ParamId::tone);
    s.level = get(ParamId::level);
    return s;
}

void Engine::addEvent(const Event& event)
{
    if (eventCount_ >= events_.size())
        return;
    // Insertion sort keeps events in frame order, and equal frames in arrival order,
    // without the temporary buffer std::stable_sort would allocate.
    std::size_t at = eventCount_++;
    while (at > 0 && events_[at - 1].frame > event.frame) {
        events_[at] = events_[at - 1];
        --at;
    }
    events_[at] = event;
}

void Engine::collectMidi(const Snapshot& s, const MidiEvent* midi, const std::uint32_t count,
                         const std::uint32_t frames)
{
    if (midi == nullptr)
        return;
    for (std::uint32_t i = 0; i < count; ++i) {
        const MidiEvent& in = midi[i];
        if (in.size < 2)
            continue;
        const std::uint8_t status = in.data[0];
        const std::uint8_t kind = status & 0xF0;
        if (!channelMatches(s.midiCh, status))
            continue;

        Event event;
        event.frame = std::min(in.frame, frames - 1);
        event.b = status & 0x0F;
        if (kind == 0x90 && in.size >= 3 && in.data[2] != 0) {
            event.kind = Event::Kind::noteOn;
            event.a = in.data[1];
            event.b = (event.b << 8) | in.data[2];  // channel in the high byte, velocity low
        } else if (kind == 0x80 || kind == 0x90) {
            event.kind = Event::Kind::noteOff;
            event.a = in.data[1];
        } else if (kind == 0xB0 && (in.data[1] == 120 || in.data[1] == 123)) {
            event.kind = Event::Kind::allOff;
        } else if (kind == 0xE0 && in.size >= 3) {
            event.kind = Event::Kind::bend;
            event.a = ((in.data[2] << 7) | in.data[1]) - 8192;
        } else {
            continue;
        }
        addEvent(event);
    }
}

void Engine::scheduleTransport(const Snapshot& s, const Transport& t, const std::uint32_t frames)
{
    const bool live = t.valid && t.playing;
    const double bpm = (t.valid && t.bpm > 0.0) ? t.bpm : 120.0;
    const double beatsPerBar = (t.valid && t.beatsPerBar > 0.0) ? t.beatsPerBar : 4.0;
    const double startBeat = static_cast<double>(s.startBar - 1) * beatsPerBar;

    if (speakRequested_.exchange(false) && !live) {
        audition_ = true;
        auditionBeat_ = startBeat;
        wasLive_ = false;  // forces a fresh locate below
    }
    if (live)
        audition_ = false;

    if (!live && !audition_) {
        if (wasLive_)
            addEvent({0, Event::Kind::stopAll, 0, 0, 0.0});
        wasLive_ = false;
        return;
    }

    const double songBeat = live ? t.bar * beatsPerBar + t.barBeat : auditionBeat_;
    const float stepValue = kStepBeats[static_cast<std::size_t>(s.step)];
    const double stepBeats = stepValue > 0.0f ? static_cast<double>(stepValue) : -static_cast<double>(stepValue) * beatsPerBar;
    const double rel0 = songBeat - startBeat;
    const double beatsPerFrame = bpm / (60.0 * sampleRate_);
    const double blockBeats = static_cast<double>(frames) * beatsPerFrame;

    // A relocation (stop and start, a loop wrap, the user dragging the playhead) is any
    // position the previous block did not lead to. Beats are compared with a margin of a
    // hundredth of a beat because hosts report them in whole ticks.
    const bool jumped = !wasLive_ || std::fabs(songBeat - expectedBeat_) > 0.01;
    if (jumped) {
        addEvent({0, Event::Kind::stopAll, 0, 0, 0.0});
        if (rel0 >= 0.0) {
            const double slot = std::floor(rel0 / stepBeats + 1.0e-9);
            const double offset = rel0 - slot * stepBeats;
            if (offset > 1.0e-4) {
                // Entered part-way through a slot: speak that word from the matching point.
                lastTriggered_ = static_cast<std::int64_t>(slot);
                const std::size_t count = script_ ? script_->size() : 0;
                const std::int64_t index = static_cast<std::int64_t>(slot);
                const bool inRange = count > 0 && (s.loop || index < static_cast<std::int64_t>(count));
                if (inRange && !audition_) {
                    const std::int64_t word = s.loop ? index % static_cast<std::int64_t>(count) : index;
                    addEvent({0, Event::Kind::word, static_cast<int>(word), 0, offset * 60.0 / bpm});
                }
            } else {
                lastTriggered_ = static_cast<std::int64_t>(slot) - 1;
            }
        } else {
            lastTriggered_ = -1;
        }
    }

    const std::size_t count = script_ ? script_->size() : 0;
    const bool looping = s.loop && !audition_;
    int emitted = 0;
    for (std::int64_t slot = std::max<std::int64_t>(lastTriggered_ + 1, 0); emitted < kMaxEventsPerBlock; ++slot) {
        // The word starts on the first frame at or after its step. Rounding to the nearest frame
        // instead would put it one frame earlier or later depending on where the host happens to
        // cut its blocks, and the tolerance keeps a boundary that lands on a frame from slipping.
        const double slotStart = static_cast<double>(slot) * stepBeats;
        const auto frame = static_cast<std::int64_t>(std::ceil((slotStart - rel0) / beatsPerFrame - 1.0e-6));
        if (frame >= static_cast<std::int64_t>(frames))
            break;
        lastTriggered_ = slot;
        ++emitted;
        if (count == 0)
            continue;
        const std::int64_t index = static_cast<std::int64_t>(slot);
        if (!looping && index >= static_cast<std::int64_t>(count))
            continue;
        const std::int64_t word = looping ? index % static_cast<std::int64_t>(count) : index;
        addEvent({static_cast<std::uint32_t>(std::max<std::int64_t>(frame, 0)), Event::Kind::word, static_cast<int>(word), 0, 0.0});
    }

    expectedBeat_ = songBeat + blockBeats;
    wasLive_ = true;
    if (audition_) {
        auditionBeat_ += blockBeats;
        // One pass over the text, then stop once the last word has finished.
        if (lastTriggered_ >= static_cast<std::int64_t>(count) - 1 && activeVoices() == 0)
            audition_ = false;
    }
}

void Engine::apply(const Snapshot& s, const Event& event)
{
    switch (event.kind) {
    case Event::Kind::noteOn: {
        const std::size_t count = script_ ? script_->size() : 0;
        if (count == 0)
            return;
        const int note = event.a;
        const int channel = event.b >> 8;
        const int velocity = event.b & 0xFF;
        std::size_t slot;
        if (s.advance == 0) {
            slot = nextSlot_ % count;
            nextSlot_ = slot + 1;
        } else {
            const int n = static_cast<int>(count);
            slot = static_cast<std::size_t>(((note - kFirstKeyWord) % n + n) % n);
        }
        // Sung at the key's pitch: the nearest whole glottal period, then the small
        // remainder is made up by resampling so the key is in tune.
        const double hz = 440.0 * std::exp2(static_cast<double>(note + s.transpose - 69) / 12.0);
        const int pitchByte = std::clamp(static_cast<int>(std::lround(kPitchConstant / hz)), kLowestPitchByte, 255);
        const double actualHz = kPitchConstant / static_cast<double>(pitchByte);

        Snapshot sung = s;
        sung.voice.sing = true;
        sung.voice.pitch = pitchByte;
        const float gain = 0.25f + 0.75f * static_cast<float>(velocity) / 127.0f;
        startWord(sung, slot, true, hz / actualHz, gain, note, channel, 0.0);
        break;
    }
    case Event::Kind::noteOff:
        releaseVoices(s.releaseMs, event.a, event.b, true);
        break;
    case Event::Kind::allOff:
        releaseVoices(kAllOffMilliseconds, 0, 0, false);
        break;
    case Event::Kind::stopAll:
        releaseVoices(kStopMilliseconds, 0, 0, false);
        break;
    case Event::Kind::word:
        startWord(s, static_cast<std::size_t>(event.a), s.voice.sing, 1.0, 1.0f, -1, 0, event.seek);
        break;
    case Event::Kind::bend:
        bend_ = static_cast<float>(std::exp2(static_cast<double>(event.a) / 8192.0 * 2.0 / 12.0));
        break;
    }
}

Engine::Voice& Engine::takeVoice()
{
    Voice* chosen = nullptr;
    for (Voice& voice : voices_) {
        if (!voice.active) {
            chosen = &voice;
            break;
        }
        if (chosen == nullptr || voice.age < chosen->age)
            chosen = &voice;
    }
    if (chosen->active) {
        // Stolen mid-word: carry its last output into a short decay so the cut is not a click.
        stealTail_ += chosen->lastOut;
        chosen->active = false;
    }
    return *chosen;
}

void Engine::startWord(const Snapshot& s, const std::size_t slot, const bool sing, const double pitchRatio,
                       const float gain, const int note, const int channel, const double seekSeconds)
{
    if (!script_ || slot >= script_->size())
        return;
    currentSlot_ = static_cast<std::int64_t>(slot);
    const Token& token = script_->tokens[slot];
    if (token.rest)
        return;
    char word[kMaxWordChars + 1];
    const std::size_t wordLength = cleanWord(token.word, s.voice.phonetic, word);
    if (wordLength == 0)
        return;

    VoiceSettings settings = s.voice;
    settings.sing = sing;

    Voice& voice = takeVoice();
    voice.length = renderWord(word, wordLength, settings, voice.data.data(), voice.data.size());
    const double start = seekSeconds * kSamRate;
    if (voice.length < 4 || start >= static_cast<double>(voice.length) - 2.0) {
        voice.active = false;
        return;
    }

    double sum = 0.0;
    for (std::size_t i = 0; i < voice.length; ++i)
        sum += static_cast<double>(voice.data[i]);
    voice.dc = static_cast<float>((sum / static_cast<double>(voice.length) - 128.0) / 128.0);

    voice.position = start;
    voice.rate = kSamRate / sampleRate_ * pitchRatio;
    voice.gain = gain;
    voice.lastOut = 0.0f;
    voice.releaseGain = 1.0f;
    voice.releaseStep = 0.0f;
    voice.fadeIn = 0;
    voice.age = ++voiceClock_;
    voice.note = note;
    voice.channel = channel;
    voice.releasing = false;
    voice.active = true;
}

void Engine::releaseVoices(const float milliseconds, const int note, const int channel, const bool matchNote)
{
    const float samples = std::max(1.0f, static_cast<float>(sampleRate_ * 0.001) * milliseconds);
    for (Voice& voice : voices_) {
        if (!voice.active || voice.releasing)
            continue;
        if (matchNote && (voice.note != note || voice.channel != channel))
            continue;
        voice.releasing = true;
        voice.releaseStep = voice.releaseGain / samples;
    }
}

float Engine::tick(Voice& voice)
{
    const auto index = static_cast<std::size_t>(voice.position);
    if (index + 1 >= voice.length) {
        voice.active = false;
        voice.lastOut = 0.0f;
        return 0.0f;
    }
    const float frac = static_cast<float>(voice.position - static_cast<double>(index));
    const float a = static_cast<float>(voice.data[index]);
    const float b = static_cast<float>(voice.data[index + 1]);
    const float sample = ((a + (b - a) * frac) - 128.0f) * (1.0f / 128.0f) - voice.dc;

    float envelope = voice.gain;
    const double fadeSamples = std::max(1.0, sampleRate_ * kFadeInSeconds);
    if (static_cast<double>(voice.fadeIn) < fadeSamples)
        envelope *= static_cast<float>(static_cast<double>(voice.fadeIn++) / fadeSamples);
    const double remaining = static_cast<double>(voice.length - 1) - voice.position;
    if (remaining < kTailSourceSamples)
        envelope *= static_cast<float>(remaining / kTailSourceSamples);
    if (voice.releasing) {
        voice.releaseGain -= voice.releaseStep;
        if (voice.releaseGain <= 0.0f) {
            voice.active = false;
            voice.lastOut = 0.0f;
            return 0.0f;
        }
        envelope *= voice.releaseGain;
    }

    voice.position += voice.rate * static_cast<double>(bend_);
    voice.lastOut = sample * envelope;
    return voice.lastOut;
}

void Engine::renderRange(const Snapshot& s, const std::uint32_t begin, const std::uint32_t end, float* outL,
                         float* outR)
{
    const bool toneOn = s.toneHz < kParameterSpecs[paramIndex(ParamId::tone)].maximum;
    const float coefficient = toneOn ? static_cast<float>(1.0 - std::exp(-2.0 * kPi * static_cast<double>(s.toneHz) / sampleRate_)) : 1.0f;

    for (std::uint32_t frame = begin; frame < end; ++frame) {
        float sum = stealTail_;
        stealTail_ = std::fabs(stealTail_) < 1.0e-9f ? 0.0f : stealTail_ * 0.99f;
        for (Voice& voice : voices_) {
            if (voice.active)
                sum += tick(voice);
        }
        if (toneOn) {
            tone1_ += coefficient * (sum - tone1_);
            tone2_ += coefficient * (tone1_ - tone2_);
            if (std::fabs(tone1_) < 1.0e-18f) tone1_ = 0.0f;
            if (std::fabs(tone2_) < 1.0e-18f) tone2_ = 0.0f;
            sum = tone2_;
        }
        const float out = clampf(sum * s.level, -1.0f, 1.0f);
        outL[frame] = out;
        outR[frame] = out;
    }
}

void Engine::process(const Transport& transport, const MidiEvent* midi, const std::uint32_t midiCount,
                     const std::uint32_t frames, float* outL, float* outR)
{
    if (frames == 0 || outL == nullptr || outR == nullptr)
        return;

    pollScript();
    const Snapshot s = snapshot();
    eventCount_ = 0;

    if (s.mode != lastMode_) {
        // Switching between Keys and Transport: let go of whatever the other mode started.
        if (lastMode_ >= 0)
            releaseVoices(kStopMilliseconds, 0, 0, false);
        lastMode_ = s.mode;
        wasLive_ = false;
        audition_ = false;
        currentSlot_ = -1;
    }

    const bool hostPlaying = transport.valid && transport.playing;
    if (hostPlaying && !hostWasPlaying_)
        nextSlot_ = 0;  // the text starts again with the song
    hostWasPlaying_ = hostPlaying;

    if (s.mode == 0) {
        collectMidi(s, midi, midiCount, frames);
    } else {
        scheduleTransport(s, transport, frames);
    }

    std::uint32_t cursor = 0;
    for (std::size_t i = 0; i < eventCount_; ++i) {
        const Event event = events_[i];
        if (event.frame > cursor) {
            renderRange(s, cursor, event.frame, outL, outR);
            cursor = event.frame;
        }
        apply(s, event);
    }
    renderRange(s, cursor, frames, outL, outR);

    const bool sounding = s.mode == 0 ? activeVoices() > 0 : (wasLive_ || audition_);
    outWord_.store(sounding && currentSlot_ >= 0 ? static_cast<int>(currentSlot_) + 1 : 0);
}

}  // namespace downspout::sam
