#include "energise_core.hpp"

#include "energise_analysis.hpp"
#include "energise_demo.hpp"
#include "energise_params.hpp"
#include "energise_quantise.hpp"
#include "energise_render.hpp"
#include "energise_serialization.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace downspout::energise {
namespace {

constexpr std::size_t index(const ParamId id) noexcept { return static_cast<std::size_t>(id); }

constexpr std::array<std::uint8_t, kClassCount> kMidiNotes = {{
    36, 38, 39, 37, 42, 46, 51, 49, 45, 47, 50, 56, 36,
}};

[[nodiscard]] float finiteOr(const float value, const float fallback) noexcept
{
    return std::isfinite(value) ? value : fallback;
}

}  // namespace

float clampValue(const float v, const float lo, const float hi) noexcept
{
    if (!std::isfinite(v))
        return lo;
    return std::clamp(v, lo, hi);
}

int clampInt(const int v, const int lo, const int hi) noexcept
{
    return std::clamp(v, lo, hi);
}

const char* className(const DrumClass cls) noexcept
{
    const auto i = static_cast<std::size_t>(cls);
    return i < kClassNames.size() ? kClassNames[i] : "Unknown";
}

DrumClass classFromIndex(const int value) noexcept
{
    if (value < 0 || value >= static_cast<int>(kClassCount))
        return DrumClass::Unknown;
    return static_cast<DrumClass>(value);
}

std::uint8_t midiNoteForClass(const DrumClass cls) noexcept
{
    const auto i = static_cast<std::size_t>(cls);
    return i < kMidiNotes.size() ? kMidiNotes[i] : 36u;
}

double gridStepSeconds(const GrooveMap& groove) noexcept
{
    return 60.0 / std::max(1.0, groove.bpm) / std::max(1, groove.gridDivisionsPerBeat);
}

Processor::Processor()
{
    for (std::size_t i = 0; i < kParameterCount; ++i)
        params_[i] = kParameterSpecs[i].defaultValue;

    const std::array<ParamId, kRequestCount> requests = {{
        ParamId::captureRequest, ParamId::analyseRequest, ParamId::renderRequest,
        ParamId::abSwap, ParamId::undoRequest, ParamId::redoRequest,
        ParamId::exportMidiRequest, ParamId::demoRequest, ParamId::eventEditRequest,
    }};
    for (std::size_t i = 0; i < lastRequests_.size(); ++i)
        lastRequests_[i] = static_cast<std::uint32_t>(std::lround(params_[index(requests[i])]));

    groove_.gridDivisionsPerBeat = gridDivisions(static_cast<int>(params_[index(ParamId::grid)]));
    settings_.swing = params_[index(ParamId::swing)];
}

void Processor::reset()
{
    for (std::size_t i = 0; i < kParameterCount; ++i)
        params_[i] = kParameterSpecs[i].defaultValue;
    groove_ = GrooveMap {};
    groove_.gridDivisionsPerBeat = gridDivisions(static_cast<int>(params_[index(ParamId::grid)]));
    settings_ = RenderSettings {};
    settings_.swing = params_[index(ParamId::swing)];
    events_.clear();
    capture_ = CaptureBuffer {};
    rendered_ = RenderedBuffer {};
    undoStack_.clear();
    redoStack_.clear();
    state_ = ProcessorState::Idle;
    progress_ = 0.0f;
    renderReady_ = false;
    showingRender_ = false;
    selected_ = -1;
    playhead_ = 0.0;
    sampleRate_ = capture_.sampleRate;
    waveform_.fill(0.0f);
    for (std::size_t i = 0; i < lastRequests_.size(); ++i)
        lastRequests_[i] = 0;
}

void Processor::activate(const int sampleRate)
{
    if (sampleRate > 0)
        sampleRate_ = sampleRate;
    playhead_ = 0.0;
    progress_ = 0.0f;
}

