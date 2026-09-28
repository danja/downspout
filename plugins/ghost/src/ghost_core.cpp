#include "ghost_core.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace downspout::ghost {
namespace {

using downspout::generative::absoluteQuarter;
using downspout::generative::ccStatus;
using downspout::generative::isDiscontinuity;
using downspout::generative::randomInt;
using downspout::generative::randomUnit;
using downspout::generative::status;

constexpr double kSlotQuarters = 0.25;   // fixed 16th-note ghost grid (v1)
constexpr double kOnsetGuardQuarters = 0.125;
constexpr std::uint32_t kMaxGhostsPerBlock = 8;
// Pentatonic-minor ladder for Notes mode fills.
constexpr std::array<int, 7> kNoteOffsets { 0, 3, 5, 7, 10, 12, 15 };

float safe(float v, float lo, float hi, float def) noexcept
{
    return std::isfinite(v) ? std::clamp(v, lo, hi) : def;
}

bool parseFloat(std::string_view text, float& value)
{
    std::string local(text);
    char* end = nullptr;
    value = std::strtof(local.c_str(), &end);
    return end && *end == '\0';
}

std::vector<std::string_view> split(std::string_view text, char delim)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t pos = text.find(delim, start);
        if (pos == std::string_view::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        parts.push_back(text.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

}  // namespace

Parameters clampParameters(const Parameters& p) noexcept
{
    Parameters out = p;
    out.sensitivity = safe(out.sensitivity, 0.0f, 1.0f, 0.5f);
    out.density     = safe(out.density,     0.0f, 1.0f, 0.35f);
    out.velocity    = std::round(safe(out.velocity, 1.0f, 127.0f, 90.0f));
    out.drag        = safe(out.drag,        0.0f, 1.0f, 0.15f);
    out.mode        = std::round(safe(out.mode,    0.0f, 1.0f, 0.0f));
    out.channel     = std::round(safe(out.channel, 1.0f, 16.0f, 10.0f));
    out.baseNote    = std::round(safe(out.baseNote, 0.0f, 127.0f, 38.0f));
    out.passInput   = std::round(safe(out.passInput, 0.0f, 1.0f, 1.0f));
    out.audioThru   = std::round(safe(out.audioThru, 0.0f, 1.0f, 0.0f));
    out.seed        = std::round(safe(out.seed, 1.0f, 65535.0f, 7.0f));
    out.ccSensitivity = std::round(safe(out.ccSensitivity, 0.0f, 127.0f, kDefaultCCSensitivity));
    out.ccDensity     = std::round(safe(out.ccDensity,     0.0f, 127.0f, kDefaultCCDensity));
    out.ccVelocity    = std::round(safe(out.ccVelocity,    0.0f, 127.0f, kDefaultCCVelocity));
    out.ccDrag        = std::round(safe(out.ccDrag,        0.0f, 127.0f, kDefaultCCDrag));
    out.ccChannel     = std::round(safe(out.ccChannel, 1.0f, 16.0f, 1.0f));
    return out;
}

void resetState(EngineState& state) noexcept
{
    state = EngineState {};
    state.lastOnsetQuarter = -1000.0;
}

MidiBlock processBlock(EngineState&        state,
                       const Parameters&   params,
                       const Transport&    transport,
                       std::uint32_t       frames,
                       double              sampleRate,
                       const float* const* inputs,
                       float* const*       outputs,
                       const MidiEvent*    midiIn,
                       std::uint32_t       midiInCount,
                       float               effectiveSensitivity,
                       float               effectiveDensity,
                       float               effectiveVelocity,
                       float               effectiveDrag) noexcept
{
    const Parameters cp = clampParameters(params);
    const float sens = std::clamp(effectiveSensitivity, 0.0f, 1.0f);
    const bool audioThru = cp.audioThru > 0.5f;
    const float dens = std::clamp(effectiveDensity, 0.0f, 1.0f);
    const float vel  = std::clamp(effectiveVelocity, 1.0f, 127.0f);
    const float drag = std::clamp(effectiveDrag, 0.0f, 1.0f);
    const std::uint64_t seed = static_cast<std::uint64_t>(std::llround(cp.seed));
    const bool drumsMode = cp.mode < 0.5f;
    const int noteChannel = drumsMode ? 10 : static_cast<int>(std::llround(cp.channel));
    const int baseNote = static_cast<int>(std::llround(cp.baseNote));
    const float threshold = 0.30f * (1.0f - sens) + 0.003f;

    MidiBlock out;

    // Pending note-offs from the previous block go first, at frame 0.
    for (std::uint32_t i = 0; i < state.pendingCount && i < EngineState::kMaxPending; ++i)
        out.push(0, status(false, state.pendingChannels[i]), state.pendingNotes[i], 0);
    state.pendingCount = 0;

    // Forward incoming MIDI (hosts expect the tap to stay in the chain).
    if (cp.passInput > 0.5f) {
        for (std::uint32_t i = 0; i < midiInCount && out.count < out.events.size(); ++i)
            out.events[out.count++] = midiIn[i];
    }

    const double bpm = transport.bpm > 0.0 ? transport.bpm : 120.0;
    const double quartersPerFrame = (sampleRate > 0.0) ? (bpm / 60.0 / sampleRate) : 0.0;
    const double blockStart = (transport.valid) ? absoluteQuarter(transport) : 0.0;

    if (isDiscontinuity(state.havePosition, state.blockEndQuarter, blockStart))
        state.haveSlot = false;

    const bool running = transport.valid && transport.playing && frames > 0 && quartersPerFrame > 0.0;
    const double blockEnd = blockStart + quartersPerFrame * static_cast<double>(frames);
    std::uint32_t ghosts = 0;

    auto emitNote = [&](std::uint32_t frame, int note, int velocity, int channel) {
        if (ghosts >= kMaxGhostsPerBlock || out.count >= out.events.size())
            return;
        if (frame >= frames)
            return;
        const auto n = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
        const auto v = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
        const auto ch = static_cast<std::uint8_t>(std::clamp(channel, 1, 16));
        out.push(frame, status(true, ch), n, v);
        if (state.pendingCount < EngineState::kMaxPending) {
            state.pendingNotes[state.pendingCount] = n;
            state.pendingChannels[state.pendingCount] = ch;
            ++state.pendingCount;
        }
        ++ghosts;
    };

    // Schedule a ghost at an absolute quarter; emits now if already due.
    auto scheduleNote = [&](double ghostQuarter, double nowQuarter, std::uint32_t nowFrame,
                            int note, int velocity) {
        const auto n = static_cast<std::uint8_t>(std::clamp(note, 0, 127));
        const auto v = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
        const auto ch = static_cast<std::uint8_t>(noteChannel);
        if (ghostQuarter <= nowQuarter) {
            emitNote(nowFrame, n, v, ch);
            return;
        }
        if (state.scheduledCount >= EngineState::kMaxPending)
            return;  // bounded: drop rather than grow
        auto& slot = state.scheduled[state.scheduledCount++];
        slot.quarter = ghostQuarter;
        slot.note = n;
        slot.velocity = v;
        slot.channel = ch;
    };

    auto ghostVoice = [&](std::int64_t slot) -> int {
        if (drumsMode)
            return (slot % 8 == 0) ? 36 : 38;  // kick on the quarter, ghost snare off it
        const int step = randomInt(seed ^ 0x51edULL, static_cast<std::uint64_t>(slot < 0 ? -slot : slot),
                                   0, static_cast<int>(kNoteOffsets.size()) - 1);
        return std::clamp(baseNote + kNoteOffsets[static_cast<std::size_t>(step)], 0, 127);
    };

    // Fire scheduled ghosts due in this block; drop ones the transport skipped.
    // A stopped transport evaporates the schedule (no stale ghosts on play).
    if (state.scheduledCount > 0) {
        if (!running) {
            state.scheduledCount = 0;
        } else {
            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < state.scheduledCount; ++i) {
                const auto sg = state.scheduled[i];
                if (sg.quarter < blockStart - 1e-6)
                    continue;  // seeked past
                if (sg.quarter < blockEnd) {
                    const auto fr = static_cast<std::uint32_t>(
                        std::llround((sg.quarter - blockStart) / quartersPerFrame));
                    emitNote(std::min(fr, frames - 1), sg.note, sg.velocity, sg.channel);
                } else {
                    state.scheduled[kept++] = sg;
                }
            }
            state.scheduledCount = kept;
        }
    }

    for (std::uint32_t f = 0; f < frames; ++f) {
        float a = (inputs && inputs[0]) ? inputs[0][f] : 0.0f;
        float b = (inputs && inputs[1]) ? inputs[1][f] : 0.0f;
        if (!std::isfinite(a)) { a = 0.0f; ++state.faults; }
        if (!std::isfinite(b)) { b = 0.0f; ++state.faults; }
        if (outputs && outputs[0]) outputs[0][f] = audioThru ? a : 0.0f;
        if (outputs && outputs[1]) outputs[1][f] = audioThru ? b : 0.0f;

        const float mono = 0.5f * (a + b);
        const float mag = std::fabs(mono);
        const float flux = std::max(0.0f, mag - state.env);
        state.env += (mag - state.env) * (mag > state.env ? 0.5f : 0.002f);
        state.lastLevel = mag;

        if (!running)
            continue;

        const double quarter = blockStart + quartersPerFrame * static_cast<double>(f);
        const auto slot = static_cast<std::int64_t>(std::floor(quarter / kSlotQuarters));

        // Onset accents: quantised to the next 16th slot plus drag.
        if (flux >= threshold && quarter - state.lastOnsetQuarter >= kOnsetGuardQuarters) {
            state.lastOnsetQuarter = quarter;
            const std::int64_t target = slot + 1;
            const double ghostQuarter =
                static_cast<double>(target) * kSlotQuarters + drag * kSlotQuarters;
            scheduleNote(ghostQuarter, quarter, f, ghostVoice(target),
                         static_cast<int>(std::llround(vel)));
        }

        // Ghost fills: off-16th slots gated by density, only when audio is present.
        if (slot != state.lastSlot || !state.haveSlot) {
            state.lastSlot = slot;
            state.haveSlot = true;
            if ((slot % 2) != 0 && state.env > 0.004f) {
                const float draw = randomUnit(seed, static_cast<std::uint64_t>(slot < 0 ? -slot : slot));
                if (draw < dens) {
                    const double ghostQuarter =
                        static_cast<double>(slot) * kSlotQuarters + drag * kSlotQuarters;
                    const float vDraw = randomUnit(seed ^ 0x9e37ULL,
                                                   static_cast<std::uint64_t>(slot < 0 ? -slot : slot));
                    const int ghostVel = static_cast<int>(std::llround(vel * (0.45f + 0.30f * vDraw)));
                    scheduleNote(ghostQuarter, quarter, f, ghostVoice(slot), ghostVel);
                }
            }
        }
    }

    if (running)
        state.blockEndQuarter = blockStart + quartersPerFrame * static_cast<double>(frames);
    state.havePosition = transport.valid;
    return out;
}

std::string serializeParameters(const Parameters& p)
{
    const Parameters cp = clampParameters(p);
    return "version=1\n"
           "sensitivity=" + std::to_string(cp.sensitivity) + "\n"
           "density=" + std::to_string(cp.density) + "\n"
           "velocity=" + std::to_string(cp.velocity) + "\n"
           "drag=" + std::to_string(cp.drag) + "\n"
           "mode=" + std::to_string(cp.mode) + "\n"
           "channel=" + std::to_string(cp.channel) + "\n"
           "base_note=" + std::to_string(cp.baseNote) + "\n"
           "pass_input=" + std::to_string(cp.passInput) + "\n"
           "audio_thru=" + std::to_string(cp.audioThru) + "\n"
           "seed=" + std::to_string(cp.seed) + "\n"
           "cc_sensitivity=" + std::to_string(cp.ccSensitivity) + "\n"
           "cc_density=" + std::to_string(cp.ccDensity) + "\n"
           "cc_velocity=" + std::to_string(cp.ccVelocity) + "\n"
           "cc_drag=" + std::to_string(cp.ccDrag) + "\n"
           "cc_channel=" + std::to_string(cp.ccChannel) + "\n";
}

std::optional<Parameters> deserializeParameters(const std::string& text)
{
    Parameters p;
    for (const std::string_view line : split(text, '\n')) {
        if (line.empty()) continue;
        const std::size_t sep = line.find('=');
        if (sep == std::string_view::npos) return std::nullopt;
        const std::string_view key = line.substr(0, sep);
        const std::string_view value = line.substr(sep + 1);
        float v = 0.0f;
        if      (key == "version") { continue; }
        else if (key == "sensitivity"    && parseFloat(value, v)) { p.sensitivity = v; }
        else if (key == "density"        && parseFloat(value, v)) { p.density = v; }
        else if (key == "velocity"       && parseFloat(value, v)) { p.velocity = v; }
        else if (key == "drag"           && parseFloat(value, v)) { p.drag = v; }
        else if (key == "mode"           && parseFloat(value, v)) { p.mode = v; }
        else if (key == "channel"        && parseFloat(value, v)) { p.channel = v; }
        else if (key == "base_note"      && parseFloat(value, v)) { p.baseNote = v; }
        else if (key == "pass_input"     && parseFloat(value, v)) { p.passInput = v; }
        else if (key == "audio_thru"      && parseFloat(value, v)) { p.audioThru = v; }
        else if (key == "seed"           && parseFloat(value, v)) { p.seed = v; }
        else if (key == "cc_sensitivity" && parseFloat(value, v)) { p.ccSensitivity = v; }
        else if (key == "cc_density"     && parseFloat(value, v)) { p.ccDensity = v; }
        else if (key == "cc_velocity"    && parseFloat(value, v)) { p.ccVelocity = v; }
        else if (key == "cc_drag"        && parseFloat(value, v)) { p.ccDrag = v; }
        else if (key == "cc_channel"     && parseFloat(value, v)) { p.ccChannel = v; }
        else { return std::nullopt; }
    }
    return clampParameters(p);
}

}  // namespace downspout::ghost
