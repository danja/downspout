#pragma once

// midiscribe_core.hpp — portable, framework-agnostic Midiscribe processing
// logic.  No DPF or OS headers are included here; file I/O is delegated to the
// caller so that this class is deterministically testable.

#include "midiscribe_params.hpp"
#include "SmfWriter.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace downspout::midiscribe {

// Controls holds the current parameter values in host-agnostic form.
struct Controls {
    bool   armed        = false;
    float  writeTrigger = 0.0f;   // momentary; set to 1.0 to fire
    int    captureBeatIndex = kDefaultCaptureBeatIndex;  // index into kCaptureBeatValues
};

// Clamp / normalise controls after any parameter update.
inline Controls clampControls(Controls c)
{
    c.captureBeatIndex = std::clamp(c.captureBeatIndex, 0, kCaptureBeatCount - 1);
    return c;
}

// The main processing core.  Thread-safety is the host's responsibility (DPF
// calls run() from the audio thread, setState/getState from other contexts);
// this class does not add locks.
class MidiscribeCore {
public:
    MidiscribeCore() = default;

    // ----- Parameter accessors -----
    Controls controls() const { return controls_; }
    void setControls(Controls c) { controls_ = clampControls(c); }

    // ----- State accessors (for DPF state serialisation) -----
    const std::string& exportPath() const { return exportPath_; }
    void setExportPath(const std::string& path) { exportPath_ = path.empty() ? kDefaultExportPath : path; }

    // ----- Per-block processing -----
    // Call once per audio block.  `events` is the array of incoming MIDI events
    // for this block; all events are forwarded to `outEvents`.  When armed,
    // events are also captured.
    //
    // `absoluteBeat` is the host transport beat position at the start of the
    //   block; pass 0.0 if unavailable.
    // `bpm` is the current tempo; stored per call and used when writing.
    // `prevWriteTrigger` tracks the previous write-trigger value so the
    //   caller can detect 0→1 transitions.
    //
    // Returns true if the write-to-file action should be performed this block.
    bool processBlock(
        const std::vector<CapturedEvent>& inEvents,
        std::vector<CapturedEvent>&       outEvents,
        double                            absoluteBeat,
        double                            bpm,
        float                             prevWriteTrigger)
    {
        // Pass all events through unconditionally.
        outEvents = inEvents;

        // Capture events when armed.
        if (controls_.armed) {
            const int capBeats = kCaptureBeatValues[controls_.captureBeatIndex];
            const double windowStart = absoluteBeat - capBeats;

            for (const auto& ev : inEvents) {
                if (static_cast<int>(buffer_.size()) < kMaxCapturedEvents) {
                    buffer_.push_back(ev);
                }
            }

            // Prune events outside the rolling window.
            buffer_.erase(
                std::remove_if(buffer_.begin(), buffer_.end(),
                    [windowStart](const CapturedEvent& e){
                        return e.beatPosition < windowStart;
                    }),
                buffer_.end());
        }

        // Detect write trigger 0→1 edge.
        const bool fired = (prevWriteTrigger < 0.5f && controls_.writeTrigger >= 0.5f);
        if (fired) {
            lastBpm_ = (bpm > 0.0) ? bpm : kDefaultBpm;
        }
        return fired;
    }

    // Write captured events to `exportPath_` as a Type-0 SMF.
    // Returns true on success, false on I/O error.
    bool writeFile()
    {
        const auto smf = serializeToSmf(buffer_, lastBpm_, kSmfPpq);
        if (smf.empty()) {
            // Nothing to write; still clear the buffer and report success.
            buffer_.clear();
            return true;
        }

        std::ofstream out(exportPath_, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(smf.data()),
                  static_cast<std::streamsize>(smf.size()));
        if (!out) return false;
        out.close();

        buffer_.clear();
        return true;
    }

    // Serialise the captured buffer to an SMF byte vector (used by tests and
    // for verifying output without touching the filesystem).
    std::vector<uint8_t> serializeBuffer(double bpm = kDefaultBpm) const
    {
        return serializeToSmf(buffer_, bpm, kSmfPpq);
    }

    // Directly append events to the buffer (for testing).
    void injectEvents(const std::vector<CapturedEvent>& events)
    {
        for (const auto& ev : events) {
            if (static_cast<int>(buffer_.size()) < kMaxCapturedEvents) {
                buffer_.push_back(ev);
            }
        }
    }

    // Clear the capture buffer.
    void resetBuffer() { buffer_.clear(); }

    int bufferSize() const { return static_cast<int>(buffer_.size()); }

    const std::vector<CapturedEvent>& buffer() const { return buffer_; }

private:
    Controls    controls_ {};
    std::string exportPath_ = kDefaultExportPath;
    double      lastBpm_    = kDefaultBpm;

    std::vector<CapturedEvent> buffer_;
};

// ----- Key=value state serialisation -----

// Serialise controls to a key=value string.
inline std::string serializeControls(const Controls& c)
{
    std::string out;
    out += "armed=";
    out += (c.armed ? "1" : "0");
    out += "\ncaptureBeatIndex=";
    out += std::to_string(c.captureBeatIndex);
    out += "\n";
    return out;
}

// Deserialise controls from a key=value string.  Returns default on failure.
inline Controls deserializeControls(const std::string& text)
{
    Controls c {};
    // Simple line-by-line parser.
    std::string::size_type pos = 0;
    while (pos < text.size()) {
        const auto nl = text.find('\n', pos);
        const std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        pos = (nl == std::string::npos) ? text.size() : nl + 1;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);

        if (key == "armed") {
            c.armed = (val == "1");
        } else if (key == "captureBeatIndex") {
            try { c.captureBeatIndex = std::stoi(val); } catch (...) {}
        }
    }
    return clampControls(c);
}

}  // namespace downspout::midiscribe