void Processor::syncSettings()
{
    settings_.quantiseStrength = params_[index(ParamId::quantiseStrength)];
    settings_.ghostStrength = params_[index(ParamId::ghostStrength)];
    settings_.swing = params_[index(ParamId::swing)];
    settings_.humanise = params_[index(ParamId::humanise)];
    settings_.driftAllowance = params_[index(ParamId::driftAllowance)];
    settings_.transientLevel = params_[index(ParamId::transientLevel)];
    settings_.bodyLevel = params_[index(ParamId::bodyLevel)];
    settings_.residualLevel = params_[index(ParamId::residualLevel)];
    settings_.saturation = params_[index(ParamId::saturation)];
    settings_.variation = params_[index(ParamId::variation)];
    settings_.outputTrim = params_[index(ParamId::outputTrim)];
    groove_.swing = settings_.swing;
    groove_.gridDivisionsPerBeat = gridDivisions(static_cast<int>(params_[index(ParamId::grid)]));
}

void Processor::loadClassParamsFromSettings()
{
    const DrumClass cls = selected_ >= 0 && selected_ < static_cast<int>(events_.size())
                              ? events_[static_cast<std::size_t>(selected_)].primary
                              : DrumClass::Unknown;
    const auto i = static_cast<std::size_t>(cls);
    params_[index(ParamId::offsetSelectedMs)] = settings_.classOffsetsMs[i];
    params_[index(ParamId::gainSelected)] = settings_.classGains[i];
}

void Processor::applyClassParamsToSettings()
{
    const DrumClass cls = selected_ >= 0 && selected_ < static_cast<int>(events_.size())
                              ? events_[static_cast<std::size_t>(selected_)].primary
                              : DrumClass::Unknown;
    const auto i = static_cast<std::size_t>(cls);
    settings_.classOffsetsMs[i] = params_[index(ParamId::offsetSelectedMs)];
    settings_.classGains[i] = params_[index(ParamId::gainSelected)];
}

void Processor::applyEventEdit()
{
    const int slot = static_cast<int>(std::lround(params_[index(ParamId::selectedEvent)]));
    if (slot < 0 || slot >= static_cast<int>(events_.size()))
        return;
    DrumEvent edited = events_[static_cast<std::size_t>(slot)];
    edited.primary = classFromIndex(static_cast<int>(std::lround(params_[index(ParamId::editClass)])));
    const double length = static_cast<double>(capture_.left.size()) / std::max(1, capture_.sampleRate);
    edited.quantisedTime = std::clamp(static_cast<double>(params_[index(ParamId::editTime)]), 0.0, length);
    edited.velocity = clampValue(params_[index(ParamId::editVelocity)], 0.0f, 1.0f);
    edited.ghost = params_[index(ParamId::editGhost)] >= 0.5f;
    setEvent(edited.id, edited);
}

void Processor::setParameter(const std::uint32_t id, const float raw)
{
    if (id >= kParameterCount)
        return;
    const ParamSpec& spec = kParameterSpecs[id];
    if (spec.output)
        return;
    float value = finiteOr(raw, spec.defaultValue);
    value = clampValue(value, spec.minimum, spec.maximum);
    if (spec.integer)
        value = std::round(value);
    params_[id] = value;

    if (id == index(ParamId::offsetSelectedMs) || id == index(ParamId::gainSelected))
        applyClassParamsToSettings();
    if (id == index(ParamId::selectedEvent)) {
        const int slot = static_cast<int>(std::lround(value));
        selected_ = events_.empty() ? -1 : std::clamp(slot, 0, static_cast<int>(events_.size()) - 1);
        loadClassParamsFromSettings();
    }
    syncSettings();
}

