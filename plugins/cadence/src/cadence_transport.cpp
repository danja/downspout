#include "cadence_transport.hpp"

#include <cmath>

namespace downspout::cadence {

double cadence_note_length_fraction(const Controls& controls)
{
    return clampd(static_cast<double>(controls.note_length), 0.10, 1.0);
}

double cadence_cycle_beats_for_controls(const Controls& controls, const double beatsPerBar)
{
    return clampd(static_cast<double>(controls.cycle_bars) * beatsPerBar,
                  1.0,
                  static_cast<double>(kMaxSegments) * beatsPerBar);
}

double cadence_segment_beats_for_controls(const Controls& controls, const double beatsPerBar)
{
    switch (controls.granularity)
    {
    case GRANULARITY_BEAT:
        return 1.0;
    case GRANULARITY_HALF_BAR:
        return beatsPerBar * 0.5 > 0.5 ? beatsPerBar * 0.5 : 0.5;
    case GRANULARITY_BAR:
    default:
        return beatsPerBar > 1.0 ? beatsPerBar : 1.0;
    }
}

int cadence_segment_count_for_controls(const Controls& controls, const double beatsPerBar)
{
    const double cycleBeats = cadence_cycle_beats_for_controls(controls, beatsPerBar);
    const double segmentBeats = cadence_segment_beats_for_controls(controls, beatsPerBar);
    return clampi(static_cast<int>(std::lround(cycleBeats / segmentBeats)), 1, kMaxSegments);
}

double cadence_wrapped_cycle_position(const double absBeats, const Controls& controls, const double beatsPerBar)
{
    const double cycleBeats = cadence_cycle_beats_for_controls(controls, beatsPerBar);
    double local = std::fmod(absBeats, cycleBeats);
    if (local < 0.0)
        local += cycleBeats;
    return local;
}

int cadence_segment_index_for_time(const Controls& controls,
                                   const double beatsPerBar,
                                   const int segmentCount,
                                   const double absBeats)
{
    const double cyclePos = cadence_wrapped_cycle_position(absBeats, controls, beatsPerBar);
    const double segmentBeats = cadence_segment_beats_for_controls(controls, beatsPerBar);
    int index = static_cast<int>(std::floor((cyclePos + kBeatEpsilon) / segmentBeats));
    if (index >= segmentCount)
        index = segmentCount - 1;
    return clampi(index, 0, segmentCount - 1);
}

std::uint32_t cadence_frame_for_beat(const double absBeatsStart,
                                     const double absBeatsEnd,
                                     const std::uint32_t nframes,
                                     const double targetBeat)
{
    if (nframes == 0 || absBeatsEnd <= absBeatsStart + 1e-12)
        return 0;

    const double t = clampd((targetBeat - absBeatsStart) / (absBeatsEnd - absBeatsStart), 0.0, 1.0);
    return static_cast<std::uint32_t>(clampi(static_cast<int>(std::lround(t * static_cast<double>(nframes))),
                                             0,
                                             static_cast<int>(nframes)));
}

}  // namespace downspout::cadence
