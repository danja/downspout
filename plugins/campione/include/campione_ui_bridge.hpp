#pragma once

#include <atomic>
#include <mutex>
#include <string>

namespace downspout::campione {

// In-process bridge for DSP→UI zone and parameter updates.
// DPF's updateStateValue is a no-op in the VST3 wrapper, so we use a shared
// singleton (same .so, same process) that the UI polls in uiIdle().
struct UiBridge {
    // Zones: DSP writes serialized zone data and bumps zonesSerial.
    // UI reads in uiIdle() whenever its cached serial differs.
    std::atomic<uint32_t> zonesSerial  {0};
    std::mutex            zonesMtx;
    std::string           zonesData;

    // Parameters: DSP writes current values and bumps paramsSerial.
    std::atomic<uint32_t> paramsSerial {0};
    std::mutex            paramsMtx;
    float masterVolume   {0.8f};
    float midiChannel    {0.0f};
    float crossfadeMs    {20.0f};
    float pitchBendRange {2.0f};
};

inline UiBridge& uiBridge()
{
    static UiBridge s;
    return s;
}

} // namespace downspout::campione
