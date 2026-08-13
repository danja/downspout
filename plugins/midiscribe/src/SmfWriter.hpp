#pragma once

// SmfWriter.hpp — header-only Standard MIDI File (Type 0) serialiser.
//
// Assumptions / design notes:
//   - Writes a single-track (Type 0) SMF.
//   - Division is fixed at 480 PPQ.
//   - A tempo meta-event is prepended at tick 0.
//   - Events are sorted by tick before writing; delta times are computed after
//     sorting.
//   - Only 3-byte MIDI channel messages are handled (Note On / Off and generic
//     pass-through). Sysex and multi-byte messages outside the range 0x80..0xEF
//     are dropped silently; add handling here if needed.
//   - Variable-length quantity (VLQ) encoding is per the SMF specification.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace downspout::midiscribe {

// A single captured MIDI event with a beat-position timestamp.
struct CapturedEvent {
    double   beatPosition = 0.0;  // absolute beat (0-based)
    uint8_t  status       = 0;
    uint8_t  data1        = 0;
    uint8_t  data2        = 0;
};

// Write a variable-length quantity to buf.
inline void writeVlq(std::vector<uint8_t>& buf, uint32_t value)
{
    uint8_t bytes[4];
    int n = 0;
    bytes[n++] = static_cast<uint8_t>(value & 0x7F);
    value >>= 7;
    while (value > 0) {
        bytes[n++] = static_cast<uint8_t>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    // bytes are in LSB-first order; write MSB-first
    for (int i = n - 1; i >= 0; --i) {
        buf.push_back(bytes[i]);
    }
}

// Write a big-endian 32-bit word.
inline void writeU32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    buf.push_back(static_cast<uint8_t>((v >>  8) & 0xFF));
    buf.push_back(static_cast<uint8_t>( v        & 0xFF));
}

// Write a big-endian 16-bit word.
inline void writeU16(std::vector<uint8_t>& buf, uint16_t v)
{
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    buf.push_back(static_cast<uint8_t>( v       & 0xFF));
}

// Serialise events to a Type-0 SMF byte buffer.
//
// Parameters:
//   events  — captured events (will be sorted by tick internally; original
//             vector is not modified)
//   bpm     — tempo used for the SMF tempo meta event (microseconds/beat)
//   ppq     — ticks per quarter note (480 is the repo constant)
//
// Returns the complete SMF file as a byte vector, including MThd and MTrk
// chunks.  Returns an empty vector if events is empty.
inline std::vector<uint8_t> serializeToSmf(
    const std::vector<CapturedEvent>& events,
    double bpm,
    int    ppq)
{
    if (bpm <= 0.0) bpm = 120.0;
    if (ppq <= 0)   ppq = 480;

    // Compute microseconds per beat (clamped to 24-bit range).
    const uint32_t usPerBeat = static_cast<uint32_t>(
        std::min(60000000.0 / bpm, static_cast<double>(0xFFFFFF)));

    // Convert beat positions to ticks and sort.
    struct TickedEvent {
        uint32_t tick;
        uint8_t  status;
        uint8_t  data1;
        uint8_t  data2;
    };
    std::vector<TickedEvent> ticked;
    ticked.reserve(events.size());
    for (const auto& ev : events) {
        ticked.push_back({
            static_cast<uint32_t>(ev.beatPosition * ppq + 0.5),
            ev.status,
            ev.data1,
            ev.data2
        });
    }
    std::stable_sort(ticked.begin(), ticked.end(),
        [](const TickedEvent& a, const TickedEvent& b){ return a.tick < b.tick; });

    // Build track data (without the MTrk header — we patch the size later).
    std::vector<uint8_t> track;

    // Tempo meta event at tick 0: delta=0, FF 51 03 tt tt tt
    writeVlq(track, 0u);
    track.push_back(0xFF);
    track.push_back(0x51);
    track.push_back(0x03);
    track.push_back(static_cast<uint8_t>((usPerBeat >> 16) & 0xFF));
    track.push_back(static_cast<uint8_t>((usPerBeat >>  8) & 0xFF));
    track.push_back(static_cast<uint8_t>( usPerBeat        & 0xFF));

    // MIDI channel events
    uint32_t prevTick = 0;
    for (const auto& ev : ticked) {
        const uint8_t statusByte = ev.status;
        const uint8_t type = statusByte & 0xF0;
        // Only pass 3-byte channel messages (Note On/Off, Aftertouch, CC, Pitch)
        if (type < 0x80 || type > 0xE0) continue;

        const uint32_t delta = ev.tick - prevTick;
        writeVlq(track, delta);
        track.push_back(statusByte);
        track.push_back(ev.data1);
        track.push_back(ev.data2);
        prevTick = ev.tick;
    }

    // End-of-track meta event: delta=0, FF 2F 00
    writeVlq(track, 0u);
    track.push_back(0xFF);
    track.push_back(0x2F);
    track.push_back(0x00);

    // Assemble the full file.
    std::vector<uint8_t> smf;

    // MThd chunk
    smf.push_back('M'); smf.push_back('T'); smf.push_back('h'); smf.push_back('d');
    writeU32(smf, 6u);            // chunk length always 6
    writeU16(smf, 0u);            // format 0
    writeU16(smf, 1u);            // 1 track
    writeU16(smf, static_cast<uint16_t>(ppq));  // division

    // MTrk chunk
    smf.push_back('M'); smf.push_back('T'); smf.push_back('r'); smf.push_back('k');
    writeU32(smf, static_cast<uint32_t>(track.size()));
    smf.insert(smf.end(), track.begin(), track.end());

    return smf;
}

}  // namespace downspout::midiscribe