float Processor::parameter(const std::uint32_t id) const
{
    if (id >= kParameterCount)
        return 0.0f;
    if (id < kInputParameterCount)
        return params_[id];

    switch (static_cast<ParamId>(id)) {
    case ParamId::status:
        return static_cast<float>(state_);
    case ParamId::progress:
        return progress_;
    case ParamId::eventCount:
        return static_cast<float>(events_.size());
    case ParamId::selectedClass: {
        if (selected_ < 0 || selected_ >= static_cast<int>(events_.size()))
            return static_cast<float>(DrumClass::Unknown);
        return static_cast<float>(events_[static_cast<std::size_t>(selected_)].primary);
    }
    case ParamId::selectedConfidence: {
        if (selected_ < 0 || selected_ >= static_cast<int>(events_.size()))
            return 0.0f;
        return events_[static_cast<std::size_t>(selected_)].confidence;
    }
    case ParamId::renderReady:
        return renderReady_ ? 1.0f : 0.0f;
    case ParamId::abSide:
        return showingRender_ ? 1.0f : 0.0f;
    case ParamId::tempoBpm:
        return static_cast<float>(groove_.bpm);
    default:
        break;
    }

    if (id >= index(ParamId::waveStart) && id < index(ParamId::eventStart))
        return waveform_[id - index(ParamId::waveStart)];

    if (id >= index(ParamId::eventStart)) {
        const std::size_t offset = id - index(ParamId::eventStart);
        const std::size_t slot = offset / kEventFields;
        const std::size_t field = offset % kEventFields;
        const std::size_t page = static_cast<std::size_t>(std::max(0, static_cast<int>(std::lround(params_[index(ParamId::eventPage)]))));
        const std::size_t eventIndex = page * kVisibleEvents + slot;
        if (eventIndex >= events_.size()) {
            if (field == 1)
                return static_cast<float>(DrumClass::Unknown);
            return 0.0f;
        }
        const DrumEvent& event = events_[eventIndex];
        switch (field) {
        case 0: return static_cast<float>(event.sourceTime);
        case 1: return static_cast<float>(event.primary);
        case 2: return event.velocity;
        case 3: return event.confidence;
        case 4: return static_cast<float>(event.quantisedTime);
        case 5: return event.ghost ? 1.0f : 0.0f;
        default: return 0.0f;
        }
    }
    return 0.0f;
}

RequestActions Processor::takeRequests()
{
    static constexpr std::array<ParamId, kRequestCount> kRequests = {{
        ParamId::captureRequest, ParamId::analyseRequest, ParamId::renderRequest,
        ParamId::abSwap, ParamId::undoRequest, ParamId::redoRequest,
        ParamId::exportMidiRequest, ParamId::demoRequest, ParamId::eventEditRequest,
    }};
    RequestActions actions {};
    bool* flags[kRequestCount] = {&actions.capture, &actions.analyse, &actions.render,
                                  &actions.abSwap, &actions.undo, &actions.redo,
                                  &actions.exportMidi, &actions.demo, &actions.eventEdit};
    for (std::size_t i = 0; i < kRequests.size(); ++i) {
        const auto value = static_cast<std::uint32_t>(std::lround(params_[index(kRequests[i])]));
        *flags[i] = value != lastRequests_[i];
        lastRequests_[i] = value;
    }
    if (actions.eventEdit)
        applyEventEdit();
    return actions;
}

void Processor::pushUndo()
{
    Snapshot snapshot {};
    snapshot.events = events_;
    snapshot.groove = groove_;
    snapshot.settings = settings_;
    snapshot.state = state_;
    snapshot.selected = selected_;
    snapshot.showingRender = showingRender_;
    undoStack_.push_back(std::move(snapshot));
    if (undoStack_.size() > 32)
        undoStack_.erase(undoStack_.begin());
    redoStack_.clear();
}

void Processor::updateWaveform()
{
    waveform_.fill(0.0f);
    const bool useRender = showingRender_ && renderReady_;
    const std::vector<float>& left = useRender ? rendered_.left : capture_.left;
    const std::vector<float>& right = useRender ? rendered_.right : capture_.right;
    const std::size_t frames = std::min(left.size(), right.size());
    if (frames == 0)
        return;
    for (std::size_t i = 0; i < waveform_.size(); ++i) {
        const std::size_t begin = frames * i / waveform_.size();
        const std::size_t end = std::max(begin + 1, frames * (i + 1) / waveform_.size());
        float peak = 0.0f;
        float last = 0.0f;
        for (std::size_t j = begin; j < end && j < frames; ++j) {
            const float sample = 0.5f * (left[j] + right[j]);
            if (std::fabs(sample) > peak)
                peak = std::fabs(sample);
            last = sample;
        }
        waveform_[i] = last < 0.0f ? -peak : peak;
    }
}

void Processor::invalidateRender()
{
    rendered_ = RenderedBuffer {};
    renderReady_ = false;
    if (state_ == ProcessorState::Rendered)
        state_ = ProcessorState::Quantised;
    showingRender_ = false;
    updateWaveform();
}

bool Processor::capture(const CaptureBuffer& buffer)
{
    if (buffer.sampleRate <= 0 || buffer.left.empty() || buffer.right.empty()) {
        state_ = ProcessorState::Error;
        return false;
    }
    const std::size_t limit = static_cast<std::size_t>(buffer.sampleRate) * static_cast<std::size_t>(kMaxCaptureSeconds);
    CaptureBuffer clipped {};
    clipped.sampleRate = buffer.sampleRate;
    clipped.left.assign(buffer.left.begin(), buffer.left.begin() + std::min(buffer.left.size(), limit));
    clipped.right.assign(buffer.right.begin(), buffer.right.begin() + std::min(buffer.right.size(), limit));

    pushUndo();
    capture_ = std::move(clipped);
    sampleRate_ = capture_.sampleRate;
    events_.clear();
    groove_ = GrooveMap {};
    groove_.gridDivisionsPerBeat = gridDivisions(static_cast<int>(params_[index(ParamId::grid)]));
    settings_ = RenderSettings {};
    syncSettings();
    rendered_ = RenderedBuffer {};
    renderReady_ = false;
    showingRender_ = false;
    selected_ = -1;
    playhead_ = 0.0;
    progress_ = 1.0f;
    state_ = ProcessorState::Captured;
    updateWaveform();
    return true;
}

bool Processor::makeDemo(const int sampleRate, const std::uint32_t seed)
{
    return capture(makeDemoBreak(sampleRate > 0 ? sampleRate : 48000, seed));
}

bool Processor::analyse()
{
    if (state_ != ProcessorState::Captured && state_ != ProcessorState::Quantised && state_ != ProcessorState::Rendered) {
        state_ = ProcessorState::Error;
        return false;
    }
    GrooveMap groove {};
    std::vector<DrumEvent> events {};
    if (!analyseCapture(capture_, groove_.gridDivisionsPerBeat, settings_.driftAllowance, groove, events)) {
        state_ = ProcessorState::Error;
        return false;
    }
    pushUndo();
    groove_ = std::move(groove);
    events_ = std::move(events);
    groove_.swing = settings_.swing;
    state_ = ProcessorState::Analysed;
    progress_ = 1.0f;
    renderReady_ = false;
    showingRender_ = false;
    selected_ = events_.empty() ? -1 : 0;
    params_[index(ParamId::selectedEvent)] = selected_ < 0 ? 0.0f : static_cast<float>(selected_);
    loadClassParamsFromSettings();
    return true;
}

bool Processor::quantise()
{
    if (state_ != ProcessorState::Analysed && state_ != ProcessorState::Quantised && state_ != ProcessorState::Rendered) {
        state_ = ProcessorState::Error;
        return false;
    }
    syncSettings();
    pushUndo();
    const double seconds = static_cast<double>(capture_.left.size()) / std::max(1, capture_.sampleRate);
    quantiseEvents(events_, groove_, settings_, static_cast<std::uint32_t>(std::lround(params_[index(ParamId::seed)])), seconds);
    state_ = ProcessorState::Quantised;
    progress_ = 1.0f;
    invalidateRender();
    return true;
}

bool Processor::render()
{
    if (state_ == ProcessorState::Analysed && !quantise())
        return false;
    if (state_ != ProcessorState::Quantised && state_ != ProcessorState::Rendered) {
        state_ = ProcessorState::Error;
        return false;
    }
    syncSettings();
    const std::uint16_t mutes = static_cast<std::uint16_t>(std::lround(params_[index(ParamId::muteMask)]));
    const std::uint16_t solos = static_cast<std::uint16_t>(std::lround(params_[index(ParamId::soloMask)]));
    rendered_ = renderBreak(capture_, events_, settings_,
                            static_cast<std::uint32_t>(std::lround(params_[index(ParamId::seed)])),
                            mutes, solos, params_[index(ParamId::outputTrim)]);
    if (rendered_.left.empty()) {
        state_ = ProcessorState::Error;
        return false;
    }
    renderReady_ = true;
    state_ = ProcessorState::Rendered;
    progress_ = 1.0f;
    updateWaveform();
    return true;
}

bool Processor::setEvent(const std::uint32_t id, const DrumEvent& event)
{
    const auto it = std::find_if(events_.begin(), events_.end(), [id](const DrumEvent& e) { return e.id == id; });
    if (it == events_.end())
        return false;
    pushUndo();
    *it = event;
    if (state_ == ProcessorState::Rendered)
        invalidateRender();
    loadClassParamsFromSettings();
    return true;
}

bool Processor::selectEvent(const int eventIndex)
{
    if (!events_.empty() && (eventIndex < -1 || eventIndex >= static_cast<int>(events_.size())))
        return false;
    selected_ = events_.empty() ? -1 : std::min(eventIndex, static_cast<int>(events_.size()) - 1);
    params_[index(ParamId::selectedEvent)] = selected_ < 0 ? 0.0f : static_cast<float>(selected_);
    loadClassParamsFromSettings();
    return true;
}

void Processor::setAbSide(const bool rendered)
{
    if (!renderReady_ && rendered)
        return;
    showingRender_ = rendered;
    updateWaveform();
}

void Processor::toggleMute(const DrumClass cls)
{
    const auto bit = classBit(cls);
    auto& mask = params_[index(ParamId::muteMask)];
    const auto current = static_cast<std::uint16_t>(std::lround(mask));
    mask = static_cast<float>((current & bit) != 0u ? (current & static_cast<std::uint16_t>(~bit)) : (current | bit));
    if (renderReady_)
        invalidateRender();
}

void Processor::toggleSolo(const DrumClass cls)
{
    const auto bit = classBit(cls);
    auto& mask = params_[index(ParamId::soloMask)];
    const auto current = static_cast<std::uint16_t>(std::lround(mask));
    mask = static_cast<float>((current & bit) != 0u ? (current & static_cast<std::uint16_t>(~bit)) : (current | bit));
    if (renderReady_)
        invalidateRender();
}

bool Processor::undo()
{
    if (undoStack_.empty())
        return false;
    Snapshot current {};
    current.events = events_;
    current.groove = groove_;
    current.settings = settings_;
    current.state = state_;
    current.selected = selected_;
    current.showingRender = showingRender_;
    const Snapshot restore = std::move(undoStack_.back());
    undoStack_.pop_back();
    redoStack_.push_back(std::move(current));
    events_ = restore.events;
    groove_ = restore.groove;
    settings_ = restore.settings;
    state_ = restore.state;
    selected_ = restore.selected;
    showingRender_ = restore.showingRender;
    params_[index(ParamId::selectedEvent)] = selected_ < 0 ? 0.0f : static_cast<float>(selected_);
    syncSettings();
    renderReady_ = state_ == ProcessorState::Rendered;
    rendered_ = renderReady_ ? rendered_ : RenderedBuffer {};
    loadClassParamsFromSettings();
    updateWaveform();
    return true;
}

bool Processor::redo()
{
    if (redoStack_.empty())
        return false;
    Snapshot current {};
    current.events = events_;
    current.groove = groove_;
    current.settings = settings_;
    current.state = state_;
    current.selected = selected_;
    current.showingRender = showingRender_;
    const Snapshot restore = std::move(redoStack_.back());
    redoStack_.pop_back();
    undoStack_.push_back(std::move(current));
    events_ = restore.events;
    groove_ = restore.groove;
    settings_ = restore.settings;
    state_ = restore.state;
    selected_ = restore.selected;
    showingRender_ = restore.showingRender;
    params_[index(ParamId::selectedEvent)] = selected_ < 0 ? 0.0f : static_cast<float>(selected_);
    syncSettings();
    renderReady_ = state_ == ProcessorState::Rendered;
    loadClassParamsFromSettings();
    updateWaveform();
    return true;
}

void Processor::advancePlayhead(const double seconds)
{
    const bool useRender = showingRender_ && renderReady_;
    const std::size_t frames = useRender ? rendered_.left.size() : capture_.left.size();
    const int rate = useRender ? rendered_.sampleRate : capture_.sampleRate;
    if (rate <= 0 || frames == 0) {
        playhead_ = 0.0;
        return;
    }
    const double length = static_cast<double>(frames) / static_cast<double>(rate);
    playhead_ = length > 0.0 ? std::fmod(playhead_ + seconds, length) : 0.0;
}

std::vector<MidiMessage> Processor::exportMidi() const
{
    const std::uint16_t solos = static_cast<std::uint16_t>(std::lround(params_[index(ParamId::soloMask)]));
    const bool anySolo = solos != 0u;
    std::vector<MidiMessage> messages {};
    for (const DrumEvent& event : events_) {
        if (event.muted)
            continue;
        const auto bit = classBit(event.primary);
        if ((solos & bit) == 0u && anySolo)
            continue;
        MidiMessage message {};
        message.frame = static_cast<std::uint32_t>(std::max(0.0, event.quantisedTime) * capture_.sampleRate);
        message.status = 0x99;  // note on, channel 10
        message.note = midiNoteForClass(event.primary);
        const float velocity = event.velocity * (event.ghost ? 0.6f : 1.0f);
        message.velocity = static_cast<std::uint8_t>(std::clamp(std::lround(velocity * 127.0f), 1L, 127L));
        messages.push_back(message);
    }
    std::sort(messages.begin(), messages.end(), [](const MidiMessage& a, const MidiMessage& b) {
        return a.frame < b.frame;
    });
    return messages;
}

std::string Processor::serialize() const
{
    StateData data {};
    data.state = state_;
    data.capture = capture_;
    data.rendered = renderReady_ ? rendered_ : RenderedBuffer {};
    data.events = events_;
    data.groove = groove_;
    data.settings = settings_;
    data.selected = selected_;
    data.eventPage = static_cast<int>(std::lround(params_[index(ParamId::eventPage)]));
    data.showingRender = showingRender_;
    return serializeState(data);
}

bool Processor::deserialize(const std::string& text)
{
    const auto loaded = deserializeState(text);
    if (!loaded)
        return false;
    const StateData& data = *loaded;
    if (data.capture.sampleRate <= 0 || data.capture.left.size() != data.capture.right.size())
        return false;
    capture_ = data.capture;
    rendered_ = data.rendered;
    events_ = data.events;
    if (events_.size() > kMaxEvents)
        events_.resize(kMaxEvents);
    groove_ = data.groove;
    settings_ = data.settings;
    state_ = data.state;
    if (state_ == ProcessorState::Rendered && rendered_.left.empty())
        state_ = ProcessorState::Quantised;
    if (rendered_.left.empty())
        renderReady_ = false;
    else if (state_ == ProcessorState::Rendered)
        renderReady_ = true;
    showingRender_ = renderReady_ && data.showingRender;
    selected_ = data.selected >= 0 && data.selected < static_cast<int>(events_.size()) ? data.selected : -1;
    params_[index(ParamId::eventPage)] = static_cast<float>(data.eventPage);
    sampleRate_ = capture_.sampleRate;
    playhead_ = 0.0;
    progress_ = 1.0f;
    syncSettings();
    params_[index(ParamId::quantiseStrength)] = settings_.quantiseStrength;
    params_[index(ParamId::ghostStrength)] = settings_.ghostStrength;
    params_[index(ParamId::swing)] = settings_.swing;
    params_[index(ParamId::humanise)] = settings_.humanise;
    params_[index(ParamId::driftAllowance)] = settings_.driftAllowance;
    params_[index(ParamId::transientLevel)] = settings_.transientLevel;
    params_[index(ParamId::bodyLevel)] = settings_.bodyLevel;
    params_[index(ParamId::residualLevel)] = settings_.residualLevel;
    params_[index(ParamId::saturation)] = settings_.saturation;
    params_[index(ParamId::variation)] = settings_.variation;
    params_[index(ParamId::outputTrim)] = settings_.outputTrim;
    loadClassParamsFromSettings();
    updateWaveform();
    return true;
}

}  // namespace downspout::energise
